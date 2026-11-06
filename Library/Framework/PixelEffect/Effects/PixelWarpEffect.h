// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/Playback/PixelParticleSystem.h"
#include "Library/Framework/PixelEffect/Playback/PixelTimedEffect.h"

//=============================================================================
// CPixelWarpEffect
//   転送（一度きり・1 秒）。足元から光の柱が太くなってから細く閉じ、横長の輪が下から上へ走り、光の粒が昇る。基準点は足元。
//=============================================================================
class CPixelWarpEffect final : public CPixelTimedEffect
{
public:
	CPixelWarpEffect();

protected:
	void OnStart( FPixelRandom& Random ) override;
	void OnStep( float32 InStepTime ) override;
	void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const override;

private:
	CPixelParticleSystem Motes;
};
