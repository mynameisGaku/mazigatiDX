// SPDX-License-Identifier: MIT
#include "NullInput.h"

#include "Library/Core/Memory/Memory.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Platform/BackendSelection.h"

REGISTER_SUBSYSTEM( CNullInput )

namespace
{
	// ボタンの番号をビットにする。範囲の外は 0
	uint32 ToButtonBit( EMouseButton Button )
	{
		const uint8 Index = static_cast<uint8>( Button );
		if ( Index >= static_cast<uint8>( EMouseButton::Count ) )
		{
			return 0u;
		}
		return 1u << Index;
	}

	// キーの番号。範囲の外は -1
	int32 ToKeyIndex( EKey Key )
	{
		const int32 Index = static_cast<int32>( Key );
		if ( Index < 0 || Index >= static_cast<int32>( EKey::Count ) )
		{
			return -1;
		}
		return Index;
	}
}

bool CNullInput::ShouldCreateSubsystem( CEngine& Engine ) const
{
	return NBackendSelection::IsNullRequested();
}

void CNullInput::Tick( float32 DeltaTime )
{
	FMemory::Memcpy( PreviousKeys, CurrentKeys, sizeof( CurrentKeys ) );
	FMemory::Memcpy( CurrentKeys, PendingKeys, sizeof( CurrentKeys ) );

	const uint32 PreviousButtons = MouseState.Buttons;
	MouseState.bFocused = true;
	MouseState.bHorizontalWheelSupported = true;
	MouseState.bPositionValid = true;
	MouseState.DeltaX = bHadMouseTick ? PendingX - MouseState.X : 0;
	MouseState.DeltaY = bHadMouseTick ? PendingY - MouseState.Y : 0;
	MouseState.X = PendingX;
	MouseState.Y = PendingY;
	MouseState.Buttons = PendingButtons;
	MouseState.PressedButtons = PendingButtons & ~PreviousButtons;
	MouseState.ReleasedButtons = PreviousButtons & ~PendingButtons;
	MouseState.WheelX = PendingWheelX;
	MouseState.WheelY = PendingWheelY;
	bHadMouseTick = true;

	// ホイールは 1 フレームだけ有効
	PendingWheelX = 0.0f;
	PendingWheelY = 0.0f;
}

ETickGroup CNullInput::GetTickGroup() const
{
	return ETickGroup::PreUpdate;
}

bool CNullInput::IsKeyDown( EKey Key ) const
{
	const int32 Index = ToKeyIndex( Key );
	return Index >= 0 && CurrentKeys[Index];
}

bool CNullInput::IsKeyPressed( EKey Key ) const
{
	const int32 Index = ToKeyIndex( Key );
	return Index >= 0 && CurrentKeys[Index] && !PreviousKeys[Index];
}

bool CNullInput::IsKeyReleased( EKey Key ) const
{
	const int32 Index = ToKeyIndex( Key );
	return Index >= 0 && !CurrentKeys[Index] && PreviousKeys[Index];
}

const FMouseState& CNullInput::GetMouseState() const
{
	return MouseState;
}

bool CNullInput::IsMouseButtonDown( EMouseButton Button ) const
{
	return ( MouseState.Buttons & ToButtonBit( Button ) ) != 0u;
}

bool CNullInput::IsMouseButtonPressed( EMouseButton Button ) const
{
	return ( MouseState.PressedButtons & ToButtonBit( Button ) ) != 0u;
}

bool CNullInput::IsMouseButtonReleased( EMouseButton Button ) const
{
	return ( MouseState.ReleasedButtons & ToButtonBit( Button ) ) != 0u;
}

void CNullInput::SetKeyDown( EKey Key, bool bDown )
{
	const int32 Index = ToKeyIndex( Key );
	if ( Index >= 0 )
	{
		PendingKeys[Index] = bDown;
	}
}

void CNullInput::SetMouseButtonDown( EMouseButton Button, bool bDown )
{
	const uint32 Bit = ToButtonBit( Button );
	if ( Bit == 0u )
	{
		return;
	}
	if ( bDown )
	{
		PendingButtons |= Bit;
	}
	else
	{
		PendingButtons &= ~Bit;
	}
}

void CNullInput::SetMousePosition( int32 X, int32 Y )
{
	PendingX = X;
	PendingY = Y;
}

void CNullInput::AddMouseWheel( float32 WheelX, float32 WheelY )
{
	PendingWheelX += WheelX;
	PendingWheelY += WheelY;
}

void CNullInput::ReleaseAll()
{
	FMemory::Memset( PendingKeys, 0, sizeof( PendingKeys ) );
	PendingButtons = 0u;
}
