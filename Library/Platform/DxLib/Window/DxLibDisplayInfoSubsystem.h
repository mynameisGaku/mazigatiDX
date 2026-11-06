// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Platform/IDisplayInfo.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CDxLibDisplayInfoSubsystem
//   IDisplayInfo の DxLib 実装。ディスプレイの一覧と解像度は DxLib（GetDisplayInfo / GetDisplayMode）、
//   ウィンドウのいるディスプレイと拡大率は Windows の API で調べる。読むたびに調べ直す（抜き差しが反映される）。
//   メインのディスプレイは、OS の設定（Windows の「メインディスプレイ」）を DxLib が読んだもの（IsPrimary）。
//   DxLib 未対応: ディスプレイの名前は DxLib から取れないので、Windows のデバイス名（"\\.\DISPLAY1" など）を使う。
//=============================================================================
class CDxLibDisplayInfoSubsystem : public CAppSubsystem, public IDisplayInfo
{
public:
	// --backend=null のときは作らない（Library/Platform/Null の実装を使う）
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	void Initialize( FSubsystemCollection& Collection ) override;

	int32 GetDisplayCount() const override;
	int32 GetPrimaryDisplayIndex() const override;
	bool GetDisplay( int32 Index, FDisplayInfo& Out ) const override;
	bool GetDisplayModes( int32 Index, TArray<FDisplayMode>& Out ) const override;
	int32 GetWindowDisplayIndex() const override;
	float32 GetWindowDpiScale() const override;
};
