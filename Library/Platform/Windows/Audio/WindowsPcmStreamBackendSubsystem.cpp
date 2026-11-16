// SPDX-License-Identifier: MIT
#include "WindowsPcmStreamBackendSubsystem.h"
#include "WindowsAudioDeviceSubsystem.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/System/WindowsInclude.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"

#include <xaudio2.h>

REGISTER_SUBSYSTEM( CWindowsPcmStreamBackendSubsystem )

bool CWindowsPcmStreamBackendSubsystem::ShouldCreateSubsystem( CEngine& ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DX12;
}

void CWindowsPcmStreamBackendSubsystem::Initialize( FSubsystemCollection& Collection )
{
	Bind( Collection.InitializeDependency<CWindowsAudioDeviceSubsystem>() );
}

void CWindowsPcmStreamBackendSubsystem::Bind( CWindowsAudioDeviceSubsystem* InDevice )
{
	Device = InDevice;
}

void CWindowsPcmStreamBackendSubsystem::Deinitialize()
{
	for ( auto& Pair : Streams )
	{
		DestroySource( Pair.Value );
	}
	Streams.Reset();
	Device = nullptr;
}

bool CWindowsPcmStreamBackendSubsystem::CreateSource( FStream& Stream )
{
	if ( Device == nullptr || !Device->IsAvailable() )
	{
		return false;
	}
	WAVEFORMATEX Format = {};
	Format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
	Format.nChannels = static_cast<WORD>( Stream.Channels );
	Format.nSamplesPerSec = static_cast<DWORD>( Stream.SampleRate );
	Format.wBitsPerSample = 32;
	Format.nBlockAlign = static_cast<WORD>( Format.nChannels * sizeof( float32 ) );
	Format.nAvgBytesPerSec = Format.nSamplesPerSec * Format.nBlockAlign;
	return SUCCEEDED( Device->GetEngine()->CreateSourceVoice( &Stream.Source, &Format, XAUDIO2_VOICE_NOPITCH ) ) && Stream.Source != nullptr;
}

void CWindowsPcmStreamBackendSubsystem::DestroySource( FStream& Stream )
{
	if ( Stream.Source != nullptr )
	{
		Stream.Source->Stop();
		Stream.Source->DestroyVoice();
		Stream.Source = nullptr;
	}
}

void CWindowsPcmStreamBackendSubsystem::Collect( FStream& Stream )
{
	if ( Stream.Source == nullptr )
	{
		return;
	}
	XAUDIO2_VOICE_STATE Now = {};
	Stream.Source->GetState( &Now );
	if ( Now.SamplesPlayed < Stream.SamplesBase )
	{
		Stream.SamplesBase = 0; // 再生済みの数が、巻き戻った（キューが空になって数え直された）
	}
	// 積んだ数から、まだ残っている数を引いたぶんが、鳴らし終わった波形
	while ( static_cast<UINT32>( Stream.Buffers.Num() ) > Now.BuffersQueued )
	{
		Stream.RemovedFrames += Stream.BufferFrames[0];
		Stream.Buffers.RemoveAt( 0 );
		Stream.BufferFrames.RemoveAt( 0 );
	}
	if ( Stream.Buffers.IsEmpty() )
	{
		Stream.SamplesBase = Now.SamplesPlayed;
		Stream.RemovedFrames = 0;
		Stream.QueuedFrames = 0;
		return;
	}
	int64 Total = 0;
	for ( int32 Frames : Stream.BufferFrames )
	{
		Total += Frames;
	}
	// 先頭のバッファーの、鳴らし終わったぶん
	const int64 Played = static_cast<int64>( Now.SamplesPlayed - Stream.SamplesBase ) - Stream.RemovedFrames;
	const int64 InFirst = Played < 0 ? 0 : ( Played > Stream.BufferFrames[0] ? Stream.BufferFrames[0] : Played );
	Stream.QueuedFrames = Total - InFirst;
}

