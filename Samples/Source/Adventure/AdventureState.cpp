#include "AdventureState.h"

#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Audio/AudioSubsystem.h"
#include "Library/Framework/Engine/Engine.h"

void FAdventureState::PlayEffect( const char* Path, const FVector3& Position, float32 Scale )
{
	FEffectPlayParams Params;
	Params.Scale = Scale;
	Params.Loop = 0;
	FTransform Transform = FTransform::Identity;
	Transform.Position = Position;
	const TSharedPtr<IEffectPlayback> Playback = FEffectBackendRegistry::Global().Play( Path, GetAppSubsystem<CAssetSubsystem>(), Params, Transform );
	if ( Playback.IsValid() )
	{
		Effects.Add( Playback );
	}
}

void FAdventureState::PlaySound( const TSharedPtr<CSound>& Sound, float32 Pitch, float32 Volume ) const
{
	CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
	if ( Audio == nullptr || !Sound.IsValid() )
	{
		return;
	}
	FSoundPlayParams Params;
	Params.Pitch = Pitch;
	Params.Volume = Volume;
	Audio->Play( Sound, Params );
}
