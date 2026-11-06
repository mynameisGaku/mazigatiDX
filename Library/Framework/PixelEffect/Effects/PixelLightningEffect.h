// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/Playback/PixelTimedEffect.h"

//=============================================================================
// CPixelLightningEffect
//   落雷（一度きり・0.5 秒）。上から基準点（落ちる所）へ、ぎざぎざの稲妻と枝。数刻みごとに形が変わってちらつく。
//   形は、種と「何回目のちらつきか」だけで決まる。
//=============================================================================
class CPixelLightningEffect final : public CPixelTimedEffect
{
public:
	CPixelLightningEffect();

protected:
	void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const override;
};
