// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Catalog/SampleScene.h"
#include "Source/RenderState/RenderStateDemoState.h"

//=============================================================================
// CRenderStateSample
//   3D の描画状態の見本。一つの舞台に、描画状態の使いどころを並べる:
//     深度バッファ（半透明のガラス）、カリング（部屋）、画像の貼り方と変換（壁・水面）、距離のフォグと高さのフォグ（遠くの柱・足元の霧）、
//     フィルターと異方性（地面の市松模様）、3D 図形の変換の積み重ね（腕）、深度バッファの消去（手前に描く光線）。
//   数字キーで切り替えて、見え方の違いを確かめる。シーンは、画像と描画先を用意して、3 つのオブジェクトを置くだけ。
//=============================================================================
class CRenderStateSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;

private:
	TSharedPtr<FRenderStateDemoState> State;
};
