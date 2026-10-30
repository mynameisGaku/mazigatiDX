// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"
#include "Source/Shapes3D/Shapes3DDemoState.h"

//=============================================================================
// CShapes3DRig
//   メインのカメラを動かし、数字キーで設定を切り替える。
//   - カメラは、広場の中心を見て回る（入力があるまで、ひとりで回る）。マウスの左ボタンを押したまま動かす: 回る。ホイール: 近づく・離れる。R: ひとりで回る。
//   - 1: 円錐を線で描く  2: フォグ（点・線・多角形・画像の違いが見える）  3: 頂点の並びの Culling（なし・裏を描かない・表を描かない）
//     4: ビルボードの基準点（足元・中心）  5: 画像を半透明に  6: 光の計算の入り切り
//=============================================================================
class CShapes3DRig : public CGameObject
{
public:
	explicit CShapes3DRig( const TSharedPtr<FShapes3DDemoState>& InState );

	void Tick( float32 DeltaTime ) override;

private:
	// 軌道の値から、カメラ（FCamera）を作る
	void UpdateCamera();

	// 数字キーで、設定を切り替える
	void HandleKeys();

	TSharedPtr<FShapes3DDemoState> State;

	// 軌道: 横の回転・縦の回転（ラジアン）・注視点からの距離
	float32 Yaw = 0.0f;
	float32 Pitch = 0.45f;
	float32 Distance = 36.0f;

	// 入力があるまで、ひとりで回る
	bool bAutoOrbit = true;
};
