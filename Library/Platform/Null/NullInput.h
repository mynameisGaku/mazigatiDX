// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Input/EKey.h"
#include "Library/Framework/Input/EMouseButton.h"
#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CNullInput
//   IInput（キーボードとマウス）の、DxLib を使わない実装。実機の入力は読まず、プログラムが決めた入力だけを返す（何も決めなければ、何も押されない）。
//   --backend=null のときだけ作られる（選び方は Library/Platform/BackendSelection.h）。
//
//   自動の検査が、見本やゲームを「キーを押した」「マウスを動かした」状態で動かすのに使う:
//     CNullInput* Input = GetAppSubsystem<CNullInput>();
//     Input->SetKeyDown( EKey::Space, true );     // 次のフレームの頭（PreUpdate の Tick）から、Space が押された状態になる
//
//   - 設定した値は、次の Tick で、現在の状態になる。そのときの IsKeyPressed / IsMouseButtonPressed は、前の状態との差（押した瞬間）で決まる
//   - マウスは、画面の中にあり、窓が有効（bFocused が true）として扱う。位置は SetMousePosition で決める（初めは 0, 0）
//=============================================================================
class CNullInput : public CAppSubsystem, public IInput, public ITickable
{
public:
	// --backend=null のときだけ作る
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;

	//-------------------------------------------------------------------------
	// ITickable
	//-------------------------------------------------------------------------
	void Tick( float32 DeltaTime ) override;
	ETickGroup GetTickGroup() const override;

	//-------------------------------------------------------------------------
	// IKeyboardInput
	//-------------------------------------------------------------------------
	bool IsKeyDown( EKey Key ) const override;
	bool IsKeyPressed( EKey Key ) const override;
	bool IsKeyReleased( EKey Key ) const override;

	//-------------------------------------------------------------------------
	// IMouseInput
	//-------------------------------------------------------------------------
	const FMouseState& GetMouseState() const override;
	bool IsMouseButtonDown( EMouseButton Button ) const override;
	bool IsMouseButtonPressed( EMouseButton Button ) const override;
	bool IsMouseButtonReleased( EMouseButton Button ) const override;

	//-------------------------------------------------------------------------
	// 入力を決める（次の Tick から有効）。範囲の外のキー・ボタンは、何もしない
	//-------------------------------------------------------------------------
	void SetKeyDown( EKey Key, bool bDown );
	void SetMouseButtonDown( EMouseButton Button, bool bDown );
	void SetMousePosition( int32 X, int32 Y );
	// 次の Tick の 1 フレームだけ有効なホイールの回転（上と右が正）。足し合わせる
	void AddMouseWheel( float32 WheelX, float32 WheelY );
	// 押されているキー・ボタンを全部離す
	void ReleaseAll();

private:
	static constexpr int32 NumKeys = static_cast<int32>( EKey::Count );

	// 決めた入力（次の Tick で、現在の状態になる）
	bool PendingKeys[NumKeys] = {};
	uint32 PendingButtons = 0;
	int32 PendingX = 0;
	int32 PendingY = 0;
	float32 PendingWheelX = 0.0f;
	float32 PendingWheelY = 0.0f;

	// 現在と、前のフレームの状態
	bool CurrentKeys[NumKeys] = {};
	bool PreviousKeys[NumKeys] = {};
	FMouseState MouseState;
	bool bHadMouseTick = false;
};
