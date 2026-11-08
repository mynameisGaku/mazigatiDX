// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Catalog/SampleScene.h"
#include "Source/Materials/MaterialsDemoState.h"

//=============================================================================
// CMaterialsSample
//   マテリアル（ノードのグラフで作る見た目。Docs/MATERIAL.md）の見本。EngineContent/Materials の組み込みのマテリアルを読んで使う:
//     Surface      庭の球と柱と地面（大理石・車の塗装・縁の光・市松模様）。光や影の計算まで、マテリアルが持つ
//     PostProcess  描画先の絵に掛ける効果（白黒・周りを暗く・モザイク）。数字キーで、チェーンへ入れる・外す
//     UI           光るボタン（マウスののり方・押し方で変わる）と、光の帯（時間で流れる）
//   シーンは、マテリアルを読んで、描画先とチェーンを用意して、2 つのオブジェクトを置くだけ。
//=============================================================================
class CMaterialsSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;

private:
	TSharedPtr<FMaterialsDemoState> State;
};
