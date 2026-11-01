// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"
#include "Source/Island/IslandState.h"

//=============================================================================
// CIslandExplorer
//   探検家を歩かせ、カメラを追わせる。
//   - WASD / 矢印キー: カメラから見た向きに歩く（Shift で走る）。右クリック: 地面の点を目的地にする（画面の点から光線を出して、地形に当てる）
//   - キーを押すまでは、いちばん近い結晶へ、ひとりで歩く（R でまた、ひとりで歩く）
//   - 海と波打ち際には入らない（入れない向きは、壁に沿って滑る）。足元は、いつも地面の高さ
//   - カメラは、探検家の後ろの上から見る。マウスの左ボタンを押したまま動かす: 回る。ホイール: 近づく・離れる
//=============================================================================
class CIslandExplorer : public CGameObject
{
public:
	explicit CIslandExplorer( const TSharedPtr<FIslandState>& InState );

	void Tick( float32 DeltaTime ) override;

private:
	// 入力から、歩きたい向き（水平。長さ 0 か 1）と、走るかを決める
	FVector3 DecideDirection( bool& bOutRun );

	// 向き Direction へ、速さ Speed で動かす。歩けない所へは入らない
	void Move( const FVector3& Direction, float32 Speed, float32 DeltaTime );

	// カメラの軌道を、マウスで動かす
	void HandleCameraInput();

	// 軌道の値から、カメラ（FCamera）を作る
	void UpdateCamera();

	TSharedPtr<FIslandState> State;
};
