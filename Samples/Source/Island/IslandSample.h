// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Catalog/SampleScene.h"
#include "Source/Island/IslandState.h"

//=============================================================================
// CIslandSample
//   3D の見本（ゲームの画面らしい構成）。M3 の機能を 1 つの画面にまとめて使う: 探検家が島を歩き、光る結晶を集める。
//     地形（頂点バッファ + インデックスバッファ。決まったシードから作る）、木と灯台（3D 図形）、空を動く太陽と影（シャドウマップ）、
//     回る灯台の光（スポットライト）、霧、半透明の海、加算の光のにじみ、地図と名札（WorldToScreen）、右クリックの目的地（ScreenToRay）。
//   シーンは、島を作って、5 つのオブジェクト（時間・探検家・結晶・3D の画面・表示）を置くだけ。
//=============================================================================
class CIslandSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;

private:
	TSharedPtr<FIslandState> State;
};
