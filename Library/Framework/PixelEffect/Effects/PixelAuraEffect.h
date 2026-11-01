// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/PixelTimedEffect.h"

//=============================================================================
// CPixelAuraEffect
//   オーラ（繰り返し・1.2 秒）。立ち絵のシルエットの縁から、炎のように揺らめいて昇る光（後ろ）。
//   シルエットは作るときに受け取って、持っておく（立ち絵そのものは描かない。呼ぶ側が前後の間に描く）。基準点は足元。
//=============================================================================
class CPixelAuraEffect final : public CPixelTimedEffect
{
public:
	explicit CPixelAuraEffect( const FPixelMask& InSilhouette );

protected:
	void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const override;

private:
	FPixelMask Silhouette;
};
