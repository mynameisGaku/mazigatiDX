// SPDX-License-Identifier: MIT
#include "WindowsInputSubsystem.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/System/WindowsInclude.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Core/Memory/Memory.h"

REGISTER_SUBSYSTEM( CWindowsInputSubsystem )

namespace
{
	// 共通キーからWindowsの物理キーへ変換する。非対応は0。
	int32 ToVirtualKey( EKey Key )
	{
		const int32 Value = static_cast<int32>( Key ); // 範囲ごとのキー位置。
		if ( Key >= EKey::A && Key <= EKey::Z )
		{
			return 'A' + Value - static_cast<int32>( EKey::A );
		}
		if ( Key >= EKey::Num0 && Key <= EKey::Num9 )
		{
			return '0' + Value - static_cast<int32>( EKey::Num0 );
		}
		if ( Key >= EKey::F1 && Key <= EKey::F12 )
		{
			return VK_F1 + Value - static_cast<int32>( EKey::F1 );
		}
		if ( Key >= EKey::Numpad0 && Key <= EKey::Numpad9 )
		{
			return VK_NUMPAD0 + Value - static_cast<int32>( EKey::Numpad0 );
		}
		switch ( Key )
		{
		case EKey::Up: return VK_UP;
		case EKey::Down: return VK_DOWN;
		case EKey::Left: return VK_LEFT;
		case EKey::Right: return VK_RIGHT;
		case EKey::Space: return VK_SPACE;
		case EKey::Enter: return VK_RETURN;
		case EKey::Escape: return VK_ESCAPE;
		case EKey::Tab: return VK_TAB;
		case EKey::BackSpace: return VK_BACK;
		case EKey::LeftShift: return VK_LSHIFT;
		case EKey::RightShift: return VK_RSHIFT;
		case EKey::LeftControl: return VK_LCONTROL;
		case EKey::RightControl: return VK_RCONTROL;
		case EKey::LeftAlt: return VK_LMENU;
		case EKey::RightAlt: return VK_RMENU;
		case EKey::PageUp: return VK_PRIOR;
		case EKey::PageDown: return VK_NEXT;
		case EKey::End: return VK_END;
		case EKey::Home: return VK_HOME;
		case EKey::Insert: return VK_INSERT;
		case EKey::Delete: return VK_DELETE;
		case EKey::Minus: return VK_OEM_MINUS;
		case EKey::Yen: return VK_OEM_5;
		case EKey::PreviousTrack: return VK_MEDIA_PREV_TRACK;
		case EKey::Period: return VK_OEM_PERIOD;
		case EKey::Slash: return VK_OEM_2;
		case EKey::ScrollLock: return VK_SCROLL;
		case EKey::Semicolon: return VK_OEM_PLUS;
		case EKey::Colon: return VK_OEM_1;
		case EKey::LeftBracket: return VK_OEM_4;
		case EKey::RightBracket: return VK_OEM_6;
		case EKey::At: return VK_OEM_3;
		case EKey::BackSlash: return VK_OEM_102;
		case EKey::Comma: return VK_OEM_COMMA;
		case EKey::Kanji: return VK_KANJI;
		case EKey::Convert: return VK_CONVERT;
		case EKey::NonConvert: return VK_NONCONVERT;
		case EKey::Kana: return VK_KANA;
		case EKey::Application: return VK_APPS;
		case EKey::CapsLock: return VK_CAPITAL;
		case EKey::PrintScreen: return VK_SNAPSHOT;
		case EKey::Pause: return VK_PAUSE;
		case EKey::LeftWindows: return VK_LWIN;
		case EKey::RightWindows: return VK_RWIN;
		case EKey::NumLock: return VK_NUMLOCK;
		case EKey::NumpadMultiply: return VK_MULTIPLY;
		case EKey::NumpadAdd: return VK_ADD;
		case EKey::NumpadSubtract: return VK_SUBTRACT;
		case EKey::NumpadDecimal: return VK_DECIMAL;
		case EKey::NumpadDivide: return VK_DIVIDE;
		default: return 0; // NumpadEnterはポーリングではEnterと区別できない。
		}
	}
	// 不正なボタンをシフト演算に渡さない。
	uint32 ButtonBit( EMouseButton Button )
	{
		return Button < EMouseButton::Count ? 1u << static_cast<uint8>( Button ) : 0;
	}
}