int32 CWindowsPcmStreamBackendSubsystem::CreatePcmStream( int32 SampleRate, int32 Channels )
{
	if ( SampleRate < 1000 || SampleRate > 192000 || Channels < 1 || Channels > 2 )
	{
		return -1;
	}
	FStream Stream;
	Stream.SampleRate = SampleRate;
	Stream.Channels = Channels;
	if ( !CreateSource( Stream ) )
	{
		return -1;
	}
	const int32 Handle = NextHandle++;
	Streams.Add( Handle, MoveTemp( Stream ) );
	return Handle;
}

void CWindowsPcmStreamBackendSubsystem::DeletePcmStream( int32 Stream )
{
	if ( FStream* Found = Streams.Find( Stream ) )
	{
		DestroySource( *Found );
		Streams.Remove( Stream );
	}
}

bool CWindowsPcmStreamBackendSubsystem::QueuePcmStream( int32 Stream, const float32* Samples, int32 FrameCount )
{
	FStream* Found = Streams.Find( Stream );
	if ( Found == nullptr || Found->Source == nullptr || Samples == nullptr || FrameCount <= 0 || FrameCount > ( 1 << 24 ) )
	{
		return false;
	}
	Collect( *Found );
	TArray<float32> Copy;
	const int32 Count = FrameCount * Found->Channels;
	Copy.SetNum( Count, 0.0f );
	for ( int32 Index = 0; Index < Count; ++Index )
	{
		const float32 Value = Samples[Index];
		Copy[Index] = Value > 1.0f ? 1.0f : ( Value < -1.0f ? -1.0f : ( Value == Value ? Value : 0.0f ) ); // 範囲を超えた分は切り、数でない値は無音
	}
	Found->Buffers.Add( MoveTemp( Copy ) );
	Found->BufferFrames.Add( FrameCount );
	TArray<float32>& Stored = Found->Buffers[Found->Buffers.Num() - 1];
	XAUDIO2_BUFFER Buffer = {};
	Buffer.AudioBytes = static_cast<UINT32>( Count * sizeof( float32 ) );
	Buffer.pAudioData = reinterpret_cast<const BYTE*>( Stored.GetData() );
	if ( FAILED( Found->Source->SubmitSourceBuffer( &Buffer ) ) )
	{
		Found->Buffers.RemoveAt( Found->Buffers.Num() - 1 );
		Found->BufferFrames.RemoveAt( Found->BufferFrames.Num() - 1 );
		return false;
	}
	Collect( *Found );
	return true;
}

int32 CWindowsPcmStreamBackendSubsystem::GetPcmStreamQueuedFrames( int32 Stream )
{
	FStream* Found = Streams.Find( Stream );
	if ( Found == nullptr || Found->Source == nullptr )
	{
		return 0;
	}
	Collect( *Found );
	return static_cast<int32>( Found->QueuedFrames );
}

bool CWindowsPcmStreamBackendSubsystem::StartPcmStream( int32 Stream )
{
	FStream* Found = Streams.Find( Stream );
	if ( Found == nullptr || Found->Source == nullptr || FAILED( Found->Source->Start() ) )
	{
		return false;
	}
	Found->bPlaying = true;
	return true;
}

void CWindowsPcmStreamBackendSubsystem::StopPcmStream( int32 Stream )
{
	FStream* Found = Streams.Find( Stream );
	if ( Found != nullptr && Found->Source != nullptr )
	{
		Found->Source->Stop();
		Found->bPlaying = false;
	}
}

void CWindowsPcmStreamBackendSubsystem::ResetPcmStream( int32 Stream )
{
	FStream* Found = Streams.Find( Stream );
	if ( Found == nullptr )
	{
		return;
	}
	DestroySource( *Found );
	Found->Buffers.Reset();
	Found->BufferFrames.Reset();
	Found->QueuedFrames = 0;
	Found->SamplesBase = 0;
	Found->RemovedFrames = 0;
	Found->bPlaying = false;
	CreateSource( *Found );
}

bool CWindowsPcmStreamBackendSubsystem::IsPcmStreamPlaying( int32 Stream )
{
	FStream* Found = Streams.Find( Stream );
	return Found != nullptr && Found->Source != nullptr && Found->bPlaying;
}
