// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/PixelParticleSystem.h"
#include "Library/Framework/PixelEffect/PixelTimedEffect.h"

//=============================================================================
// CPixelExplosionEffect
//   爆発（一度きり・1.2 秒）。最初の一瞬に光る火の玉、四方へ飛んで落ちる火花（前）、ゆっくり昇って消える煙（後ろ）。
//=============================================================================
class CPixelExplosionEffect final : public CPixelTimedEffect
{
public:
	CPixelExplosionEffect();

protected:
	void OnStart( FPixelRandom& Random ) override;
	void OnStep( float32 InStepTime ) override;
	void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const override;

private:
	CPixelParticleSystem Sparks;
	CPixelParticleSystem Smoke;
	TArray<FColor32> SmokePalette;
};
