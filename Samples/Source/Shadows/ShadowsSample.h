// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Catalog/SampleScene.h"
#include "Source/Shadows/ShadowsDemoState.h"

//=============================================================================
// CShadowsSample
//   影（シャドウマップ）の見本。昼の中庭に、家・木・木箱・柵・歩き回る旅人を置き、動く太陽の影を落とす:
//     影の絵（BeginShadowMap〜EndShadowMap）、影を受ける（UseShadowMap）、影の絵の大きさ・範囲・補正の深さ、柵のすき間の影、
//     影を受けないライト（ランタン・補助光）、影の絵の中身の表示（PreviewShadowMap）。
//   数字キーで切り替えて、見え方の違いを確かめる。シーンは、画像と影の絵を用意して、3 つのオブジェクトを置くだけ。
//=============================================================================
class CShadowsSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleTick( float32 DeltaTime ) override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;

private:
	TSharedPtr<FShadowsDemoState> State;
};
