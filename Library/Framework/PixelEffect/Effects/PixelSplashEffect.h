// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/PixelParticleSystem.h"
#include "Library/Framework/PixelEffect/PixelTimedEffect.h"

//=============================================================================
// CPixelSplashEffect
//   水しぶき（一度きり・0.9 秒）。水面から滴が跳ね上がって落ち（前）、水面に波紋が 2 重に広がる（後ろ）。基準点は水面に落ちた所。
//=============================================================================
class CPixelSplashEffect final : public CPixelTimedEffect
{
public:
	CPixelSplashEffect();

protected:
	void OnStart( FPixelRandom& Random ) override;
	void OnStep( float32 InStepTime ) override;
	void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const override;

private:
	CPixelParticleSystem Drops;
};
