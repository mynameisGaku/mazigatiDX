// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Math/Color.h"

// 既定のゲーム文字をCPU上の白いRGBA画像へ変換する。GPUとUIの文脈を必要としない。
class IRenderTextRasterizer
{
  public:
	// 借用先からの破棄を許可する。
	virtual ~IRenderTextRasterizer() = default;
	// UTF-8の指定範囲を画像化する。Alphaが文字の被覆率。不正入力・上限超過は全出力を保ってfalse。
	virtual bool RasterizeText( TArrayView<const char> Text, TArray<FColor32> &OutPixels, int32 &OutWidth, int32 &OutHeight ) = 0;
};
