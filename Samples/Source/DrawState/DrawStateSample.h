// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Catalog/SampleScene.h"

//=============================================================================
// CDrawStateSample
//   描画の状態の見本。色にかかわるもの（ブレンド・カスタムブレンド・加算する色・アルファテスト）は CBlendShowcase、
//   見え方にかかわるもの（クリップ範囲・描画モード・2D の変換）は CViewShowcase が見せる。
//   シーンは、画像を用意して、2 つのオブジェクトを置くだけ。
//=============================================================================
class CDrawStateSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;
};
