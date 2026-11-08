// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"
#include "Source/Materials/MaterialsDemoState.h"

//=============================================================================
// CMaterialsRig
//   メインのカメラを動かし、数字キーでポストプロセスを切り替え、UI のボタンの状態（ホバー・押下）を調べる。
//   - カメラは、庭の中心を見て回る（入力があるまで、ひとりで回る）。マウスの左ボタンを押したまま動かす: 回る。ホイール: 近づく・離れる。R: ひとりで回る
//   - 1: 白黒  2: 周りを暗く  3: モザイク（ポストプロセスのチェーンへ、入れる・外す）  4: 時間を止める・動かす
//=============================================================================
class CMaterialsRig : public CGameObject
{
public:
	explicit CMaterialsRig( const TSharedPtr<FMaterialsDemoState>& InState );

	void Tick( float32 DeltaTime ) override;

private:
	// 軌道の値から、カメラ（FCamera）を作る
	void UpdateCamera();

	// 数字キーで、設定を切り替える
	void HandleKeys();

	TSharedPtr<FMaterialsDemoState> State;

	// 軌道: 横の回転・縦の回転（ラジアン）・注視点からの距離
	float32 Yaw = 0.4f;
	float32 Pitch = 0.45f;
	float32 Distance = 30.0f;

	// 入力があるまで、ひとりで回る
	bool bAutoOrbit = true;
};
