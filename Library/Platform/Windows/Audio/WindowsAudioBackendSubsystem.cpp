// SPDX-License-Identifier: MIT
#include "WindowsAudioBackendSubsystem.h"
#include "WindowsAudioDeviceSubsystem.h"
#include "WindowsSoundStoreSubsystem.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/System/WindowsInclude.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"

#include <xaudio2.h>

REGISTER_SUBSYSTEM( CWindowsAudioBackendSubsystem )

namespace
{
	constexpr float32 MaxPitch = 16.0f;
	constexpr float32 MinPitch = 1.0f / 1024.0f;
}

bool CWindowsAudioBackendSubsystem::ShouldCreateSubsystem( CEngine& ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DX12;
}

void CWindowsAudioBackendSubsystem::Initialize( FSubsystemCollection& Collection )
{
	Bind( Collection.InitializeDependency<CWindowsAudioDeviceSubsystem>(), Collection.InitializeDependency<CWindowsSoundStoreSubsystem>() );
}

void CWindowsAudioBackendSubsystem::Bind( CWindowsAudioDeviceSubsystem* InDevice, CWindowsSoundStoreSubsystem* InSounds )
{
	Device = InDevice;
	Sounds = InSounds;
}

void CWindowsAudioBackendSubsystem::Deinitialize()
{
	for ( auto& Pair : Voices )
	{
		DestroySource( Pair.Value );
	}
	Voices.Reset();
	Device = nullptr;
	Sounds = nullptr;
}

void CWindowsAudioBackendSubsystem::Tick( float32 )
{
	if ( Sounds == nullptr )
	{
		return;
	}
	TArray<int32> Dead;
	for ( auto& Pair : Voices )
	{
		if ( !Sounds->Find( Pair.Key ).IsValid() )
		{
			Dead.Add( Pair.Key );
		}
	}
	for ( int32 Key : Dead )
	{
		if ( FVoice* State = Voices.Find( Key ) )
		{
			DestroySource( *State );
		}
		Voices.Remove( Key );
	}
}

void CWindowsAudioBackendSubsystem::DestroySource( FVoice& State )
{
	if ( State.Source != nullptr )
	{
		State.Source->Stop();
		State.Source->DestroyVoice();
		State.Source = nullptr;
	}
	State.bPlaying = false;
}

CWindowsAudioBackendSubsystem::FVoice* CWindowsAudioBackendSubsystem::FindOrCreate( int32 Voice )
{
	if ( Voice < 0 || Sounds == nullptr || !Sounds->Find( Voice ).IsValid() )
	{
		return nullptr;
	}
	return &Voices.FindOrAdd( Voice );
}

void CWindowsAudioBackendSubsystem::ApplyMix( const FVoice& State ) const
{
	if ( State.Source == nullptr || Device == nullptr || !State.Data.IsValid() )
	{
		return;
	}
	State.Source->SetVolume( NMath::Max( 0.0f, State.Volume ) );
	State.Source->SetFrequencyRatio( NMath::Clamp( State.Pitch, MinPitch, MaxPitch ) );
	// パン: 反対側の音を弱める（-1 = 左だけ、1 = 右だけ）
	const int32 Out = Device->GetOutputChannels();
	const int32 In = State.Data->Channels;
	if ( Out < 1 )
	{
		return;
	}
	const float32 Pan = NMath::Clamp( State.Pan, -1.0f, 1.0f );
	const float32 Left = Pan > 0.0f ? 1.0f - Pan : 1.0f;
	const float32 Right = Pan < 0.0f ? 1.0f + Pan : 1.0f;
	TArray<float32> Matrix; // 出力ごとに、入力の数だけ並ぶ（Matrix[出力 * 入力の数 + 入力]）
	Matrix.SetNum( In * Out, 0.0f );
	if ( Out == 1 )
	{
		for ( int32 Channel = 0; Channel < In; ++Channel )
		{
			Matrix[Channel] = 1.0f / static_cast<float32>( In );
		}
	}
	else if ( In == 1 )
	{
		Matrix[0 * In + 0] = Left;
		Matrix[1 * In + 0] = Right;
	}
	else
	{
		Matrix[0 * In + 0] = Left;
		Matrix[1 * In + 1] = Right;
	}
	State.Source->SetOutputMatrix( nullptr, static_cast<UINT32>( In ), static_cast<UINT32>( Out ), Matrix.GetData() );
}

