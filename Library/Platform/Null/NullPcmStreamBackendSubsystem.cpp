// SPDX-License-Identifier: MIT
#include "NullPcmStreamBackendSubsystem.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/BackendSelection.h"

REGISTER_SUBSYSTEM( CNullPcmStreamBackendSubsystem )

bool CNullPcmStreamBackendSubsystem::ShouldCreateSubsystem( CEngine& Engine ) const
{
	return NBackendSelection::IsNullRequested();
}

int32 CNullPcmStreamBackendSubsystem::CreatePcmStream( int32 SampleRate, int32 Channels )
{
	if ( SampleRate < 1 || Channels < 1 || Channels > 8 )
	{
		return -1;
	}
	const int32 Handle = NextHandle++;
	FStreamState& State = Streams.FindOrAdd( Handle );
	State.SampleRate = SampleRate;
	State.Channels = Channels;
	return Handle;
}

void CNullPcmStreamBackendSubsystem::DeletePcmStream( int32 Stream )
{
	Streams.Remove( Stream );
}

bool CNullPcmStreamBackendSubsystem::QueuePcmStream( int32 Stream, const float32* Samples, int32 FrameCount )
{
	FStreamState* State = Streams.Find( Stream );
	if ( State == nullptr || Samples == nullptr || FrameCount <= 0 )
	{
		return false;
	}
	State->TotalQueuedFrames += FrameCount;
	State->LastFirstSample = NMath::Clamp( Samples[0], -1.0f, 1.0f );
	return true;
}

int32 CNullPcmStreamBackendSubsystem::GetPcmStreamQueuedFrames( int32 Stream )
{
	const FStreamState* State = Streams.Find( Stream );
	return State != nullptr ? static_cast<int32>( State->TotalQueuedFrames - State->ConsumedFrames ) : 0;
}

bool CNullPcmStreamBackendSubsystem::StartPcmStream( int32 Stream )
{
	FStreamState* State = Streams.Find( Stream );
	if ( State == nullptr )
	{
		return false;
	}
	State->bPlaying = true;
	return true;
}

void CNullPcmStreamBackendSubsystem::StopPcmStream( int32 Stream )
{
	if ( FStreamState* State = Streams.Find( Stream ) )
	{
		State->bPlaying = false;
	}
}

void CNullPcmStreamBackendSubsystem::ResetPcmStream( int32 Stream )
{
	if ( FStreamState* State = Streams.Find( Stream ) )
	{
		State->bPlaying = false;
		State->TotalQueuedFrames = 0;
		State->ConsumedFrames = 0;
	}
}

bool CNullPcmStreamBackendSubsystem::IsPcmStreamPlaying( int32 Stream )
{
	const FStreamState* State = Streams.Find( Stream );
	return State != nullptr && State->bPlaying;
}

int32 CNullPcmStreamBackendSubsystem::Consume( int32 Stream, int32 Frames )
{
	FStreamState* State = Streams.Find( Stream );
	if ( State == nullptr || !State->bPlaying || Frames <= 0 )
	{
		return 0;
	}
	const int64 Available = State->TotalQueuedFrames - State->ConsumedFrames;
	const int32 Taken = static_cast<int32>( Available < Frames ? Available : Frames );
	State->ConsumedFrames += Taken;
	return Taken;
}

CNullPcmStreamBackendSubsystem::FStreamState CNullPcmStreamBackendSubsystem::GetStreamState( int32 Stream ) const
{
	const FStreamState* State = Streams.Find( Stream );
	return State != nullptr ? *State : FStreamState();
}

int32 CNullPcmStreamBackendSubsystem::GetNumStreams() const
{
	return Streams.Num();
}
