// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Movie/MovieTypes.h"

//=============================================================================
// IMovieBackend
//   ムービー（映像 + 音声のファイル）を開いて、再生を操作する、プラットフォーム層のインターフェース。
//   フレームワーク側（CMovieSubsystem / CMovie）はこれだけを知り、DxLib などの実装には依存しない。
//   実装: CDxLibMovieBackendSubsystem（DxLib）、CNullMovieBackendSubsystem（映さない確認用）
//
//   - ムービーは int32 のハンドルで表す。負の値は無効
//   - 映像は、再生に合わせて更新される「画像」になる。その画像のハンドルを GetMovieImage で得て、描画に渡す
//     （FImageHandle の値。ムービーを閉じるまで変わらない）
//   - 開ける形式は、バックエンドによる（DxLib は Windows が読める mp4 / wmv / avi など）
//   - すべての関数はメインスレッドだけから呼ばれる
//=============================================================================
class IMovieBackend
{
public:
	virtual ~IMovieBackend() = default;

	// 開く（Path は実在するファイルの絶対パス）。開けなければ負。開いた直後は止まっていて、位置は 0
	virtual int32 OpenMovie( const FString& Path ) = 0;
	virtual void CloseMovie( int32 Movie ) = 0;

	// 映像の画像のハンドル（描画に渡す FImageHandle の値）。無効なムービーなら負
	virtual int32 GetMovieImage( int32 Movie ) = 0;
	// 大きさ・長さ・コマ数。分からない項目は 0。無効なムービーなら false
	virtual bool GetMovieInfo( int32 Movie, FMovieInfo& OutInfo ) = 0;

	// 再生を始める（止めた位置から続ける）。bLoop なら、終わりまで来たら頭に戻って繰り返す。始められなければ false
	virtual bool PlayMovie( int32 Movie, bool bLoop ) = 0;
	// 止める（位置は覚えているので、PlayMovie で続きから）
	virtual void PauseMovie( int32 Movie ) = 0;
	// 再生中か（Pause したもの、終わりまで来て止まったものは false）
	virtual bool IsMoviePlaying( int32 Movie ) = 0;

	// 再生位置（秒）。止まっていても取れる
	virtual float32 GetMoviePosition( int32 Movie ) = 0;
	// 再生位置を変える（再生中でも止まっていても）
	virtual void SeekMovie( int32 Movie, float32 Seconds ) = 0;

	// 音量（0..1 の振幅。1 = 元の大きさ）
	virtual void SetMovieVolume( int32 Movie, float32 Volume ) = 0;
	// 再生の速さ（1 = 等倍、2 = 倍速）。対応していない実装は false を返す（そのまま等倍で再生される）
	virtual bool SetMovieSpeed( int32 /*Movie*/, float32 /*Speed*/ ) { return false; }
};
