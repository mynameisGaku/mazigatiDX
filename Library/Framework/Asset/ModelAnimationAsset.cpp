#include "ModelAnimationAsset.h"


#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Animation/ModelAnimator.h"
#include "Library/Json/JsonHelpers.h"
#include "Library/Math/MathUtils.h"

DEFINE_LOG_CATEGORY_STATIC( LogModelAnimationAsset, Log )

//=============================================================================
// FModelAnimationDefinition
//=============================================================================
int32 FModelAnimationDefinition::FindClip( const FString& Name ) const
{
	return Clips.IndexOfByPredicate( [&Name]( const FModelClipDef& Clip ) { return Clip.Name == Name; } );
}

NlohmannJson FModelAnimationDefinition::ToJson() const
{
	NlohmannJson Json = NlohmannJson::object();
	Json["Version"] = 1;
	if ( !ModelPath.IsEmpty() ) Json["Model"] = ModelPath;
	if ( !PreviewModelPath.IsEmpty() ) Json["PreviewModel"] = PreviewModelPath;
	Json["UnitsPerSecond"] = NJson::Round4( UnitsPerSecond );
	if ( Offset.x != 0.0f || Offset.y != 0.0f || Offset.z != 0.0f ) Json["Offset"] = NJson::ToJson( Offset );
	if ( RotationDegrees.x != 0.0f || RotationDegrees.y != 0.0f || RotationDegrees.z != 0.0f ) Json["Rotation"] = NJson::ToJson( RotationDegrees );
	if ( Scale.x != 1.0f || Scale.y != 1.0f || Scale.z != 1.0f ) Json["Scale"] = NJson::ToJson( Scale );

	NlohmannJson ClipArray = NlohmannJson::array();
	for ( const FModelClipDef& Clip : Clips )
	{
		NlohmannJson Item = NlohmannJson::object();
		Item["Name"] = Clip.Name;
		if ( !Clip.SourcePath.IsEmpty() ) Item["Source"] = Clip.SourcePath;
		if ( !Clip.AnimName.IsEmpty() ) Item["AnimName"] = Clip.AnimName;
		Item["AnimIndex"] = Clip.AnimIndex;
		Item["Loop"] = ToString( Clip.Loop );
		if ( Clip.Speed != 1.0f ) Item["Speed"] = NJson::Round4( Clip.Speed );
		Item["Fade"] = NJson::Round4( Clip.FadeTime );
		if ( !Clip.Events.IsEmpty() )
		{
			NlohmannJson Events = NlohmannJson::array();
			for ( const FModelEventDef& Event : Clip.Events )
			{
				NlohmannJson EventItem = NlohmannJson::object();
				EventItem["Time"] = NJson::Round4( Event.Time );
				EventItem["Name"] = Event.Name;
				Events.push_back( EventItem );
			}
			Item["Events"] = Events;
		}
		ClipArray.push_back( Item );
	}
	Json["Clips"] = ClipArray;
	return Json;
}

bool FModelAnimationDefinition::FromJson( const NlohmannJson& Json, FModelAnimationDefinition& OutDefinition )
{
	if ( !Json.is_object() ) return false;

	FModelAnimationDefinition Definition;
	Definition.ModelPath = NJson::GetOr<FString>( Json, "Model", FString() );
	Definition.PreviewModelPath = NJson::GetOr<FString>( Json, "PreviewModel", FString() );
	Definition.UnitsPerSecond = NMath::Max( NJson::GetOr<float32>( Json, "UnitsPerSecond", 30.0f ), 1.0e-3f );
	Definition.Offset = NJson::ReadVector3( Json, "Offset", FVector3::Zero );
	Definition.RotationDegrees = NJson::ReadVector3( Json, "Rotation", FVector3::Zero );
	Definition.Scale = NJson::ReadVector3( Json, "Scale", FVector3::One );

	if ( Json.contains( "Clips" ) && Json["Clips"].is_array() )
	{
		for ( const NlohmannJson& Item : Json["Clips"] )
		{
			FModelClipDef Clip;
			Clip.Name = NJson::GetOr<FString>( Item, "Name", FString() );
			if ( Clip.Name.IsEmpty() )
			{
				LOG( LogModelAnimationAsset, Warning, "A clip without a Name was skipped." );
				continue;
			}
			Clip.SourcePath = NJson::GetOr<FString>( Item, "Source", FString() );
			Clip.AnimName = NJson::GetOr<FString>( Item, "AnimName", FString() );
			Clip.AnimIndex = NJson::GetOr<int32>( Item, "AnimIndex", 0 );
			Clip.Loop = ParseLoopMode( NJson::GetOr<FString>( Item, "Loop", FString( "Loop" ) ), EAnimLoopMode::Loop );
			Clip.Speed = NJson::GetOr<float32>( Item, "Speed", 1.0f );
			Clip.FadeTime = NJson::GetOr<float32>( Item, "Fade", 0.2f );
			if ( Item.contains( "Events" ) && Item["Events"].is_array() )
			{
				for ( const NlohmannJson& EventItem : Item["Events"] )
				{
					FModelEventDef Event;
					Event.Time = NMath::Clamp( NJson::GetOr<float32>( EventItem, "Time", 0.0f ), 0.0f, 1.0f );
					Event.Name = NJson::GetOr<FString>( EventItem, "Name", FString() );
					if ( !Event.Name.IsEmpty() ) Clip.Events.Add( Event );
				}
			}
			Definition.Clips.Add( Clip );
		}
	}

	OutDefinition = Definition;
	return true;
}

