// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"
#include "Source/Shadows/ShadowsDemoState.h"

//=============================================================================
// CShadowsRig
//   メインのカメラと、太陽を動かし、数字キーで影の設定を切り替える。
//   - カメラは、中庭の中心を見て回る（入力があるまで、ひとりで回る）。マウスの左ボタンを押したまま動かす: 回る。ホイール: 近づく・離れる。R: ひとりで回る
//   - 太陽は、中庭の上を一周する。4 キーで止める
//   - 1: 影の入り切り  2: 影の絵の大きさ（256・512・1024・2048）  3: 影を計算する範囲（中庭だけ・広い範囲・カメラの見える範囲）  4: 太陽を止める・動かす
//     5: 補正の深さ（既定・0・大きい）  6: 影の絵の中身を右下に表示  7: ランタン（影を受けない点光源）
//=============================================================================
class CShadowsRig : public CGameObject
{
public:
	explicit CShadowsRig( const TSharedPtr<FShadowsDemoState>& InState );

	void Tick( float32 DeltaTime ) override;

private:
	// 軌道の値から、カメラ（FCamera）を作る
	void UpdateCamera();

	// 数字キーで、影の設定を切り替える
	void HandleKeys();

	TSharedPtr<FShadowsDemoState> State;

	// 軌道: 横の回転・縦の回転（ラジアン）・注視点からの距離
	float32 Yaw = 0.3f;
	float32 Pitch = 0.55f;
	float32 Distance = 38.0f;

	// 入力があるまで、ひとりで回る
	bool bAutoOrbit = true;
};
