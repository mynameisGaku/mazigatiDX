#include "DxLibInput.h"

#include <cstring>

#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

DEFINE_LOG_CATEGORY_STATIC( LogInput, Log )

REGISTER_SUBSYSTEM( CDxLibInput )

namespace
{
	// EKey と同じ並び
	constexpr int DxKeyCodes[] = {
		KEY_INPUT_A, KEY_INPUT_B, KEY_INPUT_C, KEY_INPUT_D, KEY_INPUT_E, KEY_INPUT_F, KEY_INPUT_G,
		KEY_INPUT_H, KEY_INPUT_I, KEY_INPUT_J, KEY_INPUT_K, KEY_INPUT_L, KEY_INPUT_M, KEY_INPUT_N,
		KEY_INPUT_O, KEY_INPUT_P, KEY_INPUT_Q, KEY_INPUT_R, KEY_INPUT_S, KEY_INPUT_T, KEY_INPUT_U,
		KEY_INPUT_V, KEY_INPUT_W, KEY_INPUT_X, KEY_INPUT_Y, KEY_INPUT_Z,
		KEY_INPUT_0, KEY_INPUT_1, KEY_INPUT_2, KEY_INPUT_3, KEY_INPUT_4,
		KEY_INPUT_5, KEY_INPUT_6, KEY_INPUT_7, KEY_INPUT_8, KEY_INPUT_9,
		KEY_INPUT_F1, KEY_INPUT_F2, KEY_INPUT_F3, KEY_INPUT_F4, KEY_INPUT_F5, KEY_INPUT_F6,
		KEY_INPUT_F7, KEY_INPUT_F8, KEY_INPUT_F9, KEY_INPUT_F10, KEY_INPUT_F11, KEY_INPUT_F12,
		KEY_INPUT_UP, KEY_INPUT_DOWN, KEY_INPUT_LEFT, KEY_INPUT_RIGHT,
		KEY_INPUT_SPACE, KEY_INPUT_RETURN, KEY_INPUT_ESCAPE, KEY_INPUT_TAB, KEY_INPUT_BACK,
		KEY_INPUT_LSHIFT, KEY_INPUT_RSHIFT, KEY_INPUT_LCONTROL, KEY_INPUT_RCONTROL, KEY_INPUT_LALT, KEY_INPUT_RALT,
		KEY_INPUT_PGUP, KEY_INPUT_PGDN, KEY_INPUT_END, KEY_INPUT_HOME, KEY_INPUT_INSERT, KEY_INPUT_DELETE,
		KEY_INPUT_MINUS, KEY_INPUT_YEN, KEY_INPUT_PREVTRACK, KEY_INPUT_PERIOD, KEY_INPUT_SLASH,
		KEY_INPUT_SCROLL, KEY_INPUT_SEMICOLON, KEY_INPUT_COLON, KEY_INPUT_LBRACKET, KEY_INPUT_RBRACKET,
		KEY_INPUT_AT, KEY_INPUT_BACKSLASH, KEY_INPUT_COMMA,
		KEY_INPUT_KANJI, KEY_INPUT_CONVERT, KEY_INPUT_NOCONVERT, KEY_INPUT_KANA, KEY_INPUT_APPS,
		KEY_INPUT_CAPSLOCK, KEY_INPUT_SYSRQ, KEY_INPUT_PAUSE, KEY_INPUT_LWIN, KEY_INPUT_RWIN,
		KEY_INPUT_NUMLOCK,
		KEY_INPUT_NUMPAD0, KEY_INPUT_NUMPAD1, KEY_INPUT_NUMPAD2, KEY_INPUT_NUMPAD3, KEY_INPUT_NUMPAD4,
		KEY_INPUT_NUMPAD5, KEY_INPUT_NUMPAD6, KEY_INPUT_NUMPAD7, KEY_INPUT_NUMPAD8, KEY_INPUT_NUMPAD9,
		KEY_INPUT_MULTIPLY, KEY_INPUT_ADD, KEY_INPUT_SUBTRACT, KEY_INPUT_DECIMAL, KEY_INPUT_DIVIDE, KEY_INPUT_NUMPADENTER,
	};
	static_assert( sizeof( DxKeyCodes ) / sizeof( DxKeyCodes[0] ) == static_cast<int>( EKey::Count ), "DxKeyCodes must match EKey" );
}

void CDxLibInput::Initialize( FSubsystemCollection& Collection )
{
	// DxLib の初期化後に状態を読むので、プラットフォームを先に初期化させる
	if ( Collection.InitializeDependency<IPlatform>() == nullptr )
	{
		LOG( LogInput, Error, "No platform is available. Keyboard and mouse input will not work." );
		return;
	}
	LOG( LogInput, Log, "Keyboard and mouse input ready (%d keys, %d mouse buttons)", static_cast<int32>( EKey::Count ), static_cast<int32>( EMouseButton::Count ) );
}

