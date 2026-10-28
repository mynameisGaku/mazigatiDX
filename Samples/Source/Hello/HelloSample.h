// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Catalog/SampleScene.h"

//=============================================================================
// CHelloSample
//   描画の基本のサンプル。図形・文字を並べるオブジェクト（CShapeGallery）と、跳ね返る四角形（CBouncingBox）を、シーンに置く。
//   シーンは、何をどこに置くかを決めるだけで、描き方・動き方は、それぞれのオブジェクトのクラスが持つ。
//=============================================================================
class CHelloSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;
};
