// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// NTestPattern
//   サンプルで使う確認用の画像（64 x 64。8 x 8 マスの色の格子）を、BMP ファイルの中身として作る。
//   画像のファイルを置かなくても動くようにするため。向きが分かるよう、四隅のマスは色が決まっている
//   （左上: 白、右上: 赤、左下: 緑、右下: 青。そのほかは、場所で色が変わる青っぽい色）。
//=============================================================================
namespace NTestPattern
{
	// 画像の大きさ（ピクセル）
	constexpr int32 Size = 64;

	// BMP ファイルの中身（CAssetManager::CreateTextureFromMemory に渡す）
	TArray<uint8> MakeBitmap();
}
