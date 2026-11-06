// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/Playback/PixelParticleSystem.h"
#include "Library/Framework/PixelEffect/Playback/PixelTimedEffect.h"

//=============================================================================
// CPixelHealEffect
//   回復（一度きり・1.4 秒）。足元から広がる輪（後ろ）と、ゆらゆら昇って消える十字の光（前）。基準点は足元。
//=============================================================================
class CPixelHealEffect final : public CPixelTimedEffect
{
public:
	CPixelHealEffect();

protected:
	void OnStart( FPixelRandom& Random ) override;
	void OnStep( float32 InStepTime ) override;
	void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const override;

private:
	CPixelParticleSystem Crosses;
};
