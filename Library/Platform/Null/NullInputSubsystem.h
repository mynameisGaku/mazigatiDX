// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Input/EKey.h"
#include "Library/Framework/Input/EMouseButton.h"
#include "Library/Core/Interfaces/Input/ICursorControl.h"
#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Core/Interfaces/Input/ITextInput.h"
#include "Library/Core/Interfaces/Input/ITouchInput.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Input/Touch/TouchTracker.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CNullInputSubsystem
//   IInput（キーボードとマウス）の、DxLib を使わない実装。実機の入力は読まず、プログラムが決めた入力だけを返す（何も決めなければ、何も押されない）。
//   --backend=null のときだけ作られる（選び方は Library/Platform/BackendSelection.h）。
//
//   自動の検査が、見本やゲームを「キーを押した」「マウスを動かした」状態で動かすのに使う:
//     CNullInputSubsystem* Input = GetAppSubsystem<CNullInputSubsystem>();
//     Input->SetKeyDown( EKey::Space, true );     // 次のフレームの頭（PreUpdate の Tick）から、Space が押された状態になる
//
//   - 設定した値は、次の Tick で、現在の状態になる。そのときの IsKeyPressed / IsMouseButtonPressed は、前の状態との差（押した瞬間）で決まる
//   - マウスは、画面の中にあり、ウィンドウが有効（bFocused が true）として扱う。位置は SetMousePosition で決める（初めは 0, 0）
//=============================================================================
class CNullInputSubsystem : public CAppSubsystem, public IInput, public ITextInput, public ICursorControl, public ITouchInput, public ITickable
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
	// ITextInput（文字入力を受けているときだけ、決めた入力が届く。受けていない間に決めた入力は、本物と同じく捨てる）
	//-------------------------------------------------------------------------
	void SetTextInputActive( bool bActive ) override;
	bool IsTextInputActive() const override;
	const TArray<FTextInputEvent>& GetTextInputEvents() const override;
	const FImeComposition& GetImeComposition() const override;

	//-------------------------------------------------------------------------
	// ICursorControl（位置は、次の Tick からマウスの位置になる）
	//-------------------------------------------------------------------------
	bool SetCursorVisible( bool bVisible ) override;
	bool IsCursorVisible() const override;
	bool SetCursorPosition( int32 X, int32 Y ) override;

	//-------------------------------------------------------------------------
	// ITouchInput
	//-------------------------------------------------------------------------
	bool IsTouchAvailable() const override;
	const TArray<FTouchPoint>& GetTouches() const override;
	const TArray<uint32>& GetReleasedTouchIds() const override;

	//-------------------------------------------------------------------------
	// 文字入力・タッチを決める（次の Tick から有効）
	//-------------------------------------------------------------------------
	// 届く文字（確定した文字列）・編集の操作を、順に足す
	void AddTypedText( const FString& Text );
	void AddTextCommand( ETextEditCommand Command );
	// 変換中の状態を決める（次の Tick から。受けていなければ、空になる）
	void SetImeComposition( const FImeComposition& Composition );
	// いま触れている指の一覧を決める（bPressedThisFrame は無視。触れ始め・離れたは、前の一覧との違いで決まる）
	void SetTouches( const TArray<FTouchPoint>& Touches );

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

	// 文字入力
	bool bTextInputActive = false;
	TArray<FTextInputEvent> PendingTextEvents;
	FImeComposition PendingComposition;
	TArray<FTextInputEvent> TextEvents;
	FImeComposition ImeComposition;

	// カーソルとタッチ
	bool bCursorVisible = true;
	TArray<FTouchPoint> PendingTouches;
	FTouchTracker TouchTracker;
};
