#include "Sound.h"

CSound::CSound( const FString& InPath, int32 InHandle, ESoundLoadMode InMode, bool bInAsync )
	: CAsset( EAssetType::Sound, InPath )
	, Handle( InHandle )
	, Mode( InMode )
	, bAsync( bInAsync )
{
}

CSound::~CSound()
{
	for ( const int32 Voice : IdleVoices ) NAssetService::ReleaseSound( Voice );
	NAssetService::ReleaseSound( Handle );
}

float32 CSound::GetLength() const
{
	if ( Length > 0.0f ) return Length;
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || Handle < 0 ) return 0.0f;
	if ( bAsync && Backend->IsLoading( Handle ) ) return 0.0f;

	const float32 Seconds = Backend->GetSoundLength( Handle );
	if ( Seconds > 0.0f ) Length = Seconds;
	return Length;
}

bool CSound::IsReady() const
{
	if ( Handle < 0 ) return false;
	if ( !bAsync ) return true;
	IAssetBackend* Backend = NAssetService::GetBackend();
	return Backend != nullptr && !Backend->IsLoading( Handle );
}

int64 CSound::GetMemoryBytes() const
{
	if ( MemoryBytes >= 0 ) return MemoryBytes;
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || !IsReady() ) return 0;
	MemoryBytes = Backend->EstimateSoundBytes( Handle, Mode );
	return MemoryBytes;
}

FString CSound::Describe() const
{
	if ( !IsReady() ) return "loading...";
	return FString::Printf( "%.2f s, %s", GetLength(), IsStream() ? "stream" : "memory" );
}

int32 CSound::AcquireVoice()
{
	if ( Handle < 0 ) return NAsset::InvalidHandle;

	if ( IsStream() )
	{
		// 再生しながら読む音は、複製できない。元のハンドルを 1 人だけに貸す
		if ( bStreamBusy ) return NAsset::InvalidHandle;
		bStreamBusy = true;
		++NumVoicesInUse;
		return Handle;
	}

	int32 Voice = NAsset::InvalidHandle;
	if ( IdleVoices.Num() > 0 )
	{
		Voice = IdleVoices.Pop();
	}
	else if ( IAssetBackend* Backend = NAssetService::GetBackend() )
	{
		Voice = Backend->DuplicateSound( Handle );
	}
	if ( Voice >= 0 ) ++NumVoicesInUse;
	return Voice;
}

void CSound::ReleaseVoice( int32 Voice )
{
	if ( Voice < 0 ) return;
	if ( NumVoicesInUse > 0 ) --NumVoicesInUse;

	if ( IsStream() )
	{
		bStreamBusy = false;
		return;
	}

	if ( IdleVoices.Num() < MaxIdleVoices ) IdleVoices.Add( Voice );
	else NAssetService::ReleaseSound( Voice );
}
