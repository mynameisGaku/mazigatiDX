// SPDX-License-Identifier: MIT
#include "WindowsSoundStoreSubsystem.h"
#include "WindowsMediaDecoder.h"
#include "Library/Core/File/File.h"
#include "Library/Framework/Asset/Sound/WavFile.h"
#include "Library/Core/Interfaces/Asset/AssetBackendTypes.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"

REGISTER_SUBSYSTEM( CWindowsSoundStoreSubsystem )

namespace
{
	// 2 チャンネルを超える音を、ステレオにする（偶数番のチャンネルを左、奇数番を右へ、平均する）
	void DownmixToStereo( FPcmBuffer& Pcm )
	{
		if ( Pcm.Channels <= 2 )
		{
			return;
		}
		const int64 Frames = Pcm.GetFrameCount();
		TArray<float32> Stereo;
		Stereo.SetNum( static_cast<int32>( Frames * 2 ), 0.0f );
		for ( int64 Frame = 0; Frame < Frames; ++Frame )
		{
			float32 Sum[2] = { 0.0f, 0.0f };
			int32 Count[2] = { 0, 0 };
			for ( int32 Channel = 0; Channel < Pcm.Channels; ++Channel )
			{
				Sum[Channel & 1] += Pcm.Samples[static_cast<int32>( Frame * Pcm.Channels + Channel )];
				++Count[Channel & 1];
			}
			Stereo[static_cast<int32>( Frame * 2 )] = Sum[0] / static_cast<float32>( Count[0] );
			Stereo[static_cast<int32>( Frame * 2 + 1 )] = Count[1] > 0 ? Sum[1] / static_cast<float32>( Count[1] ) : Sum[0] / static_cast<float32>( Count[0] );
		}
		Pcm.Samples = MoveTemp( Stereo );
		Pcm.Channels = 2;
	}

	// ファイルを、生の音にする。WAV はフレームワーク、それ以外は OS のデコーダー
	bool ReadFile( const FString& Path, FPcmBuffer& Out )
	{
		FString Extension = FString( Path ).ToLower();
		if ( Extension.EndsWith( ".wav" ) )
		{
			TArray<uint8> Bytes;
			return NFile::ReadAllBytes( Path, Bytes ) && NWavFile::Decode( Bytes.GetData(), Bytes.Num(), Out );
		}
		return NWindowsMediaDecoder::Decode( Path, Out );
	}
}

bool CWindowsSoundStoreSubsystem::ShouldCreateSubsystem( CEngine& ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DX12;
}

void CWindowsSoundStoreSubsystem::Deinitialize()
{
	Sounds.Reset();
}

int32 CWindowsSoundStoreSubsystem::Add( const TSharedPtr<const FPcmBuffer>& Data )
{
	const int32 Handle = NextHandle++;
	Sounds.Add( Handle, Data );
	return Handle;
}

int32 CWindowsSoundStoreSubsystem::LoadSound( const FString& Path, ESoundLoadMode, bool )
{
	FPcmBuffer Pcm;
	if ( !ReadFile( Path, Pcm ) )
	{
		return NAsset::InvalidHandle;
	}
	DownmixToStereo( Pcm );
	return Pcm.IsValid() ? Add( MakeShared<const FPcmBuffer>( MoveTemp( Pcm ) ) ) : NAsset::InvalidHandle;
}

int32 CWindowsSoundStoreSubsystem::DuplicateSound( int32 Source )
{
	const TSharedPtr<const FPcmBuffer> Data = Find( Source );
	return Data.IsValid() ? Add( Data ) : NAsset::InvalidHandle;
}

void CWindowsSoundStoreSubsystem::DeleteSound( int32 Sound )
{
	Sounds.Remove( Sound );
}

float32 CWindowsSoundStoreSubsystem::GetSoundLength( int32 Sound )
{
	const TSharedPtr<const FPcmBuffer> Data = Find( Sound );
	return Data.IsValid() ? Data->GetLengthSeconds() : -1.0f;
}

bool CWindowsSoundStoreSubsystem::GetSoundInfo( int32 Sound, FSoundInfo& Out )
{
	const TSharedPtr<const FPcmBuffer> Data = Find( Sound );
	if ( !Data.IsValid() )
	{
		return false;
	}
	Out.LengthSeconds = Data->GetLengthSeconds();
	Out.FrameCount = Data->GetFrameCount();
	Out.SampleRate = Data->SampleRate;
	Out.Channels = Data->Channels;
	return true;
}

int32 CWindowsSoundStoreSubsystem::CreateSoundFromPcm( const FPcmBuffer& Pcm )
{
	if ( !Pcm.IsValid() )
	{
		return NAsset::InvalidHandle;
	}
	FPcmBuffer Copy = Pcm;
	DownmixToStereo( Copy );
	return Add( MakeShared<const FPcmBuffer>( MoveTemp( Copy ) ) );
}

bool CWindowsSoundStoreSubsystem::DecodeSoundFile( const FString& Path, FPcmBuffer& Out )
{
	FPcmBuffer Pcm;
	if ( !ReadFile( Path, Pcm ) || !Pcm.IsValid() )
	{
		return false;
	}
	Out = MoveTemp( Pcm );
	return true;
}

TArray<FString> CWindowsSoundStoreSubsystem::GetReadableSoundExtensions() const
{
	return NWindowsMediaDecoder::GetReadableExtensions();
}

TSharedPtr<const FPcmBuffer> CWindowsSoundStoreSubsystem::Find( int32 Sound ) const
{
	const TSharedPtr<const FPcmBuffer>* Found = Sounds.Find( Sound );
	return Found != nullptr ? *Found : TSharedPtr<const FPcmBuffer>();
}

int64 CWindowsSoundStoreSubsystem::EstimateBytes( int32 Sound ) const
{
	const TSharedPtr<const FPcmBuffer> Data = Find( Sound );
	return Data.IsValid() ? static_cast<int64>( Data->Samples.Num() ) * static_cast<int64>( sizeof( float32 ) ) : 0;
}

int32 CWindowsSoundStoreSubsystem::Num() const
{
	return Sounds.Num();
}