bool CWindowsInputSubsystem::ShouldCreateSubsystem( CEngine& ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DX12;
}

void CWindowsInputSubsystem::Initialize( FSubsystemCollection& Collection )
{
	Window = Collection.InitializeDependency<INativeWindow>();
	Platform = Collection.InitializeDependency<IPlatform>();
}

void CWindowsInputSubsystem::Deinitialize()
{
	Window = nullptr;
	Platform = nullptr;
	FMemory::Memzero( Current, sizeof( Current ) );
	FMemory::Memzero( Previous, sizeof( Previous ) );
	Mouse = {};
}

void CWindowsInputSubsystem::Tick( float32 )
{
	FMemory::Memcpy( Previous, Current, sizeof( Current ) );
	const HWND Handle = Window ? static_cast<HWND>( Window->GetNativeWindow() ) : nullptr; // 入力対象。
	const bool bFocused = Handle && GetForegroundWindow() == Handle; // 他のアプリの入力を読まない。
	for ( int32 Index = 0; Index < static_cast<int32>( EKey::Count ); ++Index )
	{
		const int32 NativeKey = ToVirtualKey( static_cast<EKey>( Index ) ); // OSのキー番号。
		Current[Index] = bFocused && NativeKey != 0 && ( GetAsyncKeyState( NativeKey ) & 0x8000 ) != 0;
	}
	const FMouseState Old = Mouse; // 差分と押下・解放の計算元。
	Mouse = {};
	Mouse.bFocused = bFocused;
	Mouse.bHorizontalWheelSupported = true;
	if ( bFocused && Platform )
	{
		POINT Point = {}; // OSのカーソル。
		RECT Client = {}; // 表示されるクライアント領域。
		if ( GetCursorPos( &Point ) && ScreenToClient( Handle, &Point ) && GetClientRect( Handle, &Client ) && Client.right > 0 && Client.bottom > 0 )
		{
			Mouse.X = static_cast<int32>( static_cast<int64>( Point.x ) * Platform->GetScreenWidth() / Client.right );
			Mouse.Y = static_cast<int32>( static_cast<int64>( Point.y ) * Platform->GetScreenHeight() / Client.bottom );
			Mouse.bPositionValid = true;
			if ( Old.bFocused && Old.bPositionValid )
			{
				Mouse.DeltaX = Mouse.X - Old.X;
				Mouse.DeltaY = Mouse.Y - Old.Y;
			}
		}
		const int32 Buttons[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 }; // OSが提供する5ボタン。
		for ( int32 Index = 0; Index < 5; ++Index )
		{
			if ( GetAsyncKeyState( Buttons[Index] ) & 0x8000 )
			{
				Mouse.Buttons |= 1u << Index;
			}
		}
		Window->GetWheel( Mouse.WheelX, Mouse.WheelY );
	}
	Mouse.PressedButtons = Mouse.Buttons & ~Old.Buttons;
	Mouse.ReleasedButtons = Old.Buttons & ~Mouse.Buttons;
}

bool CWindowsInputSubsystem::IsKeyDown( EKey Key ) const
{
	return Key < EKey::Count && Current[static_cast<int32>( Key )];
}

bool CWindowsInputSubsystem::IsKeyPressed( EKey Key ) const
{
	return IsKeyDown( Key ) && !Previous[static_cast<int32>( Key )];
}

bool CWindowsInputSubsystem::IsKeyReleased( EKey Key ) const
{
	return Key < EKey::Count && !Current[static_cast<int32>( Key )] && Previous[static_cast<int32>( Key )];
}

const FMouseState& CWindowsInputSubsystem::GetMouseState() const
{
	return Mouse;
}
bool CWindowsInputSubsystem::IsMouseButtonDown( EMouseButton Button ) const
{
	return ( Mouse.Buttons & ButtonBit( Button ) ) != 0;
}
bool CWindowsInputSubsystem::IsMouseButtonPressed( EMouseButton Button ) const
{
	return ( Mouse.PressedButtons & ButtonBit( Button ) ) != 0;
}
bool CWindowsInputSubsystem::IsMouseButtonReleased( EMouseButton Button ) const
{
	return ( Mouse.ReleasedButtons & ButtonBit( Button ) ) != 0;
}
