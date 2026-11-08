// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Catalog/SampleScene.h"

//=============================================================================
// CUISample
//   UI: widgets, brushes, render target のサンプル。
//   UI の部品（ボタン・チェックボックス・スライダー・プログレスバー・ラベル・画像）で作った設定の窓、描画先（レンダーターゲット）を
//   表示する部品、エンジン組み込みのアイコンの一覧。画面は CUIScreen が作り、描き、入力を配る。
//=============================================================================
class CUISample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;
};
