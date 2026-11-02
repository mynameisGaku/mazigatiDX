#include "ModelAnimator.h"


#include "Library/Core/Log/Log.h"
#include "Library/Framework/Animation/AnimationBlend.h"
#include "Library/Framework/Animation/ModelAnimationSampler.h"
#include "Library/Framework/Animation/RootMotion.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Ecs/SystemParams.h"
#include "Library/Math/MathUtils.h"

DEFINE_LOG_CATEGORY_STATIC( LogModelAnimator, Log )

//-----------------------------------------------------------------------------
// クリップの登録
//-----------------------------------------------------------------------------
FModelAnimator::FClip* FModelAnimator::FindClip( const FString& Name )
{
	return Clips.FindByPredicate( [&Name]( const FClip& Clip ) { return Clip.Name == Name; } );
}

const FModelAnimator::FClip* FModelAnimator::FindClip( const FString& Name ) const
{
	return Clips.FindByPredicate( [&Name]( const FClip& Clip ) { return Clip.Name == Name; } );
}

void FModelAnimator::AddClip( const FString& Name, const TSharedPtr<CModel>& Source, int32 AnimIndex, EAnimLoopMode DefaultLoop, float32 DefaultSpeed, float32 DefaultFade )
{
	FClip Clip;
	Clip.Name = Name;
	Clip.Source = Source;
	Clip.AnimIndex = AnimIndex;
	Clip.DefaultLoop = DefaultLoop;
	Clip.DefaultSpeed = NMath::IsFinite( DefaultSpeed ) ? DefaultSpeed : 1.0f;
	Clip.DefaultFade = NMath::IsFinite( DefaultFade ) ? NMath::Max( DefaultFade, 0.0f ) : 0.2f;

	if ( FClip* Existing = FindClip( Name ) )
	{
		const int32 ExistingSlot = static_cast<int32>( Existing - Clips.GetData() ); // 置換前にactive slotを記録する。
		const bool bAttachedSlot = Layers.ContainsByPredicate( [ExistingSlot]( const FLayer& Layer ) { return Layer.ClipSlot == ExistingSlot && Layer.bAttached; } ); // outgoing層も旧sourceの添付を保持する。
		Clip.Events = Existing->Events;   // 出来事は引き継ぐ
		*Existing = Clip;
		if ( ExistingSlot == PrimaryClipSlot || bAttachedSlot )
		{
			bRootMotionClipReplacementRequiresClear = true;
			InvalidateRootMotion( true );
		}
		return;
	}
	Clips.Add( Clip );
}

int32 FModelAnimator::AddClipsFrom( const TSharedPtr<CModel>& Source, const FString& Prefix )
{
	if ( !Source.IsValid() ) return 0;
	if ( !Source->IsReady() )
	{
		LOG( LogModelAnimator, Warning, "AddClipsFrom: the model '%s' is still loading. No clip was added.", *Source->GetPath() );
		return 0;
	}

	int32 Added = 0;
	for ( const FModelClipInfo& Info : Source->GetClips() )
	{
		const FString Name = Prefix + ( Info.Name.IsEmpty() ? FString::Printf( "Anim%d", Info.AnimIndex ) : Info.Name );
		AddClip( Name, Source, Info.AnimIndex );
		++Added;
	}
	return Added;
}

bool FModelAnimator::HasClip( const FString& Name ) const
{
	return FindClip( Name ) != nullptr;
}

TArray<FString> FModelAnimator::GetClipNames() const
{
	TArray<FString> Names;
	for ( const FClip& Clip : Clips ) Names.Add( Clip.Name );
	return Names;
}

void FModelAnimator::ClearClips()
{
	Stop( 0.0f );
	for ( FLayer& Layer : Layers ) DetachLayer( Layer );
	Layers.Empty();
	Clips.Empty();
	PartParents.Empty();
	PartTouchedFrames.Empty();
	PrimaryClipSlot = INDEX_NONE;
	PartClipSlot = INDEX_NONE;
	PartFrame = -1;
	ClearPendingRootMotion();
	bRootMotionRequiresRestart = bRootMotionInstanceMismatch;
	bRootMotionClipReplacementRequiresClear = false;
}

float32 FModelAnimator::ResolveLengthUnits( FClip& Clip ) const
{
	if ( Clip.LengthUnits > 0.0f ) return Clip.LengthUnits;
	if ( !Clip.Source.IsValid() || !Clip.Source->IsReady() ) return 0.0f;

	for ( const FModelClipInfo& Info : Clip.Source->GetClips() )
	{
		if ( Info.AnimIndex == Clip.AnimIndex )
		{
			Clip.LengthUnits = Info.Length;
			break;
		}
	}
	return Clip.LengthUnits;
}

float32 FModelAnimator::ResolveUnitsPerSecond() const
{
	return NMath::IsFinite( UnitsPerSecond ) && UnitsPerSecond > 0.0f ? UnitsPerSecond : 30.0f;
}

