// SPDX-License-Identifier: MIT
#include "NullAudioBackendSubsystem.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Platform/BackendSelection.h"

REGISTER_SUBSYSTEM( CNullAudioBackendSubsystem )

bool CNullAudioBackendSubsystem::ShouldCreateSubsystem( CEngine& Engine ) const
{
	return NBackendSelection::IsNullRequested();
}

bool CNullAudioBackendSubsystem::Play( int32 Voice, bool bLoop, bool bFromStart )
{
	if ( Voice < 0 )
	{
		return false;
	}
	FVoiceState& State = Voices.FindOrAdd( Voice );
	if ( bFromStart )
	{
		State.Position = 0.0f;
	}
	State.bPlaying = true;
	State.bLoop = bLoop;
	++NumPlayCalls;
	return true;
}

void CNullAudioBackendSubsystem::Stop( int32 Voice )
{
	if ( FVoiceState* State = Voices.Find( Voice ) )
	{
		State->bPlaying = false;
	}
}

bool CNullAudioBackendSubsystem::IsPlaying( int32 Voice )
{
	const FVoiceState* State = Voices.Find( Voice );
	return State != nullptr && State->bPlaying;
}

void CNullAudioBackendSubsystem::SetVolume( int32 Voice, float32 Volume )
{
	if ( Voice >= 0 )
	{
		Voices.FindOrAdd( Voice ).Volume = Volume;
	}
}

void CNullAudioBackendSubsystem::SetPan( int32 Voice, float32 Pan )
{
	if ( Voice >= 0 )
	{
		Voices.FindOrAdd( Voice ).Pan = Pan;
	}
}

void CNullAudioBackendSubsystem::SetPitch( int32 Voice, float32 Pitch )
{
	if ( Voice >= 0 )
	{
		Voices.FindOrAdd( Voice ).Pitch = Pitch;
	}
}

float32 CNullAudioBackendSubsystem::GetPlayPosition( int32 Voice )
{
	const FVoiceState* State = Voices.Find( Voice );
	return State != nullptr ? State->Position : 0.0f;
}

void CNullAudioBackendSubsystem::SetPlayPosition( int32 Voice, float32 Seconds )
{
	if ( Voice >= 0 )
	{
		Voices.FindOrAdd( Voice ).Position = NMath::Max( Seconds, 0.0f );
	}
}

bool CNullAudioBackendSubsystem::SetLoopRegion( int32 Voice, float32 StartSeconds, float32 EndSeconds )
{
	if ( Voice < 0 )
	{
		return false;
	}
	FVoiceState& State = Voices.FindOrAdd( Voice );
	State.LoopStart = StartSeconds;
	State.LoopEnd = EndSeconds;
	State.bLoopRegionSet = true;
	return true;
}

CNullAudioBackendSubsystem::FVoiceState CNullAudioBackendSubsystem::GetVoiceState( int32 Voice ) const
{
	const FVoiceState* State = Voices.Find( Voice );
	return State != nullptr ? *State : FVoiceState();
}

int32 CNullAudioBackendSubsystem::GetNumPlayCalls() const
{
	return NumPlayCalls;
}
