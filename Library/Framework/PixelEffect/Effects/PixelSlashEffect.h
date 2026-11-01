// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/PixelEffect/PixelTimedEffect.h"

//=============================================================================
// CPixelSlashEffect
//   斬撃（一度きり・0.35 秒）。右上から左下へ振り下ろす弧。刃先が速く進み、尾が遅れて追いついて消える。
//   置き場所の左右反転で、逆向きの振りになる。基準点は弧の中心（振る人の肩のあたり）。
//=============================================================================
class CPixelSlashEffect final : public CPixelTimedEffect
{
public:
	CPixelSlashEffect();

protected:
	void OnDraw( const CPixelPainter& Painter, EPixelLayer Layer ) const override;
};
