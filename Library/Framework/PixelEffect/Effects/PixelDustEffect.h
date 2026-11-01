// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/PixelParticleSystem.h"
#include "Library/Framework/PixelEffect/PixelTimedEffect.h"

//=============================================================================
// CPixelDustEffect
//   着地の砂ぼこり（一度きり・0.8 秒）。足元から左右へ、地面を這うように広がって膨らむ土煙と、跳ねる小石。基準点は足元。
//=============================================================================
class CPixelDustEffect final : public CPixelTimedEffect
{
public:
	CPixelDustEffect();

protected:
	void OnStart( FPixelRandom& Random ) override;
	void OnStep( float32 InStepTime ) override;
	void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const override;

private:
	CPixelParticleSystem Puffs;
	CPixelParticleSystem Pebbles;
};
