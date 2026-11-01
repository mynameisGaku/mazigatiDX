// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/PixelTimedEffect.h"

//=============================================================================
// CPixelLevelUpEffect
//   レベルアップ（一度きり・1.6 秒）。足元から光の柱が次々に昇り（後ろ）、頭の上に「LEVEL UP」が跳ねて現れ、きらめきが散る（前）。
//   基準点は足元。
//=============================================================================
class CPixelLevelUpEffect final : public CPixelTimedEffect
{
public:
	CPixelLevelUpEffect();

protected:
	void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const override;
};
