// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Framework/Capture/CaptureTypes.h"

//=============================================================================
// NCaptureCommandLine
//   起動引数から、スクリーンショットと GIF の記録の指示を読む（ドキュメントの画像を、決まったフレームで撮るため）。
//
//   --capture[=名前]            スクリーンショット（名前を省くと Screenshot_00001.png のような番号の名前）
//     --capture-frame=N         N フレーム目に撮る（既定 60）
//     --capture-region=X,Y,W,H  切り出す矩形      --capture-downscale=K  縦横 1/K に縮める
//     --capture-ui              デバッグ用の UI も写す   --capture-exit  撮ったら終わる
//     --capture-format=png|jpg|bmp|gif   保存の形式（名前の拡張子が優先）   --capture-quality=Q  Jpeg の品質
//   --record[=名前]             GIF の記録（名前を省くと Screenshot_00002.gif のような番号の名前）
//     --record-start=N          N フレーム目から（既定 1）   --record-frames=M  M 枚撮ったら止める（既定 60）
//     --record-step=S           S フレームごとに 1 枚（既定 2）
//     --record-region=X,Y,W,H  --record-downscale=K  --record-ui  --record-exit
//=============================================================================
struct FCaptureCommandLine
{
	bool bScreenshot = false;
	FString ScreenshotName;
	int32 ScreenshotFrame = 60;
	FScreenshotOptions Screenshot;
	bool bExitAfterScreenshot = false;

	bool bRecord = false;
	FString RecordName;
	int32 RecordStartFrame = 1;
	FGifRecordOptions Record;
	bool bExitAfterRecord = false;
};

namespace NCaptureCommandLine
{
	FCaptureCommandLine Parse( const FString& CommandLine );

	// "png" / "jpg" / "jpeg" / "bmp" / "gif"（先頭のドットは無くてもよい。大文字小文字は問わない）を形式に。知らない名前は false（Out は変えない）
	bool ParseFormat( const FString& Text, EScreenshotFormat& Out );

	// "X,Y,W,H" を矩形に。読めなければ false（Out は変えない）
	bool ParseRegion( const FString& Text, FCaptureRegion& Out );
}
