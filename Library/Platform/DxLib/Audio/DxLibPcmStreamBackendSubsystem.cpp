// SPDX-License-Identifier: MIT
#include "DxLibPcmStreamBackendSubsystem.h"

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/System/BackendSupport.h"

DEFINE_LOG_CATEGORY_STATIC( LogPcmStreamBackend, Log )

REGISTER_SUBSYSTEM( CDxLibPcmStreamBackendSubsystem )

bool CDxLibPcmStreamBackendSubsystem::ShouldCreateSubsystem( CEngine& Engine ) const
{
	return !NBackendSelection::IsNullRequested();
}

void CDxLibPcmStreamBackendSubsystem::Initialize( FSubsystemCollection& Collection )
{
	// DxLib（プラットフォーム）が初期化されたあとに使う
	if ( Collection.InitializeDependency<IPlatform>() == nullptr )
	{
		LOG( LogPcmStreamBackend, Error, "No platform is available. PCM streams will not be played." );
	}
}

int32 CDxLibPcmStreamBackendSubsystem::CreatePcmStream( int32 SampleRate, int32 Channels )
{
	if ( Channels < 1 || Channels > 2 )
	{
		// DxLib のソフトウェアサウンドのプレイヤーは、1 か 2 チャンネルだけ
		NBackendSupport::ReportUnsupported( "DxLib", "PCM streams with more than 2 channels", "DxLib soft sound players have only 1 or 2 channels" );
		return -1;
	}
	const int32 Handle = MakeSoftSoundPlayerCustom( Channels, 16, SampleRate );
	if ( Handle < 0 )
	{
		return -1;
	}
	SilentFill.Add( Handle, ReadSilentFill( Handle ) );
	return Handle;
}

void CDxLibPcmStreamBackendSubsystem::DeletePcmStream( int32 Stream )
{
	DeleteSoftSoundPlayer( Stream );
	SilentFill.Remove( Stream );
}

bool CDxLibPcmStreamBackendSubsystem::QueuePcmStream( int32 Stream, const float32* Samples, int32 FrameCount )
{
	int Channels = 0;
	int Bits = 0;
	int Rate = 0;
	if ( Samples == nullptr || FrameCount <= 0 || GetSoftSoundPlayerFormat( Stream, &Channels, &Bits, &Rate ) != 0 || Channels < 1 )
	{
		return false;
	}

	// -1..1 の浮動小数を、16 ビットの整数に直す（範囲を超えた分は切る）
	TArray<int16> Converted;
	const int32 NumSamples = FrameCount * Channels;
	Converted.SetNum( NumSamples );
	for ( int32 Index = 0; Index < NumSamples; ++Index )
	{
		Converted[Index] = static_cast<int16>( NMath::RoundToInt( NMath::Clamp( Samples[Index], -1.0f, 1.0f ) * 32767.0f ) );
	}
	return AddDirectDataSoftSoundPlayer( Stream, Converted.GetData(), FrameCount ) == 0;
}

int32 CDxLibPcmStreamBackendSubsystem::GetPcmStreamQueuedFrames( int32 Stream )
{
	// 戻り値は、再生用の領域に移す前の分。引数には、領域に入っている分が返る。
	// 領域が無音で満たされた状態（作った直後・Reset の直後）の量は、足した波形ではないので数えない
	int InBuffer = 0;
	const int Waiting = GetStockDataLengthSoftSoundPlayer( Stream, &InBuffer );
	if ( Waiting < 0 )
	{
		return 0;
	}
	const int32* Silent = SilentFill.Find( Stream );
	const bool bOnlySilence = Silent != nullptr && InBuffer >= *Silent;
	return static_cast<int32>( Waiting ) + ( bOnlySilence ? 0 : static_cast<int32>( InBuffer ) );
}

int32 CDxLibPcmStreamBackendSubsystem::ReadSilentFill( int32 Stream ) const
{
	int InBuffer = 0;
	GetStockDataLengthSoftSoundPlayer( Stream, &InBuffer );
	return InBuffer;
}

bool CDxLibPcmStreamBackendSubsystem::StartPcmStream( int32 Stream )
{
	return StartSoftSoundPlayer( Stream ) == 0;
}

void CDxLibPcmStreamBackendSubsystem::StopPcmStream( int32 Stream )
{
	StopSoftSoundPlayer( Stream );
}

void CDxLibPcmStreamBackendSubsystem::ResetPcmStream( int32 Stream )
{
	ResetSoftSoundPlayer( Stream );
	SilentFill.Add( Stream, ReadSilentFill( Stream ) );
}

bool CDxLibPcmStreamBackendSubsystem::IsPcmStreamPlaying( int32 Stream )
{
	return CheckStartSoftSoundPlayer( Stream ) == TRUE;
}
