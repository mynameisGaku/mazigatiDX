// SPDX-License-Identifier: MIT
#include "DxLibMovieBackendSubsystem.h"
#include "Library/Platform/DxLib/DxLibBackendSubsystem.h"

#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Audio/AudioTypes.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

REGISTER_SUBSYSTEM( CDxLibMovieBackendSubsystem )

bool CDxLibMovieBackendSubsystem::ShouldCreateSubsystem( CEngine& Engine ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DxLib;
}

void CDxLibMovieBackendSubsystem::Initialize( FSubsystemCollection& Collection )
{
	// DxLib（プラットフォーム）が初期化されたあとに使う
	if ( Collection.InitializeDependency<IPlatform>() == nullptr )
	{
		LOG( LogPlatform, Error, "No platform is available. Movies will not be played." );
	}
}

int32 CDxLibMovieBackendSubsystem::OpenMovie( const FString& Path )
{
	const int Handle = OpenMovieToGraph( *Path, TRUE );
	return Handle >= 0 ? Handle : -1;
}

void CDxLibMovieBackendSubsystem::CloseMovie( int32 Movie )
{
	DeleteGraph( Movie );
}

int32 CDxLibMovieBackendSubsystem::GetMovieImage( int32 Movie )
{
	// DxLib のムービーのハンドルは、そのまま映像の画像のハンドル
	return Movie;
}

bool CDxLibMovieBackendSubsystem::GetMovieInfo( int32 Movie, FMovieInfo& OutInfo )
{
	int Width = 0;
	int Height = 0;
	if ( GetGraphSize( Movie, &Width, &Height ) != 0 )
	{
		return false;
	}
	OutInfo.Width = Width;
	OutInfo.Height = Height;

	// 長さ = 総コマ数 x 1 コマの時間（マイクロ秒）
	const int TotalFrames = GetMovieTotalFrameToGraph( Movie );
	const LONGLONG FrameMicroseconds = GetOneFrameTimeMovieToGraph( Movie );
	if ( TotalFrames > 0 && FrameMicroseconds > 0 )
	{
		OutInfo.DurationSeconds = static_cast<float32>( static_cast<float64>( TotalFrames ) * static_cast<float64>( FrameMicroseconds ) / 1000000.0 );
		OutInfo.FrameRate = static_cast<float32>( 1000000.0 / static_cast<float64>( FrameMicroseconds ) );
	}
	return true;
}

bool CDxLibMovieBackendSubsystem::PlayMovie( int32 Movie, bool bLoop )
{
	return PlayMovieToGraph( Movie, bLoop ? DX_PLAYTYPE_LOOP : DX_PLAYTYPE_BACK ) == 0;
}

void CDxLibMovieBackendSubsystem::PauseMovie( int32 Movie )
{
	PauseMovieToGraph( Movie );
}

bool CDxLibMovieBackendSubsystem::IsMoviePlaying( int32 Movie )
{
	return GetMovieStateToGraph( Movie ) == 1;
}

float32 CDxLibMovieBackendSubsystem::GetMoviePosition( int32 Movie )
{
	return static_cast<float32>( TellMovieToGraph( Movie ) ) / 1000.0f;
}

void CDxLibMovieBackendSubsystem::SeekMovie( int32 Movie, float32 Seconds )
{
	SeekMovieToGraph( Movie, static_cast<int>( NMath::Max( Seconds, 0.0f ) * 1000.0f ) );
}

void CDxLibMovieBackendSubsystem::SetMovieVolume( int32 Movie, float32 Volume )
{
	// DxLib の値は 0（無音）..10000（元の大きさ）で、100 分の 1 デシベル単位
	const int32 Pal = NMath::Clamp( 10000 + NAudio::VolumeToDecibel100( NMath::Min( Volume, 1.0f ) ), 0, 10000 );
	SetMovieVolumeToGraph( Pal, Movie );
}

bool CDxLibMovieBackendSubsystem::SetMovieSpeed( int32 Movie, float32 Speed )
{
	return SetPlaySpeedRateMovieToGraph( Movie, static_cast<double>( Speed ) ) == 0;
}
