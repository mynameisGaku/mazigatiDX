// SPDX-License-Identifier: MIT
#include "WindowsMediaDecoder.h"
#include "Library/Platform/System/WindowsInclude.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#pragma comment( lib, "mfplat.lib" )
#pragma comment( lib, "mfreadwrite.lib" )
#pragma comment( lib, "mfuuid.lib" )

namespace
{
	using Microsoft::WRL::ComPtr;

	// COM と Media Foundation を、このスレッドで使えるようにする。最後に、始めたぶんだけ戻す
	struct FMediaScope
	{
		bool bCom = false;
		bool bMedia = false;
		FMediaScope()
		{
			const HRESULT Com = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
			bCom = SUCCEEDED( Com ); // RPC_E_CHANGED_MODE（別の形で初期化済み）も、そのまま使える
			bMedia = SUCCEEDED( MFStartup( MF_VERSION, MFSTARTUP_LITE ) );
		}
		~FMediaScope()
		{
			if ( bMedia )
			{
				MFShutdown();
			}
			if ( bCom )
			{
				CoUninitialize();
			}
		}
	};
}

bool NWindowsMediaDecoder::Decode( const FString& Path, FPcmBuffer& OutPcm )
{
	FMediaScope Scope;
	if ( !Scope.bMedia || Path.IsEmpty() )
	{
		return false;
	}
	ComPtr<IMFSourceReader> Reader;
	if ( FAILED( MFCreateSourceReaderFromURL( Path.ToWide().GetData(), nullptr, &Reader ) ) )
	{
		return false;
	}
	Reader->SetStreamSelection( MF_SOURCE_READER_ALL_STREAMS, FALSE );
	Reader->SetStreamSelection( MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE );

	// 元の形（チャンネル数）を見て、2 を超えるときは 2 にしてもらう
	UINT32 NativeChannels = 0;
	{
		ComPtr<IMFMediaType> Native;
		if ( SUCCEEDED( Reader->GetNativeMediaType( MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &Native ) ) )
		{
			Native->GetUINT32( MF_MT_AUDIO_NUM_CHANNELS, &NativeChannels );
		}
	}
	ComPtr<IMFMediaType> Request;
	if ( FAILED( MFCreateMediaType( &Request ) ) )
	{
		return false;
	}
	Request->SetGUID( MF_MT_MAJOR_TYPE, MFMediaType_Audio );
	Request->SetGUID( MF_MT_SUBTYPE, MFAudioFormat_Float );
	Request->SetUINT32( MF_MT_AUDIO_BITS_PER_SAMPLE, 32 );
	if ( NativeChannels > 2 )
	{
		Request->SetUINT32( MF_MT_AUDIO_NUM_CHANNELS, 2 );
	}
	if ( FAILED( Reader->SetCurrentMediaType( MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, Request.Get() ) ) )
	{
		return false;
	}
	ComPtr<IMFMediaType> Actual;
	if ( FAILED( Reader->GetCurrentMediaType( MF_SOURCE_READER_FIRST_AUDIO_STREAM, &Actual ) ) )
	{
		return false;
	}
	UINT32 Channels = 0;
	UINT32 SampleRate = 0;
	if ( FAILED( Actual->GetUINT32( MF_MT_AUDIO_NUM_CHANNELS, &Channels ) ) || FAILED( Actual->GetUINT32( MF_MT_AUDIO_SAMPLES_PER_SECOND, &SampleRate ) ) || Channels < 1 || Channels > 2 || SampleRate == 0 )
	{
		return false;
	}

	FPcmBuffer Pcm;
	Pcm.SampleRate = static_cast<int32>( SampleRate );
	Pcm.Channels = static_cast<int32>( Channels );
	constexpr int64 MaxSamples = 1ll << 28; // 約 1GB。壊れたファイルで、際限なく読まない
	for ( ;; )
	{
		DWORD Flags = 0;
		ComPtr<IMFSample> Sample;
		if ( FAILED( Reader->ReadSample( MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &Flags, nullptr, &Sample ) ) )
		{
			return false;
		}
		if ( Flags & ( MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR ) )
		{
			break;
		}
		if ( !Sample )
		{
			continue;
		}
		ComPtr<IMFMediaBuffer> Buffer;
		if ( FAILED( Sample->ConvertToContiguousBuffer( &Buffer ) ) )
		{
			return false;
		}
		BYTE* Data = nullptr;
		DWORD Length = 0;
		if ( FAILED( Buffer->Lock( &Data, nullptr, &Length ) ) )
		{
			return false;
		}
		const int32 Count = static_cast<int32>( Length / sizeof( float32 ) );
		if ( static_cast<int64>( Pcm.Samples.Num() ) + Count > MaxSamples )
		{
			Buffer->Unlock();
			return false;
		}
		const float32* Floats = reinterpret_cast<const float32*>( Data );
		for ( int32 Index = 0; Index < Count; ++Index )
		{
			Pcm.Samples.Add( Floats[Index] );
		}
		Buffer->Unlock();
	}
	if ( !Pcm.IsValid() )
	{
		return false;
	}
	OutPcm = MoveTemp( Pcm );
	return true;
}

TArray<FString> NWindowsMediaDecoder::GetReadableExtensions()
{
	TArray<FString> Result;
	for ( const char* Extension : { ".mp3", ".m4a", ".aac", ".wma", ".flac", ".mp4" } )
	{
		Result.Add( FString( Extension ) );
	}
	return Result;
}
