// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Catalog/SampleScene.h"
#include "Source/Village/VillageState.h"

//=============================================================================
// CVillageSample
//   小さな村を歩き回る、トップダウンの 2D のゲーム画面（M2 の仕上げの見本）。これまでの機能を、1 つの画面にまとめて使う。
//   絵はすべて CPU 側で作る（FPixelImage → CDynamicTexture）。タイルの地図、歩く主人公、回る金貨、村人との会話（折り返し・1 文字ずつ表示）、
//   夜の明かり（描画先とカスタムブレンド）、金貨の数と体力（円グラフ）とミニマップ。カメラは、2D の変換（Transform2D）で、主人公を追う。
//   シーンは、絵と描画先を用意して、オブジェクトを置くだけ。オブジェクトどうしは、FVillageState で状態を分け合う。
//=============================================================================
class CVillageSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleTick( float32 DeltaTime ) override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;

private:
	TSharedPtr<FVillageState> State;
};