float32 FModelAnimator::GetClipLength( const FString& Name ) const
{
	const FClip* Clip = FindClip( Name );
	if ( Clip == nullptr ) return 0.0f;
	// 長さは再生を始めるときに決まる。まだなら、Source から読む（const なので書き戻さない）
	if ( Clip->LengthUnits > 0.0f && NMath::IsFinite( Clip->LengthUnits ) )
	{
		const float32 Seconds = Clip->LengthUnits / NMath::Max( ResolveUnitsPerSecond(), 1.0e-3f );
		return NMath::IsFinite( Seconds ) ? Seconds : 0.0f;
	}
	if ( Clip->Source.IsValid() && Clip->Source->IsReady() )
	{
		for ( const FModelClipInfo& Info : Clip->Source->GetClips() )
		{
			if ( Info.AnimIndex == Clip->AnimIndex && NMath::IsFinite( Info.Length ) && Info.Length > 0.0f )
			{
				const float32 Seconds = Info.Length / NMath::Max( ResolveUnitsPerSecond(), 1.0e-3f );
				return NMath::IsFinite( Seconds ) ? Seconds : 0.0f;
			}
		}
	}
	return 0.0f;
}

bool FModelAnimator::AddEvent( const FString& ClipName, float32 NormalizedTime, const FString& EventName )
{
	FClip* Clip = FindClip( ClipName );
	if ( Clip == nullptr ) return false;

	FClip::FEvent Event;
	Event.Normalized = NMath::Clamp( NormalizedTime, 0.0f, 1.0f );
	Event.Name = EventName;
	Clip->Events.Add( Event );
	return true;
}

//-----------------------------------------------------------------------------
// 層（重ねて再生するアニメーション）
//-----------------------------------------------------------------------------
void FModelAnimator::DetachLayer( FLayer& Layer )
{
	if ( Layer.bAttached && Instance.IsValid() )
	{
		// 別の層が同じ付け方（番号）を使っていたら、外さない
		bool bShared = false;
		for ( const FLayer& Other : Layers )
		{
			if ( &Other != &Layer && Other.bAttached && Other.AttachIndex == Layer.AttachIndex ) bShared = true;
		}
		if ( !bShared ) Instance->DetachAnim( Layer.AttachIndex );
	}
	Layer = FLayer();
}

FModelAnimator::FLayer* FModelAnimator::AcquireLayer( int32 ClipSlot, int32 InPartFrame )
{
	// 同じクリップ・同じ骨の層があれば使い回す（フェードで消えかけでも、付け直さない）
	for ( FLayer& Layer : Layers )
	{
		if ( Layer.ClipSlot == ClipSlot && Layer.PartFrame == InPartFrame )
		{
			return &Layer;
		}
	}

	if ( Layers.Num() < MaxLayers )
	{
		FLayer& Layer = Layers.EmplaceGetRef();
		Layer.ClipSlot = ClipSlot;
		Layer.PartFrame = InPartFrame;
		return &Layer;
	}

	// 一杯なら、働いていない層を、無ければ重みの小さい層を捨てる
	FLayer* Victim = nullptr;
	for ( FLayer& Layer : Layers )
	{
		if ( Layer.Weight <= 0.0f && Layer.TargetWeight <= 0.0f )
		{
			Victim = &Layer;
			break;
		}
	}
	if ( Victim == nullptr )
	{
		for ( FLayer& Layer : Layers )
		{
			if ( Victim == nullptr || Layer.Weight < Victim->Weight ) Victim = &Layer;
		}
	}
	DetachLayer( *Victim );
	Victim->ClipSlot = ClipSlot;
	Victim->PartFrame = InPartFrame;
	return Victim;
}

int32 FModelAnimator::FindPrimaryLayer() const
{
	if ( PrimaryClipSlot == INDEX_NONE ) return INDEX_NONE;
	return Layers.IndexOfByPredicate( [this]( const FLayer& Layer ) { return Layer.ClipSlot == PrimaryClipSlot && Layer.PartFrame < 0; } );
}

//-----------------------------------------------------------------------------
// 再生の操作
//-----------------------------------------------------------------------------
bool FModelAnimator::Play( const FString& ClipName, float32 FadeTime )
{
	FModelPlayParams Params;
	Params.FadeTime = FadeTime;
	return Play( ClipName, Params );
}

