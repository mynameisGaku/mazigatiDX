// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/String.h"
#include "Library/Math/Color.h"

struct FImageSaveOptions;

// OSの画像保存を使う。GPU・UI・アセット番号を知らず、RGBA8の画素だけを受け取る。
class FWicImageEncoder
{
public:
	// PNGは透明度を保持し、BMP/JPEGは不透明で保存する。JPEG品質は0〜100へ収める。
	// 不正な入力・256MiBを超す画素列・PNG圧縮率の個別指定・符号化失敗は書き込み前にfalse。
	static bool Save( int32 Width, int32 Height, const FColor32* Pixels, const FString& Path, const FImageSaveOptions& Options );
};
