// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Catalog/SampleScene.h"
#include "Source/Deferred/DeferredDemoState.h"

//=============================================================================
// CDeferredSample
//   Deferred（後から光を当てる）描画の見本。夜の広場（動く光 24 個・光る溝・球と柱）を、Forward と Deferred の 2 つの経路で描き、同じ絵になることを見せる:
//     G-buffer（CGBuffer。地の色・法線・深度・自分で光る色を、1 回の描画で 4 枚の描画先へ書く）、
//     深度からの位置の再構成（FGBufferView。カメラの前向きの奥行きと画面の位置から、世界の位置を求める）、
//     ライティングのパス（G-buffer を画素ごとに読んで、光を当てる）、HDR の場面の色からトーンマップで画面へ。
//   キーで、見せるもの（G-buffer の中身）と、Forward / Deferred を切り替えて、違いを確かめる。シーンは、描画先とシェーダーを用意して、2 つのオブジェクトを置くだけ。
//=============================================================================
class CDeferredSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;

private:
	TSharedPtr<FDeferredDemoState> State;
};
