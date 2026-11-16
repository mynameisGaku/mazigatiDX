// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Platform/IDisplayInfo.h"
#include "Library/Core/Interfaces/Platform/INativeWindow.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CWindowsDisplayInfoSubsystem
//   つながっているディスプレイの情報を、Windows の API（EnumDisplayMonitors / EnumDisplaySettings / GetDpiForMonitor）だけで調べて、
//   IDisplayInfo として貸す。描画 API も DxLib も知らない。ゲームの窓は INativeWindow から借りる（無ければ、窓についての答えは -1 / 1.0）。
//=============================================================================
class CWindowsDisplayInfoSubsystem : public CAppSubsystem, public IDisplayInfo
{
public:
	// 現段階では DX12 のとき作る（DxLib は、DxLib の実装を使う）
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	void Initialize( FSubsystemCollection& Collection ) override;
	void Deinitialize() override;
	// 窓を借りる（Initialize が呼ぶ。検査が、自分で作った窓を渡すのにも使う）
	void Bind( INativeWindow* InWindow );

	int32 GetDisplayCount() const override;
	int32 GetPrimaryDisplayIndex() const override;
	bool GetDisplay( int32 Index, FDisplayInfo& Out ) const override;
	bool GetDisplayModes( int32 Index, TArray<FDisplayMode>& Out ) const override;
	int32 GetWindowDisplayIndex() const override;
	float32 GetWindowDpiScale() const override;

private:
	INativeWindow* Window = nullptr; // 借用する OS の窓
};
