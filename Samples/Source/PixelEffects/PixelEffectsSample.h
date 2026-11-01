// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Catalog/SampleScene.h"

//=============================================================================
// CPixelEffectsSample
//   ピクセルアートのエフェクトの見本。エフェクト 1 種につき 1 枚の見本（CPixelEffectTile）を格子に並べ、それぞれ繰り返し再生する。
//   F キーで左右反転、R キーで種を変えて最初から。シーンは、エフェクトを作って見本を置くだけ。
//=============================================================================
class CPixelEffectsSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;
};
