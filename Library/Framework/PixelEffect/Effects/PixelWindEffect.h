// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/Playback/PixelTimedEffect.h"

//=============================================================================
// CPixelWindEffect
//   風（繰り返し・1.2 秒）。うねる風の筋が左から右へ流れ、木の葉が回りながら運ばれる。
//   筋と葉の位置は時刻と番号だけで決まり、1 周で描き先の幅をちょうど 1 回渡るので、繰り返しの継ぎ目が出ない。基準点は描き先の中央。
//=============================================================================
class CPixelWindEffect final : public CPixelTimedEffect
{
public:
	CPixelWindEffect();

protected:
	void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const override;
};
