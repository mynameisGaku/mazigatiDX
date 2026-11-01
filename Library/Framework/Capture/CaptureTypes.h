// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Color.h"

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

// 高解像度のスクリーンショット（画面とは別の大きな描画先へ、もう一度描いて撮る）の設定
struct FHighResShotOptions
{
	// 画面の大きさの何倍で撮るか（Width / Height が 0 のとき。2 なら 1280 x 720 の画面を 2560 x 1440 で）
	float32 Multiplier = 2.0f;
	// 撮る大きさを直接決める（例: 3840 x 2160。どちらかが 0 なら Multiplier を使う）。画面と縦横比が違うときは、2D はどちらにも収まる倍率で描く
	int32 Width = 0;
	int32 Height = 0;
	// 被写体（SetSubject で決めたもの、または TakeHighResShot に渡したもの）だけを撮り、それ以外を透明にする
	bool bSubjectOnly = false;
	// 透明度を残せない形式（JPEG・BMP・GIF）で、被写体だけを保存するときの背景の色
	FColor32 BackgroundColor = FColor32( 0, 0, 0, 255 );
	// 保存の形式（名前の拡張子が優先）。被写体だけを透明で残すなら Png
	EScreenshotFormat Format = EScreenshotFormat::Png;
	int32 JpegQuality = 90;
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
