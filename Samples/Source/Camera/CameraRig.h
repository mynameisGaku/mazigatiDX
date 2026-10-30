// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"
#include "Source/Camera/CameraDemoState.h"

//=============================================================================
// CCameraRig
//   メインのカメラを動かす。注視点のまわりを回る（軌道カメラ）。入力があるまでは、ひとりで回る（見本を、すぐに動かして見せるため）。
//   - マウスの左ボタンを押したまま動かす: 回る。ホイール、W / S: 近づく・離れる。A / D: 回る。Q / E: 視野角（透視射影）。P: 透視射影 / 正射影。R: ひとりで回る。
//   毎フレーム、メインのカメラの視錐台に入っている物を調べ（FFrustum）、マウスが指している物を調べる（画面の点を通る光線 FCamera::ScreenToRay）。
//=============================================================================
class CCameraRig : public CGameObject
{
public:
	explicit CCameraRig( const TSharedPtr<FCameraDemoState>& InState );

	void Tick( float32 DeltaTime ) override;

private:
	// 軌道の値から、カメラ（FCamera）を作る
	void UpdateCamera();

	// 視錐台に入っている物を調べる
	void UpdateVisibility();

	// マウスの位置を通る光線が、最初にあたる物を調べる
	void UpdateHover();

	TSharedPtr<FCameraDemoState> State;

	// 軌道: 横の回転・縦の回転（ラジアン）・注視点からの距離・透視射影の縦の視野角（ラジアン）
	float32 Yaw = 0.6f;
	float32 Pitch = 0.45f;
	float32 Distance = 22.0f;
	float32 FovY = 0.9f;
	bool bOrthographic = false;

	// 入力があるまで、ひとりで回る
	bool bAutoOrbit = true;
};