int64 CWindowsAudioBackendSubsystem::ComputePosition( const FVoice& State ) const
{
	if ( !State.Data.IsValid() )
	{
		return 0;
	}
	const int64 Length = State.Data->GetFrameCount();
	if ( !State.bPlaying || State.Source == nullptr )
	{
		return NMath::Clamp<int64>( State.PositionFrames, 0, Length );
	}
	XAUDIO2_VOICE_STATE Now = {};
	State.Source->GetState( &Now );
	const uint64 Base = Now.SamplesPlayed >= State.BaseSamplesPlayed ? State.BaseSamplesPlayed : 0; // 数え直された（巻き戻った）ときは、0 から
	const int64 Played = static_cast<int64>( Now.SamplesPlayed - Base );
	const int64 End = State.bLoopRegionSet && State.LoopEnd > State.LoopStart ? NMath::Min<int64>( State.LoopEnd, Length ) : Length;
	const int64 Begin = State.bLoopRegionSet && State.LoopEnd > State.LoopStart ? State.LoopStart : 0;
	const int64 FirstSegment = State.StartFrames < End ? End - State.StartFrames : 0;
	if ( Played < FirstSegment || !State.bLoop )
	{
		return NMath::Min<int64>( State.StartFrames + Played, Length );
	}
	const int64 Span = End - Begin;
	return Span > 0 ? Begin + ( Played - FirstSegment ) % Span : Begin;
}

bool CWindowsAudioBackendSubsystem::Submit( FVoice& State, int64 StartFrame )
{
	if ( State.Source == nullptr || !State.Data.IsValid() )
	{
		return false;
	}
	const int64 Length = State.Data->GetFrameCount();
	const bool bRegion = State.bLoopRegionSet && State.LoopEnd > State.LoopStart && State.LoopStart < Length;
	const int64 LoopBegin = bRegion ? State.LoopStart : 0;
	const int64 LoopEnd = bRegion ? NMath::Min<int64>( State.LoopEnd, Length ) : Length;
	StartFrame = NMath::Clamp<int64>( StartFrame, 0, Length );
	State.Source->Stop();
	State.Source->FlushSourceBuffers();
	XAUDIO2_VOICE_STATE Now = {};
	State.Source->GetState( &Now );
	State.BaseSamplesPlayed = Now.SamplesPlayed;
	State.StartFrames = StartFrame;
	XAUDIO2_BUFFER Buffer = {};
	Buffer.AudioBytes = static_cast<UINT32>( State.Data->Samples.Num() * sizeof( float32 ) );
	Buffer.pAudioData = reinterpret_cast<const BYTE*>( State.Data->Samples.GetData() );
	// 最初の区間（今の位置から、ループの終わり（ループしないときは音の最後）まで）。位置がもう終わりなら、ループの頭から
	const int64 End = State.bLoop ? LoopEnd : Length;
	if ( StartFrame < End )
	{
		XAUDIO2_BUFFER First = Buffer;
		First.PlayBegin = static_cast<UINT32>( StartFrame );
		First.PlayLength = static_cast<UINT32>( End - StartFrame );
		First.Flags = State.bLoop ? 0 : XAUDIO2_END_OF_STREAM;
		if ( FAILED( State.Source->SubmitSourceBuffer( &First ) ) )
		{
			return false;
		}
	}
	else if ( !State.bLoop )
	{
		return false; // 最後まで鳴り終わっている
	}
	if ( State.bLoop )
	{
		XAUDIO2_BUFFER Loop = Buffer;
		Loop.PlayBegin = static_cast<UINT32>( LoopBegin );
		Loop.PlayLength = static_cast<UINT32>( LoopEnd - LoopBegin );
		Loop.LoopBegin = static_cast<UINT32>( LoopBegin );
		Loop.LoopLength = static_cast<UINT32>( LoopEnd - LoopBegin );
		Loop.LoopCount = XAUDIO2_LOOP_INFINITE;
		if ( StartFrame >= End )
		{
			State.StartFrames = LoopBegin; // 頭から（位置の計算の、最初の区間は空）
		}
		if ( FAILED( State.Source->SubmitSourceBuffer( &Loop ) ) )
		{
			return false;
		}
	}
	ApplyMix( State );
	return SUCCEEDED( State.Source->Start() );
}

