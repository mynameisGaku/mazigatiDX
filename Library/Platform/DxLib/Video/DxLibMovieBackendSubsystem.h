// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Video/MovieBackend.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CDxLibMovieBackendSubsystem
//   IMovieBackend の DxLib 実装。アプリで 1 つ、自動で生成される。
//   ゲーム側は直接使わず、CMovieSubsystem で開いた CMovie を通して使う。
//
//   開く -> OpenMovieToGraph（ムービーのハンドル = 映像の画像のハンドル）/ 閉じる -> DeleteGraph
//   再生 -> PlayMovieToGraph（繰り返しは DX_PLAYTYPE_LOOP）/ 止める -> PauseMovieToGraph / 再生中か -> GetMovieStateToGraph
//   位置 -> TellMovieToGraph / SeekMovieToGraph（ミリ秒）、音量 -> SetMovieVolumeToGraph（デシベル）、速さ -> SetPlaySpeedRateMovieToGraph
//   情報 -> GetGraphSize、長さ = GetMovieTotalFrameToGraph x GetOneFrameTimeMovieToGraph
//=============================================================================
class CDxLibMovieBackendSubsystem : public CAppSubsystem, public IMovieBackend
{
public:
	// --backend=null のときは作らない（Library/Platform/Null の実装を使う）
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	void Initialize( FSubsystemCollection& Collection ) override;

	int32 OpenMovie( const FString& Path ) override;
	void CloseMovie( int32 Movie ) override;
	int32 GetMovieImage( int32 Movie ) override;
	bool GetMovieInfo( int32 Movie, FMovieInfo& OutInfo ) override;
	bool PlayMovie( int32 Movie, bool bLoop ) override;
	void PauseMovie( int32 Movie ) override;
	bool IsMoviePlaying( int32 Movie ) override;
	float32 GetMoviePosition( int32 Movie ) override;
	void SeekMovie( int32 Movie, float32 Seconds ) override;
	void SetMovieVolume( int32 Movie, float32 Volume ) override;
	bool SetMovieSpeed( int32 Movie, float32 Speed ) override;
};
