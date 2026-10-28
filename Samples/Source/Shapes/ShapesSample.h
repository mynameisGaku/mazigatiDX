// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Catalog/SampleScene.h"

//=============================================================================
// CShapesSample
//   2D の図形の見本。整数の座標の図形の列と、なめらかな（アンチエイリアス付き、float の座標の）図形の列を、同じ動きで並べる。
//   シーンは、何をどこに置くかだけを決める。図形の描き方・動き方は、オブジェクトのクラス（CShapeShowcase）が持つ。
//=============================================================================
class CShapesSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;
};
