#pragma once
#include "Library/Core/Interfaces/Input/ICursorControl.h"
#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Core/Interfaces/Input/ITextInput.h"
#include "Library/Core/Interfaces/Input/ITouchInput.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Framework/Input/Text/Utf8ByteAssembler.h"
#include "Library/Framework/Input/Touch/TouchTracker.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CDxLibInput
//   IInput（キーボード・マウス）と、ITextInput（文字入力・IME）、ICursorControl（カーソル）、ITouchInput（タッチ）の DxLib 実装。
//   毎フレームの頭 (PreUpdate) に入力を取り込む。ゲーム側は GetAppSubsystem<IInput>() などで取得する。
//   DxLib の文字コード・制御コード・IME の構造体は、DxLibInputText.cpp の中に閉じ込めてある。
//=============================================================================
class CDxLibInput : public CAppSubsystem, public IInput, public ITextInput, public ICursorControl, public ITouchInput, public ITickable
{
public:
	//-------------------------------------------------------------------------
	// CAppSubsystem
	//-------------------------------------------------------------------------
	// --backend=null のときは作らない（Library/Platform/Null の実装を使う）
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	void Initialize( FSubsystemCollection& Collection ) override;

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
	// ITextInput（DxLibInputText.cpp）
	//-------------------------------------------------------------------------
	void SetTextInputActive( bool bActive ) override;
	bool IsTextInputActive() const override;
	const TArray<FTextInputEvent>& GetTextInputEvents() const override;
	const FImeComposition& GetImeComposition() const override;

	//-------------------------------------------------------------------------
	// ICursorControl / ITouchInput（DxLibInputCursorTouch.cpp）
	//-------------------------------------------------------------------------
	bool SetCursorVisible( bool bVisible ) override;
	bool IsCursorVisible() const override;
	bool SetCursorPosition( int32 X, int32 Y ) override;
	bool IsTouchAvailable() const override;
	const TArray<FTouchPoint>& GetTouches() const override;
	const TArray<uint32>& GetReleasedTouchIds() const override;

private:
	// 毎フレームの取り込み（Tick から呼ぶ）
	void UpdateText();
	void UpdateTouch();

	static constexpr int32 NumDxKeys = 256;

	static bool IsDown( const char ( &State )[NumDxKeys], EKey Key );
	static uint32 ToButtonBit( EMouseButton Button );

	char Current[NumDxKeys] = {};
	char Previous[NumDxKeys] = {};
	FMouseState MouseState;
	bool bHadValidMousePos = false;

	// 文字入力
	bool bTextInputActive = false;
	TArray<FTextInputEvent> TextEvents;
	FImeComposition ImeComposition;
	FUtf8ByteAssembler ByteAssembler;

	// カーソルとタッチ
	bool bCursorVisible = true;
	FTouchTracker TouchTracker;
};
