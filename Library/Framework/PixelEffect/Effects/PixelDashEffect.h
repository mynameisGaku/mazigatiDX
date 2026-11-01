// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/PixelTimedEffect.h"

//=============================================================================
// CPixelDashEffect
//   突進の残像（一度きり・0.5 秒）。立ち絵のシルエットを、後ろへ並べて色を付けて描き（後ろ）、追いつくように縮めて消す。
//   横に流れる速さの線も添える。シルエットは作るときに受け取って持つ。置き場所の左右反転で、左へ突進する。基準点は足元。
//=============================================================================
class CPixelDashEffect final : public CPixelTimedEffect
{
public:
	explicit CPixelDashEffect( const FPixelMask& InSilhouette );

protected:
	void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const override;

private:
	FPixelMask Silhouette;
};