FModelAnimator::FLayer* FModelAnimator::StartClip( FClip& Clip, int32 InPartFrame, const FModelPlayParams& Params, float32& OutFade )
{
	const float32 LengthUnits = ResolveLengthUnits( Clip );
	const float32 Units = ResolveUnitsPerSecond();
	const float32 ClipSpeed = Params.Speed * Clip.DefaultSpeed;
	const float32 RequestedFade = Params.FadeTime >= 0.0f ? Params.FadeTime : Clip.DefaultFade;
	const float32 Fade = NMath::IsFinite( RequestedFade ) && RequestedFade > 1.0e-6f ? RequestedFade : 0.0f;
	if ( !NMath::IsFinite( LengthUnits ) || LengthUnits <= 0.0f || !NMath::IsFinite( Units ) || Units <= 0.0f || !NMath::IsFinite( ClipSpeed ) || !NMath::IsFinite( Fade ) || !NMath::IsFinite( Params.StartNormalized ) )
	{
		LOG( LogModelAnimator, Warning, "Cannot play '%s': its animation data or playback settings are invalid (is the model '%s' still loading?).", *Clip.Name,
			 Clip.Source.IsValid() ? *Clip.Source->GetPath() : "(none)" );
		return nullptr;
	}
	const float32 LengthSeconds = LengthUnits / NMath::Max( Units, 1.0e-3f );
	if ( !NMath::IsFinite( LengthSeconds ) || LengthSeconds <= 0.0f )
	{
		return nullptr;
	}
	const int32 Slot = static_cast<int32>( &Clip - Clips.GetData() );
	FLayer* Layer = AcquireLayer( Slot, InPartFrame );
	if ( !Layer->bAttached )
	{
		const int32 AttachIndex = Instance->AttachAnim( Clip.Source, Clip.AnimIndex, false ); // このAnimator専用のanimation添付。
		if ( AttachIndex < 0 )
		{
			if ( Layer->Weight <= 0.0f ) *Layer = FLayer();   // 付けられなかったので、層を空に戻す
			return nullptr;
		}
		Layer->AttachIndex = AttachIndex;
		Layer->bAttached = true;
		Layer->AttachedInstance = Instance;
		if ( !bHasPlaybackInstance )
		{
			PlaybackInstance = Instance;
			bHasPlaybackInstance = true;
		}
		else if ( !PlaybackInstance.IsValid() || !PlaybackInstance.HasSameObject( Instance.Get() ) )
		{
			bRootMotionInstanceMismatch = true;
			InvalidateRootMotion( true );
		}
	}

	const bool bReuse = Layer->Weight > 0.0f && !Params.bRestartIfPlaying;   // フェードで消えかけの同じクリップは、位置を引き継ぐ
	Layer->LengthSeconds = LengthSeconds;
	Layer->ClipSpeed = ClipSpeed;
	Layer->UnitsPerSecondAtStart = Units;
	Layer->Loop = Params.Loop.IsSet() ? Params.Loop.Get( Clip.DefaultLoop ) : Clip.DefaultLoop;
	if ( !bReuse || Params.StartNormalized > 0.0f )
	{
		const float64 Normalized = NMath::Clamp( static_cast<float64>( Params.StartNormalized ), 0.0, 1.0 );
		// 逆再生は最後から始める（終わりちょうどだと、Loop では先頭に戻ってしまうので、ほんの少し手前）
		Layer->Playhead = Layer->ClipSpeed < 0.0f && Normalized <= 0.0 ? static_cast<float64>( NMath::NextAfter( LengthSeconds, 0.0f ) ) : Normalized * LengthSeconds;
		// 開始位置ちょうどの出来事も拾えるよう、「手前」から始まったことにする
		Layer->PrevPlayhead = Layer->Playhead - ( Layer->ClipSpeed < 0.0f ? -1.0e-6 : 1.0e-6 );
	}
	OutFade = Fade;
	return Layer;
}

void FModelAnimator::CrossFadeTo( FLayer& Layer, float32 Fade )
{
	// 新しいものは 0 -> 1、同じ組（体全体か、体の一部か）のほかは全部 0 へ。すべて同じ時間で終わるよう、変化の速さを揃える
	const bool bPart = Layer.PartFrame >= 0;
	for ( FLayer& Other : Layers )
	{
		if ( &Other == &Layer || ( Other.PartFrame >= 0 ) != bPart )
		{
			continue;
		}
		Other.TargetWeight = 0.0f;
		if ( Fade <= 0.0f )
		{
			Other.Weight = 0.0f;
			Other.FadeRate = 0.0f;
		}
		else
		{
			Other.FadeRate = Other.Weight / Fade;
		}
	}
	Layer.TargetWeight = 1.0f;
	if ( Fade <= 0.0f )
	{
		Layer.Weight = 1.0f;
		Layer.FadeRate = 0.0f;
	}
	else
	{
		Layer.FadeRate = ( 1.0f - Layer.Weight ) / Fade;
	}
}

bool FModelAnimator::Play( const FString& ClipName, const FModelPlayParams& Params )
{
	if ( !Instance.IsValid() )
	{
		return false;
	}
	FClip* Clip = FindClip( ClipName );
	if ( Clip == nullptr )
	{
		return false;
	}
	const int32 Slot = static_cast<int32>( Clip - Clips.GetData() );

	// 同じクリップを再生中なら、やり直しを頼まれていない限り何もしない
	const int32 PrimaryIndex = FindPrimaryLayer();
	if ( PrimaryIndex != INDEX_NONE && Layers[PrimaryIndex].ClipSlot == Slot && Layers[PrimaryIndex].TargetWeight > 0.0f && !bFinished && !Params.bRestartIfPlaying && ResolveLengthUnits( *Clip ) > 0.0f )
	{
		return true;
	}

	float32 Fade = 0.0f;
	FLayer* Layer = StartClip( *Clip, -1, Params, Fade );
	if ( Layer == nullptr )
	{
		return false;
	}
	CrossFadeTo( *Layer, Fade );

	PrimaryClipSlot = Slot;
	bFinished = false;
	bFinishedThisUpdate = false;
	ClearPendingRootMotion();
	if ( !bRootMotionClipReplacementRequiresClear && !bRootMotionInstanceMismatch )
	{
		bRootMotionRequiresRestart = false;
	}
	return true;
}

