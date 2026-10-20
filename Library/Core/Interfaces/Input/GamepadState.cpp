#include "GamepadState.h"

bool FGamepadState::IsDown( EGamepadButton Button ) const
{
	// 問い合わせ対象の配列位置。
	const int32 Index = static_cast<int32>( Button );
	return Index < static_cast<int32>( EGamepadButton::Count ) && Buttons[Index];
}

bool FGamepadState::IsPressed( EGamepadButton Button ) const
{
	// 問い合わせ対象の配列位置。
	const int32 Index = static_cast<int32>( Button );
	return Index < static_cast<int32>( EGamepadButton::Count ) && PressedButtons[Index];
}

bool FGamepadState::IsReleased( EGamepadButton Button ) const
{
	// 問い合わせ対象の配列位置。
	const int32 Index = static_cast<int32>( Button );
	return Index < static_cast<int32>( EGamepadButton::Count ) && ReleasedButtons[Index];
}

float32 FGamepadState::GetAxis( EGamepadAxis Axis ) const
{
	// 問い合わせ対象の配列位置。
	const int32 Index = static_cast<int32>( Axis );
	return Index < static_cast<int32>( EGamepadAxis::Count ) ? Axes[Index] : 0.0f;
}
