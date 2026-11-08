// SPDX-License-Identifier: MIT
#include "UIInputBridge.h"

namespace
{
	bool AnyKeyPressed( const IKeyboardInput* Keyboard, const TArray<EKey>& Keys )
	{
		if ( Keyboard == nullptr )
		{
			return false;
		}
		for ( int32 Index = 0; Index < Keys.Num(); ++Index )
		{
			if ( Keyboard->IsKeyPressed( Keys[Index] ) )
			{
				return true;
			}
		}
		return false;
	}

	bool AnyKeyDown( const IKeyboardInput* Keyboard, const TArray<EKey>& Keys )
	{
		if ( Keyboard == nullptr )
		{
			return false;
		}
		for ( int32 Index = 0; Index < Keys.Num(); ++Index )
		{
			if ( Keyboard->IsKeyDown( Keys[Index] ) )
			{
				return true;
			}
		}
		return false;
	}

	// つながっているどれかのパッドで、ボタンが、このフレームで押されたか
	bool AnyGamepadPressed( const IGamepadInput* Gamepad, EGamepadButton Button )
	{
		if ( Gamepad == nullptr || !Gamepad->IsAvailable() )
		{
			return false;
		}
		const TArray<FGamepadInfo>& Devices = Gamepad->GetDevices();
		for ( int32 Index = 0; Index < Devices.Num(); ++Index )
		{
			const FGamepadState* State = Gamepad->GetState( Devices[Index].Id );
			if ( State != nullptr && State->bConnected && State->IsPressed( Button ) )
			{
				return true;
			}
		}
		return false;
	}
}

FUIInputFrame NUIInput::MakeFrame( const IKeyboardInput* Keyboard, const IMouseInput* Mouse, const IGamepadInput* Gamepad, const FUIInputMapping& Mapping )
{
	FUIInputFrame Frame;

	// ポインター: 位置が取れて、ゲームのウィンドウに入力の対象があるときだけ
	Frame.bPointerValid = false;
	if ( Mouse != nullptr )
	{
		const FMouseState& State = Mouse->GetMouseState();
		Frame.bPointerValid = State.bPositionValid && State.bFocused;
		Frame.PointerPosition = FVector2( static_cast<float32>( State.X ), static_cast<float32>( State.Y ) );
		Frame.bPointerDown = Frame.bPointerValid && Mouse->IsMouseButtonDown( EMouseButton::Left );
		Frame.bPointerSecondaryDown = Frame.bPointerValid && Mouse->IsMouseButtonDown( EMouseButton::Right );
		Frame.WheelDelta = Frame.bPointerValid ? State.WheelY : 0.0f;
	}

	// キーボード
	Frame.bNavigateUp = AnyKeyPressed( Keyboard, Mapping.NavigateUpKeys );
	Frame.bNavigateDown = AnyKeyPressed( Keyboard, Mapping.NavigateDownKeys );
	Frame.bNavigateLeft = AnyKeyPressed( Keyboard, Mapping.NavigateLeftKeys );
	Frame.bNavigateRight = AnyKeyPressed( Keyboard, Mapping.NavigateRightKeys );
	Frame.bConfirm = AnyKeyPressed( Keyboard, Mapping.ConfirmKeys );
	Frame.bCancel = AnyKeyPressed( Keyboard, Mapping.CancelKeys );
	if ( AnyKeyPressed( Keyboard, Mapping.FocusKeys ) )
	{
		const bool bShift = AnyKeyDown( Keyboard, Mapping.FocusPreviousModifierKeys );
		Frame.bFocusNext = !bShift;
		Frame.bFocusPrevious = bShift;
	}

	// パッド
	if ( Mapping.bUseGamepad )
	{
		Frame.bNavigateUp = Frame.bNavigateUp || AnyGamepadPressed( Gamepad, EGamepadButton::DPadUp );
		Frame.bNavigateDown = Frame.bNavigateDown || AnyGamepadPressed( Gamepad, EGamepadButton::DPadDown );
		Frame.bNavigateLeft = Frame.bNavigateLeft || AnyGamepadPressed( Gamepad, EGamepadButton::DPadLeft );
		Frame.bNavigateRight = Frame.bNavigateRight || AnyGamepadPressed( Gamepad, EGamepadButton::DPadRight );
		Frame.bConfirm = Frame.bConfirm || AnyGamepadPressed( Gamepad, EGamepadButton::South );
		Frame.bCancel = Frame.bCancel || AnyGamepadPressed( Gamepad, EGamepadButton::East );
		Frame.bFocusNext = Frame.bFocusNext || AnyGamepadPressed( Gamepad, EGamepadButton::RightShoulder );
		Frame.bFocusPrevious = Frame.bFocusPrevious || AnyGamepadPressed( Gamepad, EGamepadButton::LeftShoulder );
	}
	return Frame;
}
