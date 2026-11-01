// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/PixelTimedEffect.h"

//=============================================================================
// CPixelDamageNumberEffect
//   ダメージの数値（一度きり・1 秒）。跳ね上がって少し落ち、最後に薄れて消える。会心なら大きな文字で金色に。
//   横の小さなずれは、置き場所の種で決まる（同じ所に続けて出しても重なりにくい）。
//=============================================================================
class CPixelDamageNumberEffect final : public CPixelTimedEffect
{
public:
	explicit CPixelDamageNumberEffect( int32 InValue = 128, bool bInCritical = false );

protected:
	void OnStart( FPixelRandom& Random ) override;
	void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const override;

private:
	FString Text;
	bool bCritical;
	float32 OffsetX = 0.0f;
};