//=============================================================================
// CModelAnimationAsset
//=============================================================================
CModelAnimationAsset::CModelAnimationAsset( const FString& InName, const FModelAnimationDefinition& InDefinition, const TSharedPtr<CModel>& InModel,
											const TArray<TSharedPtr<CModel>>& InClipSources )
	: CAsset( EAssetType::ModelAnimation, InName )
	, Definition( InDefinition )
	, Model( InModel )
	, ClipSources( InClipSources )
{
}

TSharedPtr<CModel> CModelAnimationAsset::GetClipSource( int32 ClipIndex ) const
{
	return ClipSources.IsValidIndex( ClipIndex ) ? ClipSources[ClipIndex] : TSharedPtr<CModel>();
}

bool CModelAnimationAsset::IsReady() const
{
	if ( Model.IsValid() && !Model->IsReady() ) return false;
	for ( const TSharedPtr<CModel>& Source : ClipSources )
	{
		if ( Source.IsValid() && !Source->IsReady() ) return false;
	}
	return true;
}

FString CModelAnimationAsset::Describe() const
{
	return FString::Printf( "%d clip(s), model '%s'", Definition.Clips.Num(), Model.IsValid() ? *Model->GetPath() : "(none)" );
}

int64 CModelAnimationAsset::GetMemoryBytes() const
{
	int64 Bytes = static_cast<int64>( sizeof( *this ) );
	for ( const FModelClipDef& Clip : Definition.Clips ) Bytes += static_cast<int64>( sizeof( FModelClipDef ) ) + Clip.Events.Num() * static_cast<int64>( sizeof( FModelEventDef ) );
	return Bytes;
}

int32 CModelAnimationAsset::ApplyTo( FModelAnimator& Animator, const TSharedPtr<CModel>& FallbackSource ) const
{
	Animator.ClearClips();
	Animator.UnitsPerSecond = Definition.UnitsPerSecond;
	Animator.ModelOffset = Definition.Offset;
	Animator.ModelRotation = FQuaternion::EulerToQuat( NMath::ToRadians( Definition.RotationDegrees.x ), NMath::ToRadians( Definition.RotationDegrees.y ),
													   NMath::ToRadians( Definition.RotationDegrees.z ) );
	Animator.ModelScale = Definition.Scale;

	int32 NumAdded = 0;
	for ( int32 Index = 0; Index < Definition.Clips.Num(); ++Index )
	{
		const FModelClipDef& Clip = Definition.Clips[Index];
		TSharedPtr<CModel> Source = ClipSources.IsValidIndex( Index ) && ClipSources[Index].IsValid() ? ClipSources[Index] : Model;
		if ( !Source.IsValid() ) Source = FallbackSource;
		if ( !Source.IsValid() )
		{
			LOG( LogModelAnimationAsset, Warning, "'%s': the clip '%s' has no animation file and the set has no model, skipped.", *GetPath(), *Clip.Name );
			continue;
		}

		// 名前があれば名前で、無ければ（または見つからなければ）番号で
		int32 AnimIndex = Clip.AnimIndex;
		if ( !Clip.AnimName.IsEmpty() )
		{
			if ( const FModelClipInfo* Info = Source->FindClip( Clip.AnimName ) ) AnimIndex = Info->AnimIndex;
			else if ( Source->IsReady() ) LOG( LogModelAnimationAsset, Warning, "'%s': the animation '%s' was not found in '%s'. Animation %d is used.", *GetPath(), *Clip.AnimName, *Source->GetPath(), AnimIndex );
		}

		Animator.AddClip( Clip.Name, Source, AnimIndex, Clip.Loop, Clip.Speed, Clip.FadeTime );
		for ( const FModelEventDef& Event : Clip.Events ) Animator.AddEvent( Clip.Name, Event.Time, Event.Name );
		++NumAdded;
	}
	return NumAdded;
}

FModelAnimator CModelAnimationAsset::CreateAnimator() const
{
	FModelAnimator Animator;
	if ( Model.IsValid() ) Animator.Instance = Model->CreateInstance();   // メッシュのないファイルは、ここで null（理由はログに出る）
	ApplyTo( Animator );
	return Animator;
}

FModelAnimator CModelAnimationAsset::CreateAnimatorFor( const TSharedPtr<CModel>& Character ) const
{
	FModelAnimator Animator;
	if ( Character.IsValid() ) Animator.Instance = Character->CreateInstance();
	ApplyTo( Animator, Character );
	return Animator;
}