bool FModelAnimator::PlayPart( const FString& ClipName, const FString& BoneName, const FModelPlayParams& Params )
{
	if ( !Instance.IsValid() || !Instance->GetSource().IsValid() )
	{
		return false;
	}
	FClip* Clip = FindClip( ClipName );
	const TArray<FModelFrameInfo>& Frames = Instance->GetSource()->GetStructure().Frames;
	const TArray<FString>& BoneNames = Instance->GetSource()->GetBoneNames();
	const int32 Frame = BoneNames.Find( BoneName );
	if ( Clip == nullptr || Frame == INDEX_NONE || Frames.Num() != BoneNames.Num() )
	{
		LOG( LogModelAnimator, Warning, "PlayPart: %s '%s' was not found or the model hierarchy is unavailable.", Clip == nullptr ? "the clip" : "the bone", Clip == nullptr ? *ClipName : *BoneName );
		return false;
	}
	TArray<int32> Parents;
	for ( const FModelFrameInfo& FrameInfo : Frames )
	{
		Parents.Add( FrameInfo.Parent );
	}
	if ( !NAnimationBlend::IsValidHierarchy( Parents ) )
	{
		return false;
	}
	const int32 Slot = static_cast<int32>( Clip - Clips.GetData() );
	// 同じクリップ・同じ骨で動かしているなら、やり直しを頼まれていない限り何もしない
	if ( PartClipSlot == Slot && PartFrame == Frame && !Params.bRestartIfPlaying )
	{
		for ( const FLayer& Layer : Layers )
		{
			if ( Layer.ClipSlot == Slot && Layer.PartFrame == Frame && Layer.TargetWeight > 0.0f )
			{
				return true;
			}
		}
	}

	float32 Fade = 0.0f;
	FLayer* Layer = StartClip( *Clip, Frame, Params, Fade );
	if ( Layer == nullptr )
	{
		return false;
	}
	CrossFadeTo( *Layer, Fade );
	PartParents = Parents;
	if ( PartTouchedFrames.Num() != Parents.Num() )
	{
		PartTouchedFrames.SetNum( Parents.Num(), 0u );
	}
	for ( int32 TargetFrame = 0; TargetFrame < Parents.Num(); ++TargetFrame )
	{
		int32 Current = TargetFrame;
		for ( int32 Steps = 0; Current >= 0 && Steps < Parents.Num(); ++Steps )
		{
			if ( Current == Frame )
			{
				PartTouchedFrames[TargetFrame] = 1;
				break;
			}
			Current = Parents[Current];
		}
	}
	PartClipSlot = Slot;
	PartFrame = Frame;
	return true;
}

void FModelAnimator::StopPart( float32 FadeTime )
{
	const float32 Fade = NMath::IsFinite( FadeTime ) && FadeTime > 1.0e-6f ? FadeTime : 0.0f;
	for ( FLayer& Layer : Layers )
	{
		if ( Layer.PartFrame < 0 )
		{
			continue;
		}
		Layer.TargetWeight = 0.0f;
		if ( Fade <= 0.0f )
		{
			Layer.Weight = 0.0f;
			Layer.FadeRate = 0.0f;
		}
		else
		{
			Layer.FadeRate = Layer.Weight / Fade;
		}
	}
	PartClipSlot = INDEX_NONE;
	PartFrame = -1;
}

FString FModelAnimator::GetPartClip() const
{
	return Clips.IsValidIndex( PartClipSlot ) ? Clips[PartClipSlot].Name : FString();
}

FString FModelAnimator::GetPartBone() const
{
	if ( PartFrame < 0 || !Instance.IsValid() || !Instance->GetSource().IsValid() )
	{
		return FString();
	}
	const TArray<FString>& Bones = Instance->GetSource()->GetBoneNames();
	return Bones.IsValidIndex( PartFrame ) ? Bones[PartFrame] : FString();
}

void FModelAnimator::Stop( float32 FadeTime )
{
	const float32 Fade = NMath::IsFinite( FadeTime ) && FadeTime > 1.0e-6f ? FadeTime : 0.0f;
	for ( FLayer& Layer : Layers )
	{
		Layer.TargetWeight = 0.0f;
		if ( Fade <= 0.0f )
		{
			Layer.Weight = 0.0f;
			Layer.FadeRate = 0.0f;
		}
		else
		{
			Layer.FadeRate = Layer.Weight / Fade;
		}
	}
	PrimaryClipSlot = INDEX_NONE;
	PartClipSlot = INDEX_NONE;
	PartFrame = -1;
	bFinished = false;
	bFinishedThisUpdate = false;
	ClearPendingRootMotion();
}

