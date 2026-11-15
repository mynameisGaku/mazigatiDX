// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Input/ICursorControl.h"
#include "Library/Core/Interfaces/Platform/INativeWindow.h"
#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CWindowsCursorControlSubsystem
//   マウスカーソルの表示と位置を、ICursorControl として貸す。表示は IPlatform（窓）が持ち、位置は OS のカーソルを動かす。描画 API を知らない。
//   座標は、ゲームの描画領域の左上が原点（IMouseInput と同じ論理解像度）。
//=============================================================================
class CWindowsCursorControlSubsystem : public CAppSubsystem, public ICursorControl
{
public:
	// 現段階では DX12 のとき作る（DxLib は、DxLib の入力が ICursorControl を持つ）
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	// OS の窓と論理解像度を借りる
	void Initialize( FSubsystemCollection& Collection ) override;
	void Deinitialize() override;

	bool SetCursorVisible( bool bVisible ) override;
	bool IsCursorVisible() const override;
	// 窓がフォーカスを持たない間・窓の大きさが 0 のときは false（他のアプリのカーソルを動かさない）
	bool SetCursorPosition( int32 X, int32 Y ) override;

private:
	INativeWindow* Window = nullptr; // 借用する OS の窓
	IPlatform* Platform = nullptr; // 表示の状態と論理解像度の取得先（借用）
};
