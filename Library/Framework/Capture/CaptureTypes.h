// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// 画面の記録（スクリーンショット・GIF）の設定の値の型
//=============================================================================

// 画面のうち、切り出す矩形（画面の画素。Width か Height が 0 以下なら画面の全体）。画面からはみ出す分は、画面の中に収める
struct FCaptureRegion
{
	int32 X = 0;
	int32 Y = 0;
	int32 Width = 0;
	int32 Height = 0;

	FORCEINLINE bool IsWholeScreen() const
	{
		return Width <= 0 || Height <= 0;
	}
};

// スクリーンショットの保存の形式
enum class EScreenshotFormat : uint8
{
	Png,    // 可逆圧縮。透明度も残る（既定）
	Jpeg,   // 非可逆圧縮（小さい）。透明度は残らない
	Bmp,    // 無圧縮。透明度は残らない
	Gif,    // 256 色まで（色が多いと減らす）。静止画 1 枚
};

// スクリーンショットの設定
struct FScreenshotOptions
{
	// 保存の形式（名前に .png / .jpg / .jpeg / .bmp / .gif の拡張子があれば、拡張子の形式を使う）
	EScreenshotFormat Format = EScreenshotFormat::Png;
	// Jpeg の品質（0〜100）
	int32 JpegQuality = 90;
	FCaptureRegion Region;
	// 縮める割合（1 = そのまま、2 = 縦横 1/2。2 x 2 の画素の平均）。1 未満は 1
	int32 Downscale = 1;
	// デバッグ用の UI（ImGui）も写す（既定は、ゲームの絵だけ）
	bool bIncludeDebugUI = false;
	// 何フレーム待ってから撮るか（画面を整える時間）
	int32 DelayFrames = 0;
};

// GIF の記録の設定
struct FGifRecordOptions
{
	FCaptureRegion Region;
	int32 Downscale = 1;
	// 何フレームごとに 1 枚撮るか（1 = 毎フレーム、2 = 1 つおき）。1 未満は 1
	int32 FrameStep = 2;
	// アプリの 1 秒あたりのフレーム数（GIF の 1 枚の表示時間を、これと FrameStep から決める）
	int32 AppFramesPerSecond = 60;
	// 撮る枚数の上限（0 なら、StopRecording まで）
	int32 MaxFrames = 0;
	// 繰り返し再生する
	bool bLoop = true;
	// デバッグ用の UI（ImGui）も写す（既定は、ゲームの絵だけ）
	bool bIncludeDebugUI = false;
};
