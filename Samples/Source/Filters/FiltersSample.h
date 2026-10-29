// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Catalog/SampleScene.h"

//=============================================================================
// CFiltersSample
//   画像の処理の見本。1 枚の画像に、フィルターを掛けた結果・別の画像との混ぜ方を並べる（CFilterGallery）、マスクで、動くスポットライトの形に絵を見せる（CSpotlightView）。
//   シーンは、画像・描画先を用意して、2 つのオブジェクトを置くだけ。
//=============================================================================
class CFiltersSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;
};