bool CWindowsAudioBackendSubsystem::Play( int32 Voice, bool bLoop, bool bFromStart )
{
	FVoice* State = FindOrCreate( Voice );
	if ( State == nullptr || Device == nullptr || !Device->IsAvailable() )
	{
		return false;
	}
	const TSharedPtr<const FPcmBuffer> Data = Sounds->Find( Voice );
	if ( State->Source == nullptr || State->Data != Data )
	{
		DestroySource( *State );
		State->Data = Data;
		WAVEFORMATEX Format = {};
		Format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
		Format.nChannels = static_cast<WORD>( Data->Channels );
		Format.nSamplesPerSec = static_cast<DWORD>( Data->SampleRate );
		Format.wBitsPerSample = 32;
		Format.nBlockAlign = static_cast<WORD>( Format.nChannels * sizeof( float32 ) );
		Format.nAvgBytesPerSec = Format.nSamplesPerSec * Format.nBlockAlign;
		if ( FAILED( Device->GetEngine()->CreateSourceVoice( &State->Source, &Format, 0, MaxPitch ) ) || State->Source == nullptr )
		{
			State->Source = nullptr;
			return false;
		}
	}
	const int64 Current = ComputePosition( *State );
	State->bLoop = bLoop;
	// 止めた位置から再開する。最後まで鳴り終わっていた音は、頭から
	const int64 Start = bFromStart || Current >= Data->GetFrameCount() ? 0 : Current;
	State->bPlaying = true; // 位置の計算が、鳴らしている扱いになる前に、積む
	if ( !Submit( *State, Start ) )
	{
		State->bPlaying = false;
		State->PositionFrames = 0;
		return false;
	}
	return true;
}

void CWindowsAudioBackendSubsystem::Stop( int32 Voice )
{
	FVoice* State = Voices.Find( Voice );
	if ( State == nullptr || State->Source == nullptr )
	{
		return;
	}
	if ( State->bPlaying )
	{
		State->PositionFrames = ComputePosition( *State );
	}
	State->bPlaying = false;
	State->Source->Stop();
	State->Source->FlushSourceBuffers();
}

bool CWindowsAudioBackendSubsystem::IsPlaying( int32 Voice )
{
	FVoice* State = Voices.Find( Voice );
	if ( State == nullptr || State->Source == nullptr || !State->bPlaying )
	{
		return false;
	}
	XAUDIO2_VOICE_STATE Now = {};
	State->Source->GetState( &Now, XAUDIO2_VOICE_NOSAMPLESPLAYED );
	if ( Now.BuffersQueued == 0 )
	{
		State->bPlaying = false; // ループしない音が、最後まで鳴った
		State->PositionFrames = 0;
		return false;
	}
	return true;
}

void CWindowsAudioBackendSubsystem::SetVolume( int32 Voice, float32 Volume )
{
	if ( FVoice* State = FindOrCreate( Voice ) )
	{
		State->Volume = Volume;
		ApplyMix( *State );
	}
}

void CWindowsAudioBackendSubsystem::SetPan( int32 Voice, float32 Pan )
{
	if ( FVoice* State = FindOrCreate( Voice ) )
	{
		State->Pan = Pan;
		ApplyMix( *State );
	}
}

void CWindowsAudioBackendSubsystem::SetPitch( int32 Voice, float32 Pitch )
{
	if ( FVoice* State = FindOrCreate( Voice ) )
	{
		State->Pitch = Pitch;
		ApplyMix( *State );
	}
}

float32 CWindowsAudioBackendSubsystem::GetPlayPosition( int32 Voice )
{
	FVoice* State = Voices.Find( Voice );
	if ( State == nullptr || !State->Data.IsValid() )
	{
		return 0.0f;
	}
	IsPlaying( Voice ); // 鳴り終わっていれば、位置を 0 に戻す
	return static_cast<float32>( ComputePosition( *State ) ) / static_cast<float32>( State->Data->SampleRate );
}

void CWindowsAudioBackendSubsystem::SetPlayPosition( int32 Voice, float32 Seconds )
{
	FVoice* State = FindOrCreate( Voice );
	if ( State == nullptr )
	{
		return;
	}
	const TSharedPtr<const FPcmBuffer> Data = Sounds->Find( Voice );
	const int64 Frame = NMath::Clamp<int64>( static_cast<int64>( NMath::Max( 0.0f, Seconds ) * static_cast<float32>( Data->SampleRate ) ), 0, Data->GetFrameCount() );
	State->PositionFrames = Frame;
	if ( State->bPlaying && State->Source != nullptr )
	{
		Submit( *State, Frame ); // 鳴らしている間も、その位置から鳴らし直す
	}
}

bool CWindowsAudioBackendSubsystem::SetLoopRegion( int32 Voice, float32 StartSeconds, float32 EndSeconds )
{
	FVoice* State = FindOrCreate( Voice );
	if ( State == nullptr )
	{
		return false;
	}
	const TSharedPtr<const FPcmBuffer> Data = Sounds->Find( Voice );
	const float32 Rate = static_cast<float32>( Data->SampleRate );
	State->LoopStart = static_cast<int64>( NMath::Max( 0.0f, StartSeconds ) * Rate );
	State->LoopEnd = EndSeconds > 0.0f ? static_cast<int64>( EndSeconds * Rate ) : 0;
	State->bLoopRegionSet = true;
	if ( State->bPlaying && State->bLoop && State->Source != nullptr )
	{
		Submit( *State, ComputePosition( *State ) );
	}
	return true;
}
