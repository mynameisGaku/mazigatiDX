// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/PixelTimedEffect.h"

//=============================================================================
// CPixelBeamEffect
//   光線（一度きり・0.8 秒）。発射口に光の玉が溜まり、右へ太い光線を放って、細くなって消える。当たった所が光る。
//   置き場所の左右反転で、左へ撃つ。基準点は発射口。
//=============================================================================
class CPixelBeamEffect final : public CPixelTimedEffect
{
public:
	CPixelBeamEffect();

protected:
	void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const override;
};
