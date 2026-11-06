// SPDX-License-Identifier: MIT
#include "Movie.h"

#include "Library/Core/Interfaces/Video/MovieBackend.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Thread/GameThread.h"
#include "Library/Framework/Movie/MovieSubsystem.h"
#include "Library/Math/MathUtils.h"

TSharedPtr<CMovie> CMovie::Create( IMovieBackend& Backend, const FString& FullPath )
{
	GAME_THREAD_ONLY( nullptr );
	const int32 Handle = Backend.OpenMovie( FullPath );
	if ( Handle < 0 )
	{
		LOG( LogMovie, Error, "Failed to open the movie '%s' (unsupported format?).", *FullPath );
		return nullptr;
	}
	FMovieInfo Info;
	Backend.GetMovieInfo( Handle, Info );
	return TSharedPtr<CMovie>( new CMovie( Backend, Handle, Backend.GetMovieImage( Handle ), FullPath, Info ) );
}

CMovie::CMovie( IMovieBackend& InBackend, int32 InHandle, int32 InImage, const FString& InPath, const FMovieInfo& InInfo )
	: Backend( &InBackend )
	, Handle( InHandle )
	, Image( InImage )
	, Path( InPath )
	, Info( InInfo )
{
}

CMovie::~CMovie()
{
	if ( Backend == nullptr )
	{
		return;
	}
	if ( !IsInGameThread() )
	{
		NGameThread::ReportWrongThread( __FUNCTION__ );   // バックエンドを触れないので、ここでは閉じられない（終了時にバックエンドが片付ける）
		return;
	}
	Backend->CloseMovie( Handle );
}

bool CMovie::IsValid() const
{
	return Backend != nullptr;
}

FImageHandle CMovie::GetImageHandle() const
{
	// 描画システムのワーカーからも呼ばれる。メンバーを読むだけにする
	return Backend != nullptr ? FImageHandle( Image ) : FImageHandle();
}

bool CMovie::Play( bool bLoop )
{
	GAME_THREAD_ONLY( false );
	return Backend != nullptr && Backend->PlayMovie( Handle, bLoop );
}

void CMovie::Pause()
{
	GAME_THREAD_ONLY();
	if ( Backend != nullptr )
	{
		Backend->PauseMovie( Handle );
	}
}

void CMovie::Stop()
{
	GAME_THREAD_ONLY();
	if ( Backend != nullptr )
	{
		Backend->PauseMovie( Handle );
		Backend->SeekMovie( Handle, 0.0f );
	}
}

bool CMovie::IsPlaying() const
{
	GAME_THREAD_ONLY( false );
	return Backend != nullptr && Backend->IsMoviePlaying( Handle );
}

float32 CMovie::GetPosition() const
{
	GAME_THREAD_ONLY( 0.0f );
	return Backend != nullptr ? Backend->GetMoviePosition( Handle ) : 0.0f;
}

void CMovie::Seek( float32 Seconds )
{
	GAME_THREAD_ONLY();
	if ( Backend != nullptr )
	{
		Backend->SeekMovie( Handle, NMath::Max( Seconds, 0.0f ) );
	}
}

void CMovie::SetVolume( float32 Volume )
{
	GAME_THREAD_ONLY();
	if ( Backend != nullptr )
	{
		Backend->SetMovieVolume( Handle, NMath::Clamp( Volume, 0.0f, 1.0f ) );
	}
}

bool CMovie::SetSpeed( float32 Speed )
{
	GAME_THREAD_ONLY( false );
	return Backend != nullptr && Speed > 0.0f && Backend->SetMovieSpeed( Handle, Speed );
}

void CMovie::Detach()
{
	if ( Backend != nullptr && IsInGameThread() )
	{
		Backend->CloseMovie( Handle );
	}
	Backend = nullptr;
}
