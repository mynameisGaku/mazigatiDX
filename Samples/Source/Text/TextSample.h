// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Catalog/SampleScene.h"

//=============================================================================
// CTextSample
//   文字とフォントの見本。フォントの指定の違い（CFontShowcase）、箱の幅に合わせた折り返し（CWrapShowcase）、拡大・回転・影・縦書き（CStyledShowcase）。
//   シーンは、フォントと画像を用意して、3 つのオブジェクトを置くだけ。
//=============================================================================
class CTextSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;
};
