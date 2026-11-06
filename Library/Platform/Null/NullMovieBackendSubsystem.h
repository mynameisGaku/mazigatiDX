// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Interfaces/Video/MovieBackend.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CNullMovieBackendSubsystem
//   IMovieBackend の、DxLib を使わない実装。映さず、ムービーごとの状態（再生中か・位置・音量など）を覚えるだけ。
//   --backend=null のときだけ作られる（選び方は Library/Platform/BackendSelection.h）。
//
//   - 開けるのは、実在するファイル。映像は 640 x 360、長さは 10 秒、30 コマ / 秒とみなす
//   - 時間は進まない: Advance を呼ぶまで、再生位置は動かない。終わりまで来たら止まる（繰り返しなら頭へ戻る）
//   - 速さには対応する（Advance の進みに掛ける）
//=============================================================================
class CNullMovieBackendSubsystem : public CAppSubsystem, public IMovieBackend
{
public:
	// ムービー 1 つの状態
	struct FMovieState
	{
		bool bPlaying = false;
		bool bLoop = false;
		float32 Position = 0.0f;
		float32 Volume = 1.0f;
		float32 Speed = 1.0f;
	};

	// 映したことにするムービーの大きさ・長さ・コマ数
	static constexpr int32 FakeWidth = 640;
	static constexpr int32 FakeHeight = 360;
	static constexpr float32 FakeDuration = 10.0f;
	static constexpr float32 FakeFrameRate = 30.0f;

	// --backend=null のときだけ作る
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;

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

	//-------------------------------------------------------------------------
	// Null 固有（確認用）
	//-------------------------------------------------------------------------
	// 再生中のムービーを、Seconds だけ進める（速さを掛ける）
	void Advance( int32 Movie, float32 Seconds );
	// ムービーの状態。無ければ初めの状態
	FMovieState GetMovieState( int32 Movie ) const;
	int32 GetNumMovies() const;

private:
	TMap<int32, FMovieState> Movies;
	int32 NextHandle = 0;
};