void FModelAnimator::Update( float32 DeltaTime )
{
	Events.Empty();
	bFinishedThisUpdate = false;
	if ( bHasPlaybackInstance && ( !PlaybackInstance.IsValid() || !PlaybackInstance.HasSameObject( Instance.Get() ) ) )
	{
		bRootMotionInstanceMismatch = true;
		InvalidateRootMotion( true );
	}
	if ( RootMotionFrame >= 0 && ( !RootMotionConfiguredInstance.IsValid() || !RootMotionConfiguredInstance.HasSameObject( Instance.Get() ) ) )
	{
		bRootMotionInstanceMismatch = true;
		InvalidateRootMotion( true );
	}
	const int32 PrimaryIndex = FindPrimaryLayer(); // 変更前の全身層を特定する。
	if ( PrimaryIndex != INDEX_NONE && Layers[PrimaryIndex].UnitsPerSecondAtStart != ResolveUnitsPerSecond() )
	{
		InvalidateRootMotion( true );
	}
	if ( PendingRootMotion.bValid && PendingRootMotion.UnitsPerSecond != ResolveUnitsPerSecond() )
	{
		InvalidateRootMotion( true );
	}
	if ( !NMath::IsFinite( DeltaTime ) || DeltaTime <= 0.0f )
	{
		return;
	}
	// Model材質効果はanimation pauseとは独立した個体時計で更新する。
	MaterialEffect.Update( DeltaTime );
	if ( bPaused )
	{
		return;
	}
	const bool bTrackRootMotion = RootMotionFrame >= 0 && PrimaryIndex != INDEX_NONE && Layers[PrimaryIndex].PartFrame < 0 && Layers[PrimaryIndex].bAttached; // root区間を追う全身層。
	const float64 RootMotionFrom = bTrackRootMotion ? Layers[PrimaryIndex].Playhead : 0.0; // Update開始時刻を保存する。

	for ( FLayer& Layer : Layers )
	{
		if ( Layer.ClipSlot == INDEX_NONE || !Layer.bAttached ) continue;

		// 重み
		if ( Layer.Weight != Layer.TargetWeight )
		{
			const float64 Step = static_cast<float64>( Layer.FadeRate ) * static_cast<float64>( DeltaTime );
			const float64 Distance = NMath::Abs( static_cast<float64>( Layer.TargetWeight ) - static_cast<float64>( Layer.Weight ) );
			if ( !NMath::IsFinite( Step ) || Step >= Distance || Layer.FadeRate <= 0.0f )
			{
				Layer.Weight = Layer.TargetWeight;
			}
			else if ( Layer.Weight < Layer.TargetWeight )
			{
				Layer.Weight += static_cast<float32>( Step );
			}
			else
			{
				Layer.Weight -= static_cast<float32>( Step );
			}
		}
		if ( Layer.Weight <= 0.0f && Layer.TargetWeight <= 0.0f ) continue;   // 働いていない層は、時間も進めない

		// 時間
		const float64 PlaybackSpeed = NMath::IsFinite( Speed ) ? static_cast<float64>( Speed ) : 0.0;
		const float64 Delta = static_cast<float64>( DeltaTime ) * static_cast<float64>( Layer.ClipSpeed ) * PlaybackSpeed;
		if ( !NMath::IsFinite( Delta ) )
		{
			continue;
		}
		Layer.PrevPlayhead = Layer.Playhead;
		const float64 NextPlayhead = Layer.Playhead + Delta;
		if ( !NMath::IsFinite( NextPlayhead ) )
		{
			continue;
		}
		Layer.Playhead = NextPlayhead;
		const float64 Length = static_cast<float64>( Layer.LengthSeconds );
		bool bReachedEnd = false;
		if ( Layer.Loop == EAnimLoopMode::Once )
		{
			if ( Layer.Playhead >= Length && Delta > 0.0 )
			{
				Layer.Playhead = Length;
				bReachedEnd = true;
			}
			else if ( Layer.Playhead <= 0.0 && Delta < 0.0 )
			{
				Layer.Playhead = 0.0;
				bReachedEnd = true;
			}
		}

		// 出来事
		const FClip& Clip = Clips[Layer.ClipSlot];
		for ( const FClip::FEvent& Event : Clip.Events )
		{
			const int32 Count = NAnim::CountMarkerCrossings( Layer.PrevPlayhead, Layer.Playhead, Event.Normalized * Layer.LengthSeconds, Layer.LengthSeconds, Layer.Loop );
			for ( int32 i = 0; i < Count; ++i ) Events.Add( Event.Name );
		}

		if ( bReachedEnd && Layer.PartFrame < 0 && Layer.ClipSlot == PrimaryClipSlot && Layer.TargetWeight > 0.0f && !bFinished )
		{
			bFinished = true;
			bFinishedThisUpdate = true;
		}
	}
	if ( bTrackRootMotion && Layers.IsValidIndex( PrimaryIndex ) && Layers[PrimaryIndex].ClipSlot == PrimaryClipSlot )
	{
		AccumulateRootMotion( Layers[PrimaryIndex], RootMotionFrom, Layers[PrimaryIndex].Playhead );
	}
}

bool FModelAnimator::PrepareMaterialEffect( int32 MaterialIndex, const FMeshMaterialEffectDefinition& Definition, const TCallable<TSharedPtr<CTexture>( const FString& )>& LoadTexture, const IDrawExecutor& Executor, bool bTwoSided )
{
	if ( !Instance.IsValid() )
	{
		MaterialEffect.Clear();
		return false;
	}
	return MaterialEffect.Prepare( Instance->GetSource(), MaterialIndex, Definition, LoadTexture, Executor, bTwoSided );
}

void FModelAnimator::ClearMaterialEffect()
{
	MaterialEffect.Clear();
}

FModelMaterialEffectRenderer& FModelAnimator::GetMaterialEffect()
{
	return MaterialEffect;
}

const FModelMaterialEffectRenderer& FModelAnimator::GetMaterialEffect() const
{
	return MaterialEffect;
}

void FModelAnimator::Seek( float32 NormalizedTime )
{
	const int32 Index = FindPrimaryLayer();
	if ( Index == INDEX_NONE ) return;

	FLayer& Layer = Layers[Index];
	// 1 ちょうどだと、Loop では先頭に戻ってしまうので、手前で止める
	const float64 Normalized = NMath::Clamp( static_cast<float64>( NormalizedTime ), 0.0, 0.999999 );
	Layer.Playhead = Normalized * static_cast<float64>( Layer.LengthSeconds );
	Layer.PrevPlayhead = Layer.Playhead;
	ClearPendingRootMotion();
	bFinished = false;
	bFinishedThisUpdate = false;
	Events.Empty();
}

