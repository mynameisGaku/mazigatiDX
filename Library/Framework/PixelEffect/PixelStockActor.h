// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/PixelEffectTypes.h"

//=============================================================================
// NPixelStockActor
//   状態変化・オーラ・被弾の演出を試すための、標準の立ち絵（12 x 19 画素の旅人）。
//   Scale = 2 なら、同じ絵を 2 倍に拡大した 24 x 38（1 未満は 1、3 以上は 2）。足元の中心が (0, 0)。
//=============================================================================
namespace NPixelStockActor
{
	FPixelActor Make( int32 Scale = 1 );
}
