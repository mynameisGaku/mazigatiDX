// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Camera/CameraDemoState.h"
#include "Source/Catalog/SampleScene.h"

//=============================================================================
// CCameraSample
//   3D のカメラの見本。注視点のまわりを回る軌道カメラ（FCamera。透視射影 / 正射影、視野角）で、景色を見る。
//   - 同じ景色を、真上から見下ろす 2 つ目のカメラで、別の描画先に描いて、右下に貼る（FCameraScope と描画先）。メインのカメラが映す地面の範囲も重ねる。
//   - 世界の点が画面のどこに見えるか（WorldToScreen）で、物の名前を重ねる。マウスの位置を通る光線（ScreenToRay）で、指している物を調べる。
//   - 視錐台（FFrustum）に入らない物は、描かない。
//   シーンは、景色の物と描画先を用意して、4 つのオブジェクトを置くだけ。
//=============================================================================
class CCameraSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;

private:
	TSharedPtr<FCameraDemoState> State;
};
