// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/PixelTimedEffect.h"

//=============================================================================
// CPixelPoisonEffect
//   毒（繰り返し・1.5 秒）。体のあたりから泡が揺れながら昇り、膨らんで弾ける（前）。体の周りの淀んだ光（後ろ）。
//   泡は時刻と番号だけで位置が決まる（粒子を持たない）。基準点は足元。
//=============================================================================
class CPixelPoisonEffect final : public CPixelTimedEffect
{
public:
	CPixelPoisonEffect();

protected:
	void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const override;
};
