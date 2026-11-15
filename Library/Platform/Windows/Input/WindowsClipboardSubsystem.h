// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Input/IClipboard.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CWindowsClipboardSubsystem
//   OS（Windows）のクリップボードを、IClipboard として貸す。文字列だけ（UTF-8 <-> UTF-16）。描画 API を知らない。
//=============================================================================
class CWindowsClipboardSubsystem : public CAppSubsystem, public IClipboard
{
public:
	// 現段階では DX12 のとき作る（DxLib は、DxLib の入力が IClipboard を持つ）
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;

	bool GetText( FString& OutText ) const override;
	bool SetText( const FString& Text ) override;
};