void FModelAnimator::ClearPendingRootMotion()
{
	PendingRootMotion = FRootMotionInterval();
}

void FModelAnimator::InvalidateRootMotion( bool bRequireRestart )
{
	ClearPendingRootMotion();
	if ( bRequireRestart )
	{
		bRootMotionRequiresRestart = true;
	}
}

bool FModelAnimator::IsRootMotionIdentityValid() const
{
	if ( RootMotionFrame < 0 || !Instance.IsValid() || !RootMotionConfiguredInstance.IsValid() || !RootMotionConfiguredInstance.HasSameObject( Instance.Get() ) || bRootMotionRequiresRestart || bRootMotionClipReplacementRequiresClear || bRootMotionInstanceMismatch )
	{
		return false;
	}
	if ( bHasPlaybackInstance && ( !PlaybackInstance.IsValid() || !PlaybackInstance.HasSameObject( Instance.Get() ) ) )
	{
		return false;
	}
	if ( PendingRootMotion.bValid && ( !PendingRootMotion.Instance.IsValid() || !PendingRootMotion.Instance.HasSameObject( Instance.Get() ) ) )
	{
		return false;
	}
	return true;
}

void FModelAnimator::AccumulateRootMotion( const FLayer& Layer, float64 From, float64 To )
{
	if ( RootMotionFrame < 0 || bRootMotionRequiresRestart || bRootMotionClipReplacementRequiresClear || bRootMotionInstanceMismatch || !Instance.IsValid() || !Layer.AttachedInstance.IsValid() || !Layer.AttachedInstance.HasSameObject( Instance.Get() ) )
	{
		return;
	}
	if ( Layer.ClipSlot < 0 || Layer.ClipSlot >= Clips.Num() || Layer.UnitsPerSecondAtStart != ResolveUnitsPerSecond() )
	{
		InvalidateRootMotion( true );
		return;
	}
	const FClip& Clip = Clips[Layer.ClipSlot]; // 区間のclip情報を固定する。
	if ( !Clip.Source.IsValid() || !NMath::IsFinite( Clip.LengthUnits ) || Clip.LengthUnits <= 0.0f || !NMath::IsFinite( Layer.LengthSeconds ) || Layer.LengthSeconds <= 0.0f || !NMath::IsFinite( From ) || !NMath::IsFinite( To ) )
	{
		InvalidateRootMotion( true );
		return;
	}
	if ( From == To )
	{
		return;
	}
	if ( PendingRootMotion.bValid )
	{
		const bool bSameInterval = PendingRootMotion.To == From && PendingRootMotion.ClipSlot == Layer.ClipSlot && PendingRootMotion.Source.Get() == Clip.Source.Get() && PendingRootMotion.AnimIndex == Clip.AnimIndex && PendingRootMotion.LengthUnits == Clip.LengthUnits && PendingRootMotion.LengthSeconds == Layer.LengthSeconds && PendingRootMotion.UnitsPerSecond == Layer.UnitsPerSecondAtStart && PendingRootMotion.Loop == Layer.Loop && PendingRootMotion.Frame == RootMotionFrame && PendingRootMotion.Instance.IsValid() && PendingRootMotion.Instance.HasSameObject( Instance.Get() ); // 連続性と全条件を固定する。
		if ( !bSameInterval )
		{
			InvalidateRootMotion( true );
			return;
		}
		PendingRootMotion.To = To;
		return;
	}
	PendingRootMotion.bValid = true;
	PendingRootMotion.From = From;
	PendingRootMotion.To = To;
	PendingRootMotion.ClipSlot = Layer.ClipSlot;
	PendingRootMotion.Source = Clip.Source;
	PendingRootMotion.AnimIndex = Clip.AnimIndex;
	PendingRootMotion.LengthUnits = Clip.LengthUnits;
	PendingRootMotion.LengthSeconds = Layer.LengthSeconds;
	PendingRootMotion.UnitsPerSecond = Layer.UnitsPerSecondAtStart;
	PendingRootMotion.Loop = Layer.Loop;
	PendingRootMotion.Frame = RootMotionFrame;
	PendingRootMotion.Instance = Instance;
}

bool FModelAnimator::ConfigureRootMotionBone( const FString& BoneName )
{
	GAME_THREAD_ONLY( false );
	if ( !NAssetService::IsMainThread() )
	{
		return false;
	}
	if ( bHasPlaybackInstance && ( !PlaybackInstance.IsValid() || !PlaybackInstance.HasSameObject( Instance.Get() ) ) )
	{
		bRootMotionInstanceMismatch = true;
		InvalidateRootMotion( true );
	}
	if ( RootMotionFrame >= 0 && ( !RootMotionConfiguredInstance.IsValid() || !RootMotionConfiguredInstance.HasSameObject( Instance.Get() ) ) )
	{
		bRootMotionInstanceMismatch = true;
		InvalidateRootMotion( true );
	}
	const int32 PrimaryIndex = FindPrimaryLayer(); // 現在再生中の時間単位を確認する。
	if ( PrimaryIndex != INDEX_NONE && Layers[PrimaryIndex].UnitsPerSecondAtStart != ResolveUnitsPerSecond() )
	{
		InvalidateRootMotion( true );
	}
	if ( !Instance.IsValid() || !Instance->GetSource().IsValid() || !Instance->GetSource()->IsReady() || !Instance->GetSource()->HasMesh() || bRootMotionInstanceMismatch || bRootMotionRequiresRestart || bRootMotionClipReplacementRequiresClear )
	{
		return false;
	}
	const int32 Frame = Instance->GetSource()->GetBoneNames().Find( BoneName ); // 骨名から選択番号を求める。
	if ( Frame < 0 )
	{
		return false;
	}
	RootMotionFrame = Frame;
	RootMotionConfiguredInstance = Instance;
	ClearPendingRootMotion();
	return true;
}

