// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Catalog/SampleScene.h"

//=============================================================================
// CPixelsSample
//   CPU 側の画像（FPixelImage）の見本。画素を書いて画像にして毎フレーム更新する（CPlasmaView）、
//   スプライトシートを格子で分けて 1 コマずつの画像にする（CTileView）。シーンは、画像を用意して、2 つのオブジェクトを置くだけ。
//=============================================================================
class CPixelsSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;
};
