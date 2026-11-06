// SPDX-License-Identifier: MIT
#include "DxLibAssetBackendSubsystem.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/System/BackendSupport.h"

//=============================================================================
// CDxLibAssetBackendSubsystem のうち、音の情報と PCM（生の音。DxLib のソフトサウンド）
//   DxLib 未対応: 3 チャンネル以上の音は作れない（ソフトサウンドが 1〜2 チャンネルだけ）。音のハンドルからチャンネル数を調べる関数も無い。
//=============================================================================
namespace
{
	// 16 ビット整数に収める（-1..1 を 32767 倍して丸める。数でない値は 0）
	int ToInt16( float32 Sample )
	{
		if ( Sample != Sample )
		{
			return 0;
		}
		return NMath::RoundToInt( NMath::Clamp( Sample, -1.0f, 1.0f ) * 32767.0f );
	}
}

bool CDxLibAssetBackendSubsystem::GetSoundInfo( int32 Sound, FSoundInfo& Out )
{
	if ( Sound < 0 || CheckHandleASyncLoad( Sound ) != FALSE )
	{
		return false;
	}
	const LONGLONG TotalSamples = GetSoundTotalSample( Sound );
	const LONGLONG TotalMilliseconds = GetSoundTotalTime( Sound );
	if ( TotalSamples <= 0 || TotalMilliseconds <= 0 )
	{
		return false;
	}
	FSoundInfo Info;
	Info.FrameCount = static_cast<int64>( TotalSamples );
	Info.LengthSeconds = static_cast<float32>( TotalMilliseconds ) / 1000.0f;
	// 元の周波数。DxLib 未対応: チャンネル数は調べられないので 0（不明）のまま
	const int Frequency = GetFrequencySoundMem( Sound );
	Info.SampleRate = Frequency > 0 ? Frequency : static_cast<int32>( TotalSamples * 1000 / TotalMilliseconds );
	Out = Info;
	return true;
}

int32 CDxLibAssetBackendSubsystem::CreateSoundFromPcm( const FPcmBuffer& Pcm )
{
	if ( !Pcm.IsValid() )
	{
		return -1;
	}
	if ( Pcm.Channels > 2 )
	{
		NBackendSupport::ReportUnsupported( "DxLib", "Sound with more than 2 channels", "DxLib soft sounds have only 1 or 2 channels" );
		return -1;
	}
	const int64 Frames = Pcm.GetFrameCount();
	const int Soft = MakeSoftSoundCustom( Pcm.Channels, 16, Pcm.SampleRate, static_cast<LONGLONG>( Frames ), 0 );
	if ( Soft < 0 )
	{
		return -1;
	}
	for ( int64 Frame = 0; Frame < Frames; ++Frame )
	{
		const int32 Base = static_cast<int32>( Frame ) * Pcm.Channels;
		const int Left = ToInt16( Pcm.Samples[Base] );
		const int Right = Pcm.Channels == 2 ? ToInt16( Pcm.Samples[Base + 1] ) : 0;
		WriteSoftSoundData( Soft, static_cast<LONGLONG>( Frame ), Left, Right );
	}
	const int Sound = LoadSoundMemFromSoftSound( Soft );
	DeleteSoftSound( Soft );
	return Sound;
}

bool CDxLibAssetBackendSubsystem::DecodeSoundFile( const FString& Path, FPcmBuffer& Out )
{
	const int Soft = LoadSoftSound( *Path );
	if ( Soft < 0 )
	{
		return false;
	}
	int Channels = 0;
	int Bits = 0;
	int Rate = 0;
	const LONGLONG Frames = GetSoftSoundSampleNum( Soft );
	bool bOk = GetSoftSoundFormat( Soft, &Channels, &Bits, &Rate, nullptr ) == 0 && Frames > 0 && Rate > 0 && ( Channels == 1 || Channels == 2 );
	FPcmBuffer Result;
	if ( bOk )
	{
		Result.SampleRate = Rate;
		Result.Channels = Channels;
		Result.Samples.Reserve( static_cast<int32>( Frames ) * Channels );
		for ( LONGLONG Frame = 0; Frame < Frames; ++Frame )
		{
			float Left = 0.0f;
			float Right = 0.0f;
			if ( ReadSoftSoundDataF( Soft, Frame, &Left, &Right ) != 0 )
			{
				bOk = false;
				break;
			}
			Result.Samples.Add( Left );
			if ( Channels == 2 )
			{
				Result.Samples.Add( Right );
			}
		}
	}
	DeleteSoftSound( Soft );
	if ( !bOk )
	{
		return false;
	}
	Out = MoveTemp( Result );
	return true;
}
