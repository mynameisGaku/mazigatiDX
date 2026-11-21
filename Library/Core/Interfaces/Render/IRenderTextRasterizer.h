// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Math/Color.h"
#include "Library/Core/Render/RenderTextFont.h"

// 既定のゲーム文字をCPU上の白いRGBA画像へ変換する。GPUとUIの文脈を必要としない。
class IRenderTextRasterizer
{
  public:
	// 借用先からの破棄を許可する。
	virtual ~IRenderTextRasterizer() = default;
	// UTF-8の指定範囲を画像化する。Alphaが文字の被覆率。不正入力は全出力を保ってfalse。実装の画像の大きさの上限を超える文字列は、はみ出す右・下を切った画像にする（失敗にしない）。
	virtual bool RasterizeText( TArrayView<const char> Text, TArray<FColor32> &OutPixels, int32 &OutWidth, int32 &OutHeight ) = 0;
	// フォントを指定して、UTF-8の指定範囲を画像化する。Alphaが文字の被覆率。縁取りのあるフォントでは、OutEdgePixelsに縁取りを含む被覆率も返す（無ければ空）。
	// 未対応の実装・不正入力・上限超過は全出力を保ってfalse。
	virtual bool RasterizeFontText( const FRenderTextFont & /*Font*/, TArrayView<const char> /*Text*/, TArray<FColor32> & /*OutPixels*/, TArray<FColor32> & /*OutEdgePixels*/, int32 & /*OutWidth*/, int32 & /*OutHeight*/ ) { return false; }
};
