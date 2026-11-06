// SPDX-License-Identifier: MIT
#include "PcmSoundLoader.h"

#include "Library/Core/File/File.h"
#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Framework/Asset/Sound/WavFile.h"

DEFINE_LOG_CATEGORY_STATIC( LogSoundLoader, Log )

CPcmSoundLoader::CPcmSoundLoader( IAssetBackend* InBackend )
	: Backend( InBackend )
{
}

TSharedPtr<CSound> CPcmSoundLoader::CreateFromPcm( const FString& Name, const FPcmBuffer& Pcm ) const
{
	GAME_THREAD_ONLY( nullptr );
	if ( Backend == nullptr )
	{
		LOG( LogSoundLoader, Error, "Cannot create the sound '%s' from PCM: no asset backend.", *Name );
		return nullptr;
	}
	if ( !Pcm.IsValid() )
	{
		LOG( LogSoundLoader, Error, "Cannot create the sound '%s' from PCM: the data is empty or malformed (%d channel(s), %d Hz, %d sample(s)).", *Name, Pcm.Channels, Pcm.SampleRate, Pcm.Samples.Num() );
		return nullptr;
	}
	const int32 Handle = Backend->CreateSoundFromPcm( Pcm );
	if ( Handle < 0 )
	{
		LOG( LogSoundLoader, Error, "The backend could not create the sound '%s' from PCM (%d channel(s), %d Hz).", *Name, Pcm.Channels, Pcm.SampleRate );
		return nullptr;
	}
	LOG( LogSoundLoader, Verbose, "Created the sound '%s' from PCM (%.2f s, %d channel(s), %d Hz)", *Name, Pcm.GetLengthSeconds(), Pcm.Channels, Pcm.SampleRate );
	return MakeShared<CSound>( "pcm:" + Name, Handle, ESoundLoadMode::Memory, false );
}

bool CPcmSoundLoader::Decode( const FString& FullPath, FPcmBuffer& OutPcm ) const
{
	GAME_THREAD_ONLY( false );
	if ( FullPath.EndsWith( ".wav", ESearchCase::IgnoreCase ) )
	{
		TArray<uint8> Bytes;
		if ( NFile::ReadAllBytes( FullPath, Bytes ) && NWavFile::Decode( Bytes.GetData(), static_cast<usize>( Bytes.Num() ), OutPcm ) )
		{
			return true;
		}
	}
	if ( Backend != nullptr && Backend->DecodeSoundFile( FullPath, OutPcm ) )
	{
		return true;
	}
	LOG( LogSoundLoader, Error, "Failed to decode the sound '%s' (unsupported or broken file).", *FullPath );
	return false;
}

bool CPcmSoundLoader::SaveWav( const FPcmBuffer& Pcm, const FString& FullPath )
{
	TArray<uint8> Bytes;
	if ( FullPath.IsEmpty() || !NWavFile::Encode( Pcm, Bytes ) || !NFile::WriteAllBytes( FullPath, Bytes.GetData(), static_cast<usize>( Bytes.Num() ) ) )
	{
		LOG( LogSoundLoader, Error, "Failed to save the WAV file '%s'.", *FullPath );
		return false;
	}
	return true;
}
