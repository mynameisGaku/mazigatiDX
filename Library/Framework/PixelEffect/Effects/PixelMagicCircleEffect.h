// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/PixelTimedEffect.h"

//=============================================================================
// CPixelMagicCircleEffect
//   魔法陣（繰り返し・2 秒）。足元の 2 重の楕円（後ろ）と、外の楕円の上を 1 周で 1 回まわる印、印から昇る光の筋。
//   印と筋は、楕円の奥の半分を後ろの層、手前の半分を前の層に描く（立ち絵を挟むと、印が立ち絵の周りを回って見える）。
//   1 周でちょうど 1 回まわるので、繰り返しの継ぎ目が出ない。基準点は足元。
//=============================================================================
class CPixelMagicCircleEffect final : public CPixelTimedEffect
{
public:
	CPixelMagicCircleEffect();

protected:
	void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const override;
};