void FModelAnimator::ClearRootMotionBone()
{
	GAME_THREAD_ONLY();
	if ( !NAssetService::IsMainThread() )
	{
		return;
	}
	RootMotionFrame = INDEX_NONE;
	RootMotionConfiguredInstance.Reset();
	ClearPendingRootMotion();
	RootMotionSampler.Reset();
	RootMotionSamplerTarget.Reset();
	RootMotionSamplerSource.Reset();
	RootMotionSamplerAnimIndex = INDEX_NONE;
}

bool FModelAnimator::ConsumeRootMotion( FMatrix4x4& OutDelta )
{
	GAME_THREAD_ONLY( false );
	if ( !NAssetService::IsMainThread() )
	{
		return false;
	}
	if ( bHasPlaybackInstance && ( !PlaybackInstance.IsValid() || !PlaybackInstance.HasSameObject( Instance.Get() ) ) )
	{
		bRootMotionInstanceMismatch = true;
		InvalidateRootMotion( true );
	}
	if ( RootMotionFrame >= 0 && ( !RootMotionConfiguredInstance.IsValid() || !RootMotionConfiguredInstance.HasSameObject( Instance.Get() ) ) )
	{
		bRootMotionInstanceMismatch = true;
		InvalidateRootMotion( true );
	}
	const int32 PrimaryIndex = FindPrimaryLayer(); // 公開時間単位がPlay時の値か調べる。
	if ( PrimaryIndex != INDEX_NONE && Layers[PrimaryIndex].UnitsPerSecondAtStart != ResolveUnitsPerSecond() )
	{
		InvalidateRootMotion( true );
	}
	if ( PendingRootMotion.bValid && PendingRootMotion.UnitsPerSecond != ResolveUnitsPerSecond() )
	{
		InvalidateRootMotion( true );
	}
	if ( !PendingRootMotion.bValid || !IsRootMotionIdentityValid() )
	{
		return false;
	}
	if ( PrimaryIndex == INDEX_NONE || !Layers[PrimaryIndex].bAttached || !Layers[PrimaryIndex].AttachedInstance.IsValid() || !Layers[PrimaryIndex].AttachedInstance.HasSameObject( Instance.Get() ) || Layers[PrimaryIndex].UnitsPerSecondAtStart != PendingRootMotion.UnitsPerSecond )
	{
		bRootMotionInstanceMismatch = bHasPlaybackInstance && ( !PlaybackInstance.IsValid() || !PlaybackInstance.HasSameObject( Instance.Get() ) );
		InvalidateRootMotion( true );
		return false;
	}
	const FRootMotionInterval Interval = PendingRootMotion; // 失敗時に保つ区間のcopy。
	const TSharedPtr<CModel>& Target = Instance->GetSource(); // samplerの対象meshを得る。
	if ( !Target.IsValid() )
	{
		return false;
	}
	if ( Interval.From != Interval.To && ( !RootMotionSampler.IsValid() || RootMotionSamplerTarget.Get() != Target.Get() || RootMotionSamplerSource.Get() != Interval.Source.Get() || RootMotionSamplerAnimIndex != Interval.AnimIndex ) )
	{
		TSharedPtr<CModelAnimationSampler> NewSampler = CModelAnimationSampler::Create( Target, Interval.Source, Interval.AnimIndex ); // main threadで必要時だけ作る。
		if ( !NewSampler.IsValid() )
		{
			return false;
		}
		RootMotionSampler = MoveTemp( NewSampler );
		RootMotionSamplerTarget = Target;
		RootMotionSamplerSource = Interval.Source;
		RootMotionSamplerAnimIndex = Interval.AnimIndex;
	}
	FMatrix4x4 Delta; // 成功が確定するまで呼出側出力を触らない。
	const TCallable<bool( float32, FMatrix4x4& )> Sample = [this, &Interval]( float32 SampleSeconds, FMatrix4x4& OutPose ) {
			if ( !RootMotionSampler.IsValid() || !NMath::IsFinite( SampleSeconds ) )
			{
				return false;
			}
			const float64 NativeTime64 = static_cast<float64>( SampleSeconds ) * static_cast<float64>( Interval.UnitsPerSecond ); // 再生時と同じ時間単位へ変換する。
			if ( !NMath::IsFinite( NativeTime64 ) )
			{
				return false;
			}
			const float32 NativeTime = static_cast<float32>( NMath::Max( 0.0, NMath::Min( NativeTime64, static_cast<float64>( Interval.LengthUnits ) ) ) ); // float丸め後もclip範囲へ収める。
			return RootMotionSampler->SampleFrameLocalMatrix( Interval.Frame, NativeTime, OutPose );
		};
	const bool bExtracted = NRootMotion::ExtractDelta( Interval.From, Interval.To, Interval.LengthSeconds, Interval.Loop, Sample, Delta ); // 完全な差分を一時行列へ求める。
	if ( !bExtracted )
	{
		return false;
	}
	OutDelta = Delta;
	ClearPendingRootMotion();
	return true;
}

