// SPDX-License-Identifier: MIT
#include "NullAudioBackend.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Platform/BackendSelection.h"

REGISTER_SUBSYSTEM( CNullAudioBackend )

bool CNullAudioBackend::ShouldCreateSubsystem( CEngine& Engine ) const
{
	return NBackendSelection::IsNullRequested();
}

bool CNullAudioBackend::Play( int32 Voice, bool bLoop, bool bFromStart )
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

void CNullAudioBackend::Stop( int32 Voice )
{
	if ( FVoiceState* State = Voices.Find( Voice ) )
	{
		State->bPlaying = false;
	}
}

bool CNullAudioBackend::IsPlaying( int32 Voice )
{
	const FVoiceState* State = Voices.Find( Voice );
	return State != nullptr && State->bPlaying;
}

void CNullAudioBackend::SetVolume( int32 Voice, float32 Volume )
{
	if ( Voice >= 0 )
	{
		Voices.FindOrAdd( Voice ).Volume = Volume;
	}
}

void CNullAudioBackend::SetPan( int32 Voice, float32 Pan )
{
	if ( Voice >= 0 )
	{
		Voices.FindOrAdd( Voice ).Pan = Pan;
	}
}

void CNullAudioBackend::SetPitch( int32 Voice, float32 Pitch )
{
	if ( Voice >= 0 )
	{
		Voices.FindOrAdd( Voice ).Pitch = Pitch;
	}
}

float32 CNullAudioBackend::GetPlayPosition( int32 Voice )
{
	const FVoiceState* State = Voices.Find( Voice );
	return State != nullptr ? State->Position : 0.0f;
}

void CNullAudioBackend::SetPlayPosition( int32 Voice, float32 Seconds )
{
	if ( Voice >= 0 )
	{
		Voices.FindOrAdd( Voice ).Position = NMath::Max( Seconds, 0.0f );
	}
}

CNullAudioBackend::FVoiceState CNullAudioBackend::GetVoiceState( int32 Voice ) const
{
	const FVoiceState* State = Voices.Find( Voice );
	return State != nullptr ? *State : FVoiceState();
}

int32 CNullAudioBackend::GetNumPlayCalls() const
{
	return NumPlayCalls;
}
