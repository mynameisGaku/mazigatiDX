// SPDX-License-Identifier: MIT
#include "PcmStream.h"

#include "Library/Core/Interfaces/Audio/PcmStreamBackend.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Framework/Audio/AudioSubsystem.h"

TSharedPtr<CPcmStream> CPcmStream::Create( IPcmStreamBackend& Backend, int32 SampleRate, int32 Channels )
{
	GAME_THREAD_ONLY( nullptr );
	if ( SampleRate <= 0 || Channels < 1 || Channels > 8 )
	{
		LOG( LogAudio, Error, "Cannot create the PCM stream: %d Hz with %d channel(s) is not valid (1 Hz or more, 1 to 8 channels).", SampleRate, Channels );
		return nullptr;
	}
	const int32 Handle = Backend.CreatePcmStream( SampleRate, Channels );
	if ( Handle < 0 )
	{
		LOG( LogAudio, Error, "The audio backend cannot create a PCM stream of %d Hz with %d channel(s).", SampleRate, Channels );
		return nullptr;
	}
	return TSharedPtr<CPcmStream>( new CPcmStream( Backend, Handle, SampleRate, Channels ) );
}

CPcmStream::CPcmStream( IPcmStreamBackend& InBackend, int32 InHandle, int32 InSampleRate, int32 InChannels )
	: Backend( &InBackend )
	, Handle( InHandle )
	, SampleRate( InSampleRate )
	, Channels( InChannels )
{
}

CPcmStream::~CPcmStream()
{
	if ( Backend == nullptr )
	{
		return;
	}
	if ( !IsInGameThread() )
	{
		NGameThread::ReportWrongThread( __FUNCTION__ );   // バックエンドを触れないので、ここでは解放できない（終了時にバックエンドが片付ける）
		return;
	}
	Backend->DeletePcmStream( Handle );
}

bool CPcmStream::IsValid() const
{
	return Backend != nullptr;
}

bool CPcmStream::Submit( const float32* Samples, int32 FrameCount )
{
	GAME_THREAD_ONLY( false );
	if ( Backend == nullptr || Samples == nullptr || FrameCount <= 0 )
	{
		return false;
	}
	return Backend->QueuePcmStream( Handle, Samples, FrameCount );
}

bool CPcmStream::Submit( const FPcmBuffer& Pcm )
{
	if ( Pcm.SampleRate != SampleRate || Pcm.Channels != Channels )
	{
		LOG( LogAudio, Error, "Cannot submit the PCM: the stream is %d Hz with %d channel(s) but the PCM is %d Hz with %d channel(s).", SampleRate, Channels, Pcm.SampleRate, Pcm.Channels );
		return false;
	}
	return Pcm.IsValid() && Submit( Pcm.Samples.GetData(), static_cast<int32>( Pcm.GetFrameCount() ) );
}

bool CPcmStream::Start()
{
	GAME_THREAD_ONLY( false );
	return Backend != nullptr && Backend->StartPcmStream( Handle );
}

void CPcmStream::Stop()
{
	GAME_THREAD_ONLY();
	if ( Backend != nullptr )
	{
		Backend->StopPcmStream( Handle );
	}
}

void CPcmStream::Reset()
{
	GAME_THREAD_ONLY();
	if ( Backend != nullptr )
	{
		Backend->ResetPcmStream( Handle );
	}
}

bool CPcmStream::IsPlaying() const
{
	GAME_THREAD_ONLY( false );
	return Backend != nullptr && Backend->IsPcmStreamPlaying( Handle );
}

int32 CPcmStream::GetQueuedFrames() const
{
	GAME_THREAD_ONLY( 0 );
	return Backend != nullptr ? Backend->GetPcmStreamQueuedFrames( Handle ) : 0;
}

float32 CPcmStream::GetQueuedSeconds() const
{
	return static_cast<float32>( GetQueuedFrames() ) / static_cast<float32>( SampleRate );
}

void CPcmStream::Detach()
{
	if ( Backend != nullptr && IsInGameThread() )
	{
		Backend->DeletePcmStream( Handle );
	}
	Backend = nullptr;
}