void CDxLibInput::Tick( float32 DeltaTime )
{
	const HWND MainWindow = GetMainWindowHandle();
	const bool bFocused = ( MainWindow != nullptr ) && ( GetForegroundWindow() == MainWindow );

	FMemory::Memcpy( Previous, Current, sizeof( Current ) );
	if ( bFocused )
	{
		GetHitKeyStateAll( Current );
	}
	else
	{
		FMemory::Memset( Current, 0, sizeof( Current ) );
	}

	// ホイール回転量は非フォーカス中も読み捨てておき、復帰時にまとめて飛ばないようにする
	const float32 RawWheelY = GetMouseWheelRotVolF( TRUE );
	const float32 RawWheelX = GetMouseHWheelRotVolF( TRUE );
	const uint32 PrevButtons = MouseState.Buttons;

	MouseState.bFocused = bFocused;
	MouseState.bHorizontalWheelSupported = true;

	if ( bFocused )
	{
		int MouseX = 0;
		int MouseY = 0;
		if ( GetMousePoint( &MouseX, &MouseY ) == 0 )
		{
			MouseState.DeltaX = bHadValidMousePos ? ( MouseX - MouseState.X ) : 0;
			MouseState.DeltaY = bHadValidMousePos ? ( MouseY - MouseState.Y ) : 0;
			MouseState.X = MouseX;
			MouseState.Y = MouseY;
			MouseState.bPositionValid = true;
			bHadValidMousePos = true;
		}
		else
		{
			MouseState.DeltaX = 0;
			MouseState.DeltaY = 0;
			MouseState.bPositionValid = false;
			bHadValidMousePos = false;
		}

		const uint32 CurrentButtons = static_cast<uint32>( GetMouseInput() ) & 0xFFu;
		MouseState.Buttons = CurrentButtons;
		MouseState.PressedButtons = CurrentButtons & ~PrevButtons;
		MouseState.ReleasedButtons = PrevButtons & ~CurrentButtons;
		MouseState.WheelX = RawWheelX;
		MouseState.WheelY = RawWheelY;
	}
	else
	{
		MouseState.DeltaX = 0;
		MouseState.DeltaY = 0;
		MouseState.WheelX = 0.0f;
		MouseState.WheelY = 0.0f;
		MouseState.Buttons = 0;
		MouseState.PressedButtons = 0;
		MouseState.ReleasedButtons = PrevButtons;
		MouseState.bPositionValid = false;
		bHadValidMousePos = false;
	}
}

bool CDxLibInput::IsDown( const char ( &State )[NumDxKeys], EKey Key )
{
	const int32 Index = static_cast<int32>( Key );
	if ( Index < 0 || Index >= static_cast<int32>( EKey::Count ) ) return false;
	return State[DxKeyCodes[Index]] != 0;
}

uint32 CDxLibInput::ToButtonBit( EMouseButton Button )
{
	const uint8 Index = static_cast<uint8>( Button );
	if ( Index >= static_cast<uint8>( EMouseButton::Count ) ) return 0u;
	return 1u << Index;
}

#pragma region VirtualDefaults

// 仮想関数は vtable 経由で呼ばれインライン展開できないので、ヘッダーではなくここで定義する

ETickGroup CDxLibInput::GetTickGroup() const
{
	return ETickGroup::PreUpdate;
}

bool CDxLibInput::IsKeyDown( EKey Key ) const
{
	return IsDown( Current, Key );
}

bool CDxLibInput::IsKeyPressed( EKey Key ) const
{
	return IsDown( Current, Key ) && !IsDown( Previous, Key );
}

bool CDxLibInput::IsKeyReleased( EKey Key ) const
{
	return !IsDown( Current, Key ) && IsDown( Previous, Key );
}

const FMouseState& CDxLibInput::GetMouseState() const
{
	return MouseState;
}

bool CDxLibInput::IsMouseButtonDown( EMouseButton Button ) const
{
	return ( MouseState.Buttons & ToButtonBit( Button ) ) != 0u;
}

bool CDxLibInput::IsMouseButtonPressed( EMouseButton Button ) const
{
	return ( MouseState.PressedButtons & ToButtonBit( Button ) ) != 0u;
}

bool CDxLibInput::IsMouseButtonReleased( EMouseButton Button ) const
{
	return ( MouseState.ReleasedButtons & ToButtonBit( Button ) ) != 0u;
}

#pragma endregion
