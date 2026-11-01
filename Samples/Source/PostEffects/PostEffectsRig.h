// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"
#include "Source/PostEffects/PostEffectsDemoState.h"

//=============================================================================
// CPostEffectsRig
//   メインのカメラを動かし、キーでポストエフェクトの設定を切り替える。
//   - カメラは、広場の中心を見て回る（入力があるまで、ひとりで回る）。マウスの左ボタンを押したまま動かす: 回る。ホイール: 近づく・離れる。R: ひとりで回る
//   - 1: 見せるもの（仕上がり・8 ビット相当・ブルームだけ・深度）  2: トーンマップ（ACES・Reinhard・なし）  3: ブルーム  4: 霧  5: 時間を止める
//   - Q / E: 露出を下げる / 上げる
//=============================================================================
class CPostEffectsRig : public CGameObject
{
public:
	explicit CPostEffectsRig( const TSharedPtr<FPostEffectsDemoState>& InState );

	void Tick( float32 DeltaTime ) override;

private:
	// 軌道の値から、カメラ（FCamera）を作る
	void UpdateCamera();

	// キーで、設定を切り替える
	void HandleKeys();

	TSharedPtr<FPostEffectsDemoState> State;

	// 軌道: 横の回転・縦の回転（ラジアン）・注視点からの距離
	float32 Yaw = 0.6f;
	float32 Pitch = 0.32f;
	float32 Distance = 26.0f;

	// 入力があるまで、ひとりで回る
	bool bAutoOrbit = true;
};
