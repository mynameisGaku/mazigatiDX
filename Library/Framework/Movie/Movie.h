// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Interfaces/Render/IImage.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Movie/MovieTypes.h"

class IMovieBackend;

//=============================================================================
// CMovie
//   開いたムービー（映像 + 音声）。CMovieSubsystem::Open で開く。ゲームスレッドから使う。
//   IImage を実装しているので、再生中のコマが、そのまま画像として描ける。
//
//   TSharedPtr<CMovie> Opening = Movies->Open( "Movies/Opening.mp4" );
//   Opening->Play();
//   // 毎フレーム
//   Draw.Image( 0.0f, 0.0f, Opening );          // 今のコマ
//   if ( !Opening->IsPlaying() ) { ... }        // 終わった
//
//   - 開いた直後は止まっている。Play で始める。Pause で止め、Play で続きから
//   - 手放すと閉じる。CMovieSubsystem が終わるときは、残っているムービーは使えなくなる（IsValid が false）
//   - 音量はムービー自身のもの。ミキサーのバス（CAudioSubsystem）には入らない
//=============================================================================
class CMovie : public IImage
{
public:
	// 開けなければ nullptr。普通は CMovieSubsystem::Open を使う
	static TSharedPtr<CMovie> Create( IMovieBackend& Backend, const FString& FullPath );

	~CMovie() override;
	CMovie( const CMovie& ) = delete;
	CMovie& operator=( const CMovie& ) = delete;

	// 使える状態か（バックエンドが無くなったら false）
	bool IsValid() const;

	// 今のコマ（IImage）。使えないときは無効なハンドル
	FImageHandle GetImageHandle() const override;

	const FMovieInfo& GetInfo() const { return Info; }
	const FString& GetPath() const { return Path; }

	// 再生を始める（止めた位置から続ける）。bLoop なら、終わりまで来たら頭に戻って繰り返す
	bool Play( bool bLoop = false );
	// 止める（位置は覚えている）
	void Pause();
	// 頭に戻して止める
	void Stop();
	bool IsPlaying() const;

	// 再生位置（秒）
	float32 GetPosition() const;
	void Seek( float32 Seconds );

	// 音量（0..1）
	void SetVolume( float32 Volume );
	// 再生の速さ（1 = 等倍）。バックエンドが対応しないと false（等倍のまま）
	bool SetSpeed( float32 Speed );

	// CMovieSubsystem が、終わるときに呼ぶ。以降は使えなくなる
	void Detach();

private:
	CMovie( IMovieBackend& InBackend, int32 InHandle, int32 InImage, const FString& InPath, const FMovieInfo& InInfo );

	IMovieBackend* Backend = nullptr;
	int32 Handle = -1;
	int32 Image = -1;
	FString Path;
	FMovieInfo Info;
};
