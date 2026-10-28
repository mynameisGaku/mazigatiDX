// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

//=============================================================================
// CMouseView
//   マウスの位置・動き・ホイール・ボタン（左・中・右）の状態を、文字と図で出す。
//   IMouseInput::GetMouseState / IsMouseButtonDown の見本。ホイールは、回した量の合計を数える。
//=============================================================================
class CMouseView : public CGameObject
{
public:
	// InOrigin は、表示の左上（画面の座標。ピクセル）
	explicit CMouseView( const FVector2& InOrigin );

	void Tick( float32 DeltaTime ) override;
	void Draw() override;

private:
	// 表示の左上（画面の座標。ピクセル）
	FVector2 Origin;

	// ホイールを回した量の合計（上が正。1 段が 1）
	float32 WheelTotal = 0.0f;
};
