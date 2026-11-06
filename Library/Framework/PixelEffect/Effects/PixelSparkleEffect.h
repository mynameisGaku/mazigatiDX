// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/PixelEffect/Playback/PixelTimedEffect.h"

//=============================================================================
// CPixelSparkleEffect
//   きらめき（繰り返し・1.6 秒）。基準点の周りの星が、それぞれの間合いで瞬く（十字の腕が伸び縮みし、中心が光る）。
//   星の位置と間合いは、置き場所のシードで決まる。
//=============================================================================
class CPixelSparkleEffect final : public CPixelTimedEffect
{
public:
	CPixelSparkleEffect();

protected:
	void OnStart( FPixelRandom& Random ) override;
	void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const override;

private:
	struct FStar
	{
		FVector2 Position;
		// 1 周の中で、いちばん明るくなる時刻の割合（0〜1）
		float32 Phase = 0.0f;
		float32 Size = 2.0f;
	};

	TArray<FStar> Stars;
};
