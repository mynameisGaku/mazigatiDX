// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/PixelTimedEffect.h"

//=============================================================================
// CPixelFireEffect
//   焚き火の炎（繰り返し・1 秒）。上へ流れるノイズで炎の形を毎画素決め、熱さで色の段を選ぶ（根元ほど明るい）。
//   火の粉は時刻と番号だけで位置が決まる（粒子を持たない）。基準点は炎の根元。
//=============================================================================
class CPixelFireEffect final : public CPixelTimedEffect
{
public:
	CPixelFireEffect();

protected:
	void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const override;
};
