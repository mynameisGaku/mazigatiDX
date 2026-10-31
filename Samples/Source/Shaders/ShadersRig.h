// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"
#include "Source/Shaders/ShadersDemoState.h"

//=============================================================================
// CShadersRig
//   メインのカメラを動かし、数字キーでシェーダーの設定を切り替える。
//   - カメラは、庭の中心を見て回る（入力があるまで、ひとりで回る）。マウスの左ボタンを押したまま動かす: 回る。ホイール: 近づく・離れる。R: ひとりで回る
//   - 1: 物の見え方（トゥーン・法線の色・奥行き・うねり）  2: 地面（波紋・市松模様）  3: ポストエフェクト（なし・白黒・周りを暗く・モザイク・ゆがみ）  4: 時間を止める
//=============================================================================
class CShadersRig : public CGameObject
{
public:
	explicit CShadersRig( const TSharedPtr<FShadersDemoState>& InState );

	void Tick( float32 DeltaTime ) override;

private:
	// 軌道の値から、カメラ（FCamera）を作る
	void UpdateCamera();

	// 数字キーで、シェーダーの設定を切り替える
	void HandleKeys();

	TSharedPtr<FShadersDemoState> State;

	// 軌道: 横の回転・縦の回転（ラジアン）・注視点からの距離
	float32 Yaw = 0.4f;
	float32 Pitch = 0.45f;
	float32 Distance = 30.0f;

	// 入力があるまで、ひとりで回る
	bool bAutoOrbit = true;
};
