// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Catalog/SampleScene.h"
#include "Source/Lighting/LightingDemoState.h"

//=============================================================================
// CLightingSample
//   3D のライティングの見本。夜の中庭に、ライトの使いどころを並べる:
//     月明かり（方向ライト）、床の上を回る電球（点光源）、床を舐めるスポットライト、周りの街灯（同時に 6 つまで）、
//     全体の環境光、材質の違う 5 つの球（つや消し・つや・自己発光・プラスチック・金属）、画素ごとの光、ハイライト、光の計算の入り切り。
//   数字キーで切り替えて、見え方の違いを確かめる。シーンは、画像と描画先を用意して、3 つのオブジェクトを置くだけ。
//=============================================================================
class CLightingSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;

private:
	TSharedPtr<FLightingDemoState> State;
};
