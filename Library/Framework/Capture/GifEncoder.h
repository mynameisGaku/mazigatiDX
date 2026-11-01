// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Math/Color.h"

//=============================================================================
// FGifEncoder
//   画像を 1 枚ずつ受け取って、動く GIF（GIF89a）のバイト列を組み立てる（CPU だけ。ファイルにはしない）。
//   - 1 枚ごとに自分の色の表（256 色まで）を持つ。使われている色が 255 色以下なら、色は変わらない
//   - 前の 1 枚から変わった所を囲む矩形だけを書き、変わらない画素は透明にする（画面の記録は、変わらない所が多いので小さくなる）
//   - 前の 1 枚とまったく同じなら、新しい 1 枚を書かず、前の 1 枚の表示時間を延ばす
//
//   FGifEncoder Encoder( Width, Height, true );
//   Encoder.AddFrame( Pixels, 3 );          // 3 / 100 秒表示する
//   TArray<uint8> Bytes = Encoder.Finish();
//=============================================================================
class FGifEncoder
{
public:
	// 大きさは 1〜65535（範囲の外なら、何も書けない。IsValid が false）
	FGifEncoder( int32 InWidth, int32 InHeight, bool bLoop );

	FORCEINLINE bool IsValid() const
	{
		return Width > 0 && Height > 0;
	}

	// Width x Height 個の画素（左上から行の順。a は見ない）を、DelayCentiseconds（1/100 秒。1 未満は 1）表示する 1 枚として足す。
	// 画素の数が違うときは、何もせず false
	bool AddFrame( TArrayView<const FColor32> Pixels, int32 DelayCentiseconds );

	// 書いた枚数（前と同じで、表示時間を延ばしただけのものは数えない）
	FORCEINLINE int32 GetFrameCount() const
	{
		return FrameCount;
	}

	// 終わりを書いて、GIF のバイト列を返す（1 枚も無ければ空）。このあとは AddFrame できない
	TArray<uint8> Finish();

private:
	// まだ書いていない 1 枚（次の 1 枚が前と同じなら、表示時間を延ばすので、書くのを 1 枚遅らせる）
	struct FPendingFrame
	{
		int32 Left = 0;
		int32 Top = 0;
		int32 Width = 0;
		int32 Height = 0;
		int32 Delay = 0;
		TArray<FColor32> Palette;
		// 透明に使う番号（無ければ -1）
		int32 Transparent = -1;
		TArray<uint8> Indices;
	};

	// Pending を、バイト列へ書く
	void WritePending();

	int32 Width;
	int32 Height;
	bool bFinished = false;
	bool bHasPending = false;
	int32 FrameCount = 0;
	TArray<uint8> Bytes;
	TArray<FColor32> Previous;
	FPendingFrame Pending;
};
