// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Platform/IDisplayInfo.h"
#include "Library/Core/Interfaces/Platform/INativeWindow.h"
#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Interfaces/Platform/IWindowControl.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CWindowsWindowControlSubsystem
//   ゲームの窓の位置・大きさの倍率・フォーカスを、Windows の API だけで操作して、IWindowControl として貸す。描画 API も DxLib も知らない。
//   倍率は、窓のクライアント領域の大きさ / ゲーム画面（描画先）の大きさ。位置と倍率を変えられるのは、通常の窓（EWindowMode::Windowed）のときだけ。
//   ディスプレイの並びは IDisplayInfo から取る。
//=============================================================================
class CWindowsWindowControlSubsystem : public CAppSubsystem, public IWindowControl
{
public:
	// 現段階では DX12 のとき作る（DxLib は、DxLib の実装を使う）
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	void Initialize( FSubsystemCollection& Collection ) override;
	void Deinitialize() override;
	// 窓・プラットフォーム・ディスプレイの情報を借りる（Initialize が呼ぶ。検査が、自分で作ったものを渡すのにも使う）
	void Bind( INativeWindow* InWindow, IPlatform* InPlatform, IDisplayInfo* InDisplays );

	bool GetWindowPosition( int32& OutX, int32& OutY ) const override;
	bool SetWindowPosition( int32 X, int32 Y ) override;
	bool MoveWindowToDisplay( int32 DisplayIndex ) override;
	float64 GetWindowScale() const override;
	bool SetWindowScale( float64 Scale ) override;
	bool IsWindowFocused() const override;

private:
	// 有効な窓の持ち手。無ければ nullptr
	void* GetHandle() const;
	bool IsWindowed() const;

	INativeWindow* Window = nullptr;
	IPlatform* Platform = nullptr;
	IDisplayInfo* Displays = nullptr;
};
