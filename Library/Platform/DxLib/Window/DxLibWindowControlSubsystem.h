// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Platform/IWindowControl.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CDxLibWindowControlSubsystem
//   IWindowControl の DxLib 実装。位置とフォーカスは Windows の API（DxLib はメインウィンドウのハンドルを渡してくれる）、
//   倍率は CDxLibBackendSubsystem::SetWindowExtendRate（DxLib のウィンドウ倍率）に任せる。
//   ディスプレイの並びは IDisplayInfo から取る（DxLib の関数には直接依存しない）。
//=============================================================================
class CDxLibWindowControlSubsystem : public CAppSubsystem, public IWindowControl
{
public:
	// --backend=null のときは作らない（Library/Platform/Null の実装を使う）
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	void Initialize( FSubsystemCollection& Collection ) override;

	bool GetWindowPosition( int32& OutX, int32& OutY ) const override;
	bool SetWindowPosition( int32 X, int32 Y ) override;
	bool MoveWindowToDisplay( int32 DisplayIndex ) override;
	float64 GetWindowScale() const override;
	bool SetWindowScale( float64 Scale ) override;
	bool IsWindowFocused() const override;
};
