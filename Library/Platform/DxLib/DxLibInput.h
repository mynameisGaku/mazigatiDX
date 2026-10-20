#pragma once
#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CDxLibInput
//   IInput の DxLib 実装。毎フレームの頭 (PreUpdate) にキーボードの状態を取り込む。
//   ゲーム側は GetAppSubsystem<IInput>() で取得する。
//=============================================================================
class CDxLibInput : public CAppSubsystem, public IInput, public ITickable
{
public:
	//-------------------------------------------------------------------------
	// CAppSubsystem
	//-------------------------------------------------------------------------
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

private:
	static constexpr int32 NumDxKeys = 256;

	static bool IsDown( const char ( &State )[NumDxKeys], EKey Key );
	static uint32 ToButtonBit( EMouseButton Button );

	char Current[NumDxKeys] = {};
	char Previous[NumDxKeys] = {};
	FMouseState MouseState;
	bool bHadValidMousePos = false;
};
