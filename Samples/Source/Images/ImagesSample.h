// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Catalog/SampleScene.h"

//=============================================================================
// CImagesSample
//   2D の画像の描画の見本。確認用の画像 1 枚（NTestPattern。ファイルは置かず、メモリの上で作る）を、等倍・拡大・自由な四角形・一部分・
//   基準点と回転・タイル・円グラフで描く。シーンは、画像を用意して、描くオブジェクト（CImageShowcase）を置くだけ。
//=============================================================================
class CImagesSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;
};
