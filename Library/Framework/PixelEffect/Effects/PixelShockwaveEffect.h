// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/PixelTimedEffect.h"

//=============================================================================
// CPixelShockwaveEffect
//   衝撃波（一度きり・0.7 秒）。外へ速く広がって細く薄れる輪と、少し遅れて追う内側の輪。時刻だけで絵が決まる。
//=============================================================================
class CPixelShockwaveEffect final : public CPixelTimedEffect
{
public:
	CPixelShockwaveEffect();

protected:
	void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const override;
};
