// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/PixelEffect/Playback/PixelTimedEffect.h"

//=============================================================================
// CPixelFreezeEffect
//   凍結（一度きり・1.2 秒）。地面から氷の結晶が順に突き出し（前）、しばらく光ってから、薄れて消える。
//   結晶の位置・高さ・傾きは、置き場所の種で決まる。基準点は足元。
//=============================================================================
class CPixelFreezeEffect final : public CPixelTimedEffect
{
public:
	CPixelFreezeEffect();

protected:
	void OnStart( FPixelRandom& Random ) override;
	void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const override;

private:
	struct FShard
	{
		float32 X = 0.0f;
		float32 Height = 10.0f;
		float32 HalfWidth = 2.0f;
		// 先端の横のずれ（傾き）
		float32 Lean = 0.0f;
		// 突き出し始める時刻（秒）
		float32 Delay = 0.0f;
	};

	TArray<FShard> Shards;
};