//-----------------------------------------------------------------------------
// 状態
//-----------------------------------------------------------------------------
FString FModelAnimator::GetCurrentClip() const
{
	return Clips.IsValidIndex( PrimaryClipSlot ) ? Clips[PrimaryClipSlot].Name : FString();
}

bool FModelAnimator::IsPlaying() const
{
	return PrimaryClipSlot != INDEX_NONE && !bFinished && !bPaused;
}

float32 FModelAnimator::GetNormalizedTime() const
{
	const int32 Index = FindPrimaryLayer();
	if ( Index == INDEX_NONE ) return 0.0f;
	const FLayer& Layer = Layers[Index];
	return NAnim::ToNormalized( Layer.Playhead, Layer.LengthSeconds, Layer.Loop );
}

float32 FModelAnimator::GetPlayTime() const
{
	const int32 Index = FindPrimaryLayer();
	if ( Index == INDEX_NONE ) return 0.0f;
	const FLayer& Layer = Layers[Index];
	return NAnim::ToClipTime( Layer.Playhead, Layer.LengthSeconds, Layer.Loop );
}

int32 FModelAnimator::GetNumActiveLayers() const
{
	int32 Count = 0;
	for ( const FLayer& Layer : Layers )
	{
		if ( Layer.bAttached && ( Layer.Weight > 0.0f || Layer.TargetWeight > 0.0f ) ) ++Count;
	}
	return Count;
}

TArray<FModelAnimator::FLayerInfo> FModelAnimator::GetLayerInfos() const
{
	float32 Sum = 0.0f;
	for ( const FLayer& Layer : Layers )
	{
		if ( Layer.bAttached && Layer.Weight > 0.0f ) Sum += Layer.Weight;
	}

	TArray<FLayerInfo> Infos;
	for ( const FLayer& Layer : Layers )
	{
		if ( !Layer.bAttached ) continue;
		FLayerInfo Info;
		Info.ClipName = Clips.IsValidIndex( Layer.ClipSlot ) ? Clips[Layer.ClipSlot].Name : FString();
		Info.Weight = Sum > 0.0f && Layer.Weight > 0.0f ? Layer.Weight / Sum : 0.0f;
		Info.ClipTime = NAnim::ToClipTime( Layer.Playhead, Layer.LengthSeconds, Layer.Loop ) * ResolveUnitsPerSecond();
		Info.AttachIndex = Layer.AttachIndex;
		Infos.Add( Info );
	}
	return Infos;
}

bool FModelAnimator::ConsumeEvent( const FString& Name )
{
	const int32 Index = Events.Find( Name );
	if ( Index == INDEX_NONE ) return false;
	Events.RemoveAt( Index );
	return true;
}

//-----------------------------------------------------------------------------
// 描画
//-----------------------------------------------------------------------------
void FModelAnimator::Record( FDrawContext& Draw, const FMatrix4x4& World ) const
{
	if ( !bVisible || !Instance.IsValid() ) return;
	// このobject個体のnative model handle。
	const int32 Handle = Instance->GetHandle();
	if ( Handle < 0 ) return;

	// 付けているアニメーションすべての時間と割合を毎回伝える（働いていないものは 0 にして、前の値を残さない）。
	// 割合は NAnimationBlend で決める（体全体の層は合計 1、体の一部の層は、その骨とその子だけ）
	TArray<FAnimationBlendLayer> BlendLayers;
	for ( const FLayer& Layer : Layers )
	{
		BlendLayers.Add( FAnimationBlendLayer { Layer.bAttached ? Layer.Weight : 0.0f, Layer.PartFrame } );
	}
	const FAnimationBlendResult Blend = NAnimationBlend::Compute( BlendLayers, PartParents, PartTouchedFrames );
	for ( int32 Index = 0; Index < Layers.Num(); ++Index )
	{
		const FLayer& Layer = Layers[Index];
		if ( !Layer.bAttached ) continue;
		// モデルの時間の単位へ変換した層の再生位置。
		const float32 Time = NAnim::ToClipTime( Layer.Playhead, Layer.LengthSeconds, Layer.Loop ) * ResolveUnitsPerSecond();
		Draw.ModelAnimLayer( Handle, Layer.AttachIndex, NMath::IsFinite( Time ) ? Time : 0.0f, Blend.WholeRates[Index] );
	}
	for ( const FAnimationFrameRate& FrameRate : Blend.FrameRates )
	{
		const FLayer& Layer = Layers[FrameRate.Layer];
		if ( Layer.bAttached )
		{
			Draw.ModelAnimFrameRate( Handle, Layer.AttachIndex, FrameRate.Frame, FrameRate.Rate, false );
		}
	}

	// Animator固有の補正行列。
	const FMatrix4x4 Local = FMatrix4x4::TRS( ModelOffset, ModelRotation, ModelScale );
	// 既存のModelと材質効果の双方へ渡す完全なworld matrix。
	const FMatrix4x4 ModelWorld = Local * World;
	// 選択材質効果が記録を引き受けたときは全Model命令を重ねない。
	if ( MaterialEffect.Record( Draw, Instance, ModelWorld ) ) return;
	Draw.Model( Handle, ModelWorld );
}

void FModelAnimator::RecordAt( FDrawContext& Draw, const FTransformView& Transform ) const
{
	Record( Draw, Transform.GetWorldMatrix() );
}
