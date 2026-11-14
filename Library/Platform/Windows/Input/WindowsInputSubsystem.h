// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Core/Interfaces/Platform/INativeWindow.h"
#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

// 描画APIを知らないWindowsのキーボード・マウス入力。フレームに一度だけ取り込む。
class CWindowsInputSubsystem : public CAppSubsystem, public IInput, public ITickable
{
public:
	// 現段階ではDX12で選択。別のWindows描画実装でも再利用できる。
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	// OSの窓と論理解像度を借りる。
	void Initialize( FSubsystemCollection& Collection ) override;
	// 借用先と入力状態を外す。
	void Deinitialize() override;
	// フォーカスを失った場合は全ボタンを解放として取り込む。
	void Tick( float32 DeltaTime ) override;
	// 不正なキーはfalse。
	bool IsKeyDown( EKey Key ) const override;
	// 今フレームに押されたキーだけtrue。
	bool IsKeyPressed( EKey Key ) const override;
	// 今フレームに離されたキーだけtrue。
	bool IsKeyReleased( EKey Key ) const override;
	// 次の更新まで同じマウス状態を返す。
	const FMouseState& GetMouseState() const override;
	// 不正なボタンはfalse。
	bool IsMouseButtonDown( EMouseButton Button ) const override;
	// 今フレームに押されたボタンだけtrue。
	bool IsMouseButtonPressed( EMouseButton Button ) const override;
	// 今フレームに離されたボタンだけtrue。
	bool IsMouseButtonReleased( EMouseButton Button ) const override;
private:
	INativeWindow* Window = nullptr; // 借用するOS窓口。
	IPlatform* Platform = nullptr; // 論理解像度の取得先。
	bool Current[static_cast<int32>( EKey::Count )] = {}; // 今フレームのキー。
	bool Previous[static_cast<int32>( EKey::Count )] = {}; // 前フレームのキー。
	FMouseState Mouse; // 今フレームのマウス。
};
