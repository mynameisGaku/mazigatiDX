// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/Playback/PixelTimedEffect.h"

//=============================================================================
// CPixelCoinEffect
//   お金を拾う（一度きり・0.9 秒）。回りながら跳ね上がる硬貨が、頂点できらめいて消え、「+1」の文字が昇る。基準点は硬貨のあった所。
//=============================================================================
class CPixelCoinEffect final : public CPixelTimedEffect
{
public:
	CPixelCoinEffect();

protected:
	void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const override;
};
