// SPDX-License-Identifier: MIT
#include "NullMovieBackendSubsystem.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/System/FileSystem.h"

REGISTER_SUBSYSTEM( CNullMovieBackendSubsystem )

bool CNullMovieBackendSubsystem::ShouldCreateSubsystem( CEngine& Engine ) const
{
	return NBackendSelection::IsNullRequested();
}

int32 CNullMovieBackendSubsystem::OpenMovie( const FString& Path )
{
	if ( !NFileSystem::FileExists( Path ) )
	{
		return -1;
	}
	const int32 Handle = NextHandle++;
	Movies.FindOrAdd( Handle );
	return Handle;
}

void CNullMovieBackendSubsystem::CloseMovie( int32 Movie )
{
	Movies.Remove( Movie );
}

int32 CNullMovieBackendSubsystem::GetMovieImage( int32 Movie )
{
	return Movies.Contains( Movie ) ? Movie : -1;
}

bool CNullMovieBackendSubsystem::GetMovieInfo( int32 Movie, FMovieInfo& OutInfo )
{
	if ( !Movies.Contains( Movie ) )
	{
		return false;
	}
	OutInfo.Width = FakeWidth;
	OutInfo.Height = FakeHeight;
	OutInfo.DurationSeconds = FakeDuration;
	OutInfo.FrameRate = FakeFrameRate;
	return true;
}

bool CNullMovieBackendSubsystem::PlayMovie( int32 Movie, bool bLoop )
{
	FMovieState* State = Movies.Find( Movie );
	if ( State == nullptr )
	{
		return false;
	}
	// 終わりまで来て止まっていたら、頭から
	if ( State->Position >= FakeDuration )
	{
		State->Position = 0.0f;
	}
	State->bPlaying = true;
	State->bLoop = bLoop;
	return true;
}

void CNullMovieBackendSubsystem::PauseMovie( int32 Movie )
{
	if ( FMovieState* State = Movies.Find( Movie ) )
	{
		State->bPlaying = false;
	}
}

bool CNullMovieBackendSubsystem::IsMoviePlaying( int32 Movie )
{
	const FMovieState* State = Movies.Find( Movie );
	return State != nullptr && State->bPlaying;
}

float32 CNullMovieBackendSubsystem::GetMoviePosition( int32 Movie )
{
	const FMovieState* State = Movies.Find( Movie );
	return State != nullptr ? State->Position : 0.0f;
}

void CNullMovieBackendSubsystem::SeekMovie( int32 Movie, float32 Seconds )
{
	if ( FMovieState* State = Movies.Find( Movie ) )
	{
		State->Position = NMath::Clamp( Seconds, 0.0f, FakeDuration );
	}
}

void CNullMovieBackendSubsystem::SetMovieVolume( int32 Movie, float32 Volume )
{
	if ( FMovieState* State = Movies.Find( Movie ) )
	{
		State->Volume = Volume;
	}
}

bool CNullMovieBackendSubsystem::SetMovieSpeed( int32 Movie, float32 Speed )
{
	FMovieState* State = Movies.Find( Movie );
	if ( State == nullptr || Speed <= 0.0f )
	{
		return false;
	}
	State->Speed = Speed;
	return true;
}

void CNullMovieBackendSubsystem::Advance( int32 Movie, float32 Seconds )
{
	FMovieState* State = Movies.Find( Movie );
	if ( State == nullptr || !State->bPlaying || Seconds <= 0.0f )
	{
		return;
	}
	State->Position += Seconds * State->Speed;
	if ( State->Position >= FakeDuration )
	{
		if ( State->bLoop )
		{
			State->Position = NMath::Fmod( State->Position, FakeDuration );
		}
		else
		{
			State->Position = FakeDuration;
			State->bPlaying = false;
		}
	}
}

CNullMovieBackendSubsystem::FMovieState CNullMovieBackendSubsystem::GetMovieState( int32 Movie ) const
{
	const FMovieState* State = Movies.Find( Movie );
	return State != nullptr ? *State : FMovieState();
}

int32 CNullMovieBackendSubsystem::GetNumMovies() const
{
	return Movies.Num();
}
