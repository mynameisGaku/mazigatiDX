// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"
#include "Source/Deferred/DeferredDemoState.h"

//=============================================================================
// CDeferredRig
//   メインのカメラを動かし、キーで見せ方を切り替える。
//   - カメラは、広場の中心を見て回る（入力があるまで、ひとりで回る）。マウスの左ボタンを押したまま動かす: 回る。ホイール: 近づく・離れる。R: ひとりで回る
//   - 1: 見せるもの（仕上がり・地の色・法線・位置・深度・自分で光る色）  2: Forward / Deferred  3: 時間を止める  Q / E: 露出を下げる / 上げる
//   絵を比べる検査のために、コマンドラインで、設定を固定できる:
//     --deferred-view=<0〜5>（見せるもの）、--deferred-forward（Forward で描く）、--deferred-time=<秒>（時間を止めて、ひとりで回るのもやめる）
//=============================================================================
class CDeferredRig : public CGameObject
{
public:
	explicit CDeferredRig( const TSharedPtr<FDeferredDemoState>& InState );

	void Tick( float32 DeltaTime ) override;

private:
	// 軌道の値から、カメラ（FCamera）を作る
	void UpdateCamera();

	// キーで、設定を切り替える
	void HandleKeys();

	TSharedPtr<FDeferredDemoState> State;

	// 軌道: 横の回転・縦の回転（ラジアン）・注視点からの距離
	float32 Yaw = 0.6f;
	float32 Pitch = 0.32f;
	float32 Distance = 26.0f;

	// 入力があるまで、ひとりで回る
	bool bAutoOrbit = true;
};
