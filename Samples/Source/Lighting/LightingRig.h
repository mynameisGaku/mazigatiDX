// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"
#include "Source/Lighting/LightingDemoState.h"

//=============================================================================
// CLightingRig
//   メインのカメラを動かし、数字キーでライティングを切り替える。
//   - カメラは、中庭の中心を見て回る（入力があるまで、ひとりで回る）。マウスの左ボタンを押したまま動かす: 回る。ホイール: 近づく・離れる。R: ひとりで回る。
//   - 1: 月明かり（方向ライト）  2: 回る電球（点光源）  3: 動くスポットライト  4: 全体の環境光（4 段階）  5: 画素ごとの光  6: ハイライト（鏡面反射）
//     7: 光の当たらない側をやわらかく照らす  8: 周りの街灯の数（0・2・4・6）  9: 光の計算そのものの入り切り
//=============================================================================
class CLightingRig : public CGameObject
{
public:
	explicit CLightingRig( const TSharedPtr<FLightingDemoState>& InState );

	void Tick( float32 DeltaTime ) override;

private:
	// 軌道の値から、カメラ（FCamera）を作る
	void UpdateCamera();

	// 数字キーで、ライティングを切り替える
	void HandleKeys();

	TSharedPtr<FLightingDemoState> State;

	// 軌道: 横の回転・縦の回転（ラジアン）・注視点からの距離
	float32 Yaw = 0.0f;
	float32 Pitch = 0.5f;
	float32 Distance = 30.0f;

	// 入力があるまで、ひとりで回る
	bool bAutoOrbit = true;
};
