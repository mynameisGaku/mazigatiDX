// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

//=============================================================================
// CKeyboardView
//   キーボードの一部（WASD・矢印・Space・Enter・Shift）を描いて、押している間だけ、そのキーを光らせる。
//   IInput::IsKeyDown（押している間ずっと true）の見本。
//=============================================================================
class CKeyboardView : public CGameObject
{
public:
	// InOrigin は、キーの並びの左上（画面の座標。ピクセル）
	explicit CKeyboardView( const FVector2& InOrigin );

	void Draw() override;

private:
	// キーの並びの左上（画面の座標。ピクセル）
	FVector2 Origin;
};
