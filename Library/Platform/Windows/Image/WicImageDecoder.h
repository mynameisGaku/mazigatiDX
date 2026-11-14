// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Math/Color.h"

// Windows標準のWICで画像をRGBA8へ展開する。GPUと登録番号に依存しない。
class FWicImageDecoder
{
public:
	// メモリのファイル内容を展開する。失敗時は出力を一切変更しない。
	static bool Decode( const void* FileImage, usize Size, int32& OutWidth, int32& OutHeight, TArray<FColor32>& OutPixels );
};
