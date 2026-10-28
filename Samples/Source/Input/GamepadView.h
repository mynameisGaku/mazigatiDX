// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

//=============================================================================
// CGamepadView
//   つながっているゲームパッド（最大 2 台）の、スティック・トリガー・フェイスボタンの状態を図で出す。
//   つながっていなければ、その案内を出す。IGamepadInput::GetDevices / GetState の見本。
//=============================================================================
class CGamepadView : public CGameObject
{
public:
	// InOrigin は、表示の左上（画面の座標。ピクセル）
	explicit CGamepadView( const FVector2& InOrigin );

	void Draw( FDrawContext& CommandList ) override;

private:
	// 表示の左上（画面の座標。ピクセル）
	FVector2 Origin;
};
