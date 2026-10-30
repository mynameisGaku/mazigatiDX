// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"
#include "Source/RenderState/RenderStateDemoState.h"

//=============================================================================
// CRenderStateRig
//   メインのカメラを動かし、数字キーで描画状態を切り替える。
//   - カメラは、舞台の中心を見て回る（入力があるまで、ひとりで回る）。マウスの左ボタンを押したまま動かす: 回る。ホイール: 近づく・離れる。R: ひとりで回る。
//   - 1: ガラスの深度（書く / 書かない）  2: 部屋のカリング（なし / 裏面 / 表面）  3: 距離のフォグ（なし / 直線 / 指数 / 指数の 2 乗）  4: 足元の霧（高さのフォグ）
//     5: 地面のフィルター（Nearest / Bilinear / Anisotropic）  6: 異方性の強さ（1・2・4・8・16）  7: 水面を流す  8: 光線の前に深度を消す
//=============================================================================
class CRenderStateRig : public CGameObject
{
public:
	explicit CRenderStateRig( const TSharedPtr<FRenderStateDemoState>& InState );

	void Tick( float32 DeltaTime ) override;

private:
	// 軌道の値から、カメラ（FCamera）を作る
	void UpdateCamera();

	// 数字キーで、描画状態を切り替える
	void HandleKeys();

	TSharedPtr<FRenderStateDemoState> State;

	// 軌道: 横の回転・縦の回転（ラジアン）・注視点からの距離
	float32 Yaw = 0.35f;
	float32 Pitch = 0.3f;
	float32 Distance = 24.0f;

	// 入力があるまで、ひとりで回る
	bool bAutoOrbit = true;
};
