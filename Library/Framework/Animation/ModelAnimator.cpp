#include "ModelAnimator.h"

#include <algorithm>
#include <cmath>

#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Ecs/SystemParams.h"

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
	Clip.DefaultSpeed = DefaultSpeed;
	Clip.DefaultFade = DefaultFade;

	if ( FClip* Existing = FindClip( Name ) )
	{
		Clip.Events = Existing->Events;   // 出来事は引き継ぐ
		*Existing = Clip;
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
	PrimaryClipSlot = INDEX_NONE;
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

float32 FModelAnimator::GetClipLength( const FString& Name ) const
{
	const FClip* Clip = FindClip( Name );
	if ( Clip == nullptr ) return 0.0f;
	// 長さは再生を始めるときに決まる。まだなら、Source から読む（const なので書き戻さない）
	if ( Clip->LengthUnits > 0.0f ) return Clip->LengthUnits / UnitsPerSecond;
	if ( Clip->Source.IsValid() && Clip->Source->IsReady() )
	{
		for ( const FModelClipInfo& Info : Clip->Source->GetClips() )
		{
			if ( Info.AnimIndex == Clip->AnimIndex ) return Info.Length / UnitsPerSecond;
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

FModelAnimator::FLayer* FModelAnimator::AcquireLayer( int32 ClipSlot )
{
	// 同じクリップの層があれば使い回す（フェードで消えかけでも、付け直さない）
	for ( FLayer& Layer : Layers )
	{
		if ( Layer.ClipSlot == ClipSlot ) return &Layer;
	}

	if ( Layers.Num() < MaxLayers )
	{
		FLayer& Layer = Layers.EmplaceGetRef();
		Layer.ClipSlot = ClipSlot;
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
	return Victim;
}

int32 FModelAnimator::FindPrimaryLayer() const
{
	if ( PrimaryClipSlot == INDEX_NONE ) return INDEX_NONE;
	return Layers.IndexOfByPredicate( [this]( const FLayer& Layer ) { return Layer.ClipSlot == PrimaryClipSlot; } );
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

bool FModelAnimator::Play( const FString& ClipName, const FModelPlayParams& Params )
{
	if ( !Instance.IsValid() ) return false;
	FClip* Clip = FindClip( ClipName );
	if ( Clip == nullptr ) return false;
	const int32 Slot = static_cast<int32>( Clip - Clips.GetData() );

	const float32 LengthUnits = ResolveLengthUnits( *Clip );
	if ( LengthUnits <= 0.0f )
	{
		LOG( LogModelAnimator, Warning, "Cannot play '%s': the length of the animation is unknown (is the model '%s' still loading?).", *ClipName,
			 Clip->Source.IsValid() ? *Clip->Source->GetPath() : "(none)" );
		return false;
	}

	// 同じクリップを再生中なら、やり直しを頼まれていない限り何もしない
	const int32 PrimaryIndex = FindPrimaryLayer();
	if ( PrimaryIndex != INDEX_NONE && Layers[PrimaryIndex].ClipSlot == Slot && Layers[PrimaryIndex].TargetWeight > 0.0f && !bFinished && !Params.bRestartIfPlaying ) return true;

	FLayer* Layer = AcquireLayer( Slot );
	if ( !Layer->bAttached )
	{
		const int32 AttachIndex = Instance->AttachAnim( Clip->Source, Clip->AnimIndex );
		if ( AttachIndex < 0 )
		{
			if ( Layer->Weight <= 0.0f ) *Layer = FLayer();   // 付けられなかったので、層を空に戻す
			return false;
		}
		Layer->AttachIndex = AttachIndex;
		Layer->bAttached = true;
	}

	const float32 LengthSeconds = LengthUnits / NMath::Max( UnitsPerSecond, 1.0e-3f );
	const bool bReuse = Layer->Weight > 0.0f && !Params.bRestartIfPlaying;   // フェードで消えかけの同じクリップは、位置を引き継ぐ
	Layer->LengthSeconds = LengthSeconds;
	Layer->ClipSpeed = Params.Speed * Clip->DefaultSpeed;
	Layer->Loop = Params.Loop.IsSet() ? Params.Loop.Get( Clip->DefaultLoop ) : Clip->DefaultLoop;
	if ( !bReuse || Params.StartNormalized > 0.0f )
	{
		const float64 Normalized = NMath::Clamp( static_cast<float64>( Params.StartNormalized ), 0.0, 1.0 );
		// 逆再生は最後から始める（終わりちょうどだと、Loop では先頭に戻ってしまうので、ほんの少し手前）
		Layer->Playhead = Layer->ClipSpeed < 0.0f && Normalized <= 0.0 ? static_cast<float64>( std::nextafter( LengthSeconds, 0.0f ) ) : Normalized * LengthSeconds;
		// 開始位置ちょうどの出来事も拾えるよう、「手前」から始まったことにする
		Layer->PrevPlayhead = Layer->Playhead - ( Layer->ClipSpeed < 0.0f ? -1.0e-6 : 1.0e-6 );
	}

	// クロスフェード: 新しいものは 0 -> 1、ほかは全部 0 へ。すべて同じ時間で終わるよう、変化の速さを揃える
	const float32 Fade = Params.FadeTime >= 0.0f ? Params.FadeTime : NMath::Max( Clip->DefaultFade, 0.0f );
	for ( FLayer& Other : Layers )
	{
		if ( &Other == Layer ) continue;
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
	Layer->TargetWeight = 1.0f;
	if ( Fade <= 0.0f )
	{
		Layer->Weight = 1.0f;
		Layer->FadeRate = 0.0f;
	}
	else
	{
		Layer->FadeRate = ( 1.0f - Layer->Weight ) / Fade;
	}

	PrimaryClipSlot = Slot;
	bFinished = false;
	bFinishedThisUpdate = false;
	return true;
}

void FModelAnimator::Stop( float32 FadeTime )
{
	const float32 Fade = NMath::Max( FadeTime, 0.0f );
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
	bFinished = false;
	bFinishedThisUpdate = false;
}

void FModelAnimator::Update( float32 DeltaTime )
{
	Events.Empty();
	bFinishedThisUpdate = false;
	// Model材質効果はanimation pauseとは独立した個体時計で更新する。
	MaterialEffect.Update( DeltaTime );
	if ( bPaused || DeltaTime <= 0.0f ) return;

	for ( FLayer& Layer : Layers )
	{
		if ( Layer.ClipSlot == INDEX_NONE || !Layer.bAttached ) continue;

		// 重み
		if ( Layer.Weight != Layer.TargetWeight )
		{
			const float32 Step = Layer.FadeRate * DeltaTime;
			if ( Layer.Weight < Layer.TargetWeight ) Layer.Weight = NMath::Min( Layer.Weight + Step, Layer.TargetWeight );
			else Layer.Weight = NMath::Max( Layer.Weight - Step, Layer.TargetWeight );
			if ( Layer.FadeRate <= 0.0f ) Layer.Weight = Layer.TargetWeight;
		}
		if ( Layer.Weight <= 0.0f && Layer.TargetWeight <= 0.0f ) continue;   // 働いていない層は、時間も進めない

		// 時間
		const float64 Delta = static_cast<float64>( DeltaTime ) * static_cast<float64>( Layer.ClipSpeed ) * static_cast<float64>( Speed );
		Layer.PrevPlayhead = Layer.Playhead;
		Layer.Playhead += Delta;
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

		if ( bReachedEnd && Layer.ClipSlot == PrimaryClipSlot && Layer.TargetWeight > 0.0f && !bFinished )
		{
			bFinished = true;
			bFinishedThisUpdate = true;
		}
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
	bFinished = false;
	bFinishedThisUpdate = false;
	Events.Empty();
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
		Info.ClipTime = NAnim::ToClipTime( Layer.Playhead, Layer.LengthSeconds, Layer.Loop ) * UnitsPerSecond;
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

	// 重みの合計を 1 にそろえて、付けているアニメーションすべての状態を毎回伝える（働いていないものは 0 にして、前の値を残さない）
	// 有効層の重み合計。
	float32 Sum = 0.0f;
	for ( const FLayer& Layer : Layers )
	{
		if ( Layer.bAttached && Layer.Weight > 0.0f ) Sum += Layer.Weight;
	}
	for ( const FLayer& Layer : Layers )
	{
		if ( !Layer.bAttached ) continue;
		// native層へ渡す正規化ブレンド率。
		const float32 Rate = Sum > 0.0f && Layer.Weight > 0.0f ? Layer.Weight / Sum : 0.0f;
		// DxLib時間単位へ変換した層の再生位置。
		const float32 Time = NAnim::ToClipTime( Layer.Playhead, Layer.LengthSeconds, Layer.Loop ) * UnitsPerSecond;
		Draw.ModelAnimLayer( Handle, Layer.AttachIndex, Time, Rate );
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
