// SPDX-License-Identifier: MIT
#include "WindowsTouchInputSubsystem.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/System/WindowsInclude.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"

REGISTER_SUBSYSTEM( CWindowsTouchInputSubsystem )

bool CWindowsTouchInputSubsystem::ShouldCreateSubsystem( CEngine& ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DX12;
}

void CWindowsTouchInputSubsystem::Initialize( FSubsystemCollection& Collection )
{
	Bind( Collection.InitializeDependency<INativeWindow>(), Collection.InitializeDependency<IPlatform>() );
}

void CWindowsTouchInputSubsystem::Bind( INativeWindow* InWindow, IPlatform* InPlatform )
{
	Window = InWindow;
	Platform = InPlatform;
	HWND Handle = Window != nullptr ? static_cast<HWND>( Window->GetNativeWindow() ) : nullptr;
	if ( Handle != nullptr && IsWindow( Handle ) )
	{
		bRegistered = RegisterTouchWindow( Handle, 0 ) != FALSE;
		Window->AddMessageObserver( this );
	}
}

void CWindowsTouchInputSubsystem::Deinitialize()
{
	if ( Window != nullptr )
	{
		Window->RemoveMessageObserver( this );
		HWND Handle = static_cast<HWND>( Window->GetNativeWindow() );
		if ( bRegistered && Handle != nullptr && IsWindow( Handle ) )
		{
			UnregisterTouchWindow( Handle );
		}
	}
	Window = nullptr;
	Platform = nullptr;
	bRegistered = false;
	Active.Reset();
	Tracker.Reset();
}

void CWindowsTouchInputSubsystem::Tick( float32 )
{
	// 離された指は、1 フレームだけ見せてから外す（Tracker は、前のフレームとの差で、離された ID を出す）
	TArray<FTouchPoint> Raw;
	for ( const FActive& Entry : Active )
	{
		if ( !Entry.bEnded )
		{
			Raw.Add( Entry.Point );
		}
	}
	Tracker.Update( Raw );
	for ( int32 Index = Active.Num() - 1; Index >= 0; --Index )
	{
		if ( Active[Index].bEnded )
		{
			Active.RemoveAt( Index );
		}
	}
}

bool CWindowsTouchInputSubsystem::IsTouchAvailable() const
{
	return bRegistered;
}

const TArray<FTouchPoint>& CWindowsTouchInputSubsystem::GetTouches() const
{
	return Tracker.GetTouches();
}

const TArray<uint32>& CWindowsTouchInputSubsystem::GetReleasedTouchIds() const
{
	return Tracker.GetReleasedIds();
}

void CWindowsTouchInputSubsystem::AddContact( uint32 Id, int32 X, int32 Y, EContact Contact )
{
	for ( FActive& Entry : Active )
	{
		if ( Entry.Point.Id == Id && !Entry.bEnded )
		{
			Entry.Point.X = X;
			Entry.Point.Y = Y;
			Entry.bEnded = Contact == EContact::Up;
			return;
		}
	}
	if ( Contact == EContact::Up )
	{
		return; // 知らない指が離れた
	}
	FActive Entry;
	Entry.Point.Id = Id;
	Entry.Point.X = X;
	Entry.Point.Y = Y;
	Active.Add( Entry );
}

bool CWindowsTouchInputSubsystem::OnNativeWindowMessage( void* WindowHandle, uint32 Message, uint64 WParam, int64 LParam, int64& OutResult )
{
	if ( Message != WM_TOUCH )
	{
		return false;
	}
	const HWND Handle = static_cast<HWND>( WindowHandle );
	const UINT Count = LOWORD( WParam );
	TArray<TOUCHINPUT> Inputs;
	Inputs.SetNum( static_cast<int32>( Count ), TOUCHINPUT() );
	RECT Client = {};
	if ( Count > 0 && Count <= 64 && GetTouchInputInfo( reinterpret_cast<HTOUCHINPUT>( LParam ), Count, Inputs.GetData(), sizeof( TOUCHINPUT ) ) )
	{
		const bool bCanMap = Platform != nullptr && GetClientRect( Handle, &Client ) && Client.right > 0 && Client.bottom > 0;
		for ( const TOUCHINPUT& Input : Inputs )
		{
			POINT Point = { TOUCH_COORD_TO_PIXEL( Input.x ), TOUCH_COORD_TO_PIXEL( Input.y ) }; // 画面の座標（画素）
			ScreenToClient( Handle, &Point );
			const int32 X = bCanMap ? static_cast<int32>( static_cast<int64>( Point.x ) * Platform->GetScreenWidth() / Client.right ) : Point.x;
			const int32 Y = bCanMap ? static_cast<int32>( static_cast<int64>( Point.y ) * Platform->GetScreenHeight() / Client.bottom ) : Point.y;
			EContact Contact = EContact::Move;
			if ( Input.dwFlags & TOUCHEVENTF_DOWN )
			{
				Contact = EContact::Down;
			}
			else if ( Input.dwFlags & TOUCHEVENTF_UP )
			{
				Contact = EContact::Up;
			}
			AddContact( Input.dwID, X, Y, Contact );
		}
	}
	CloseTouchInputHandle( reinterpret_cast<HTOUCHINPUT>( LParam ) );
	OutResult = 0;
	return true;
}
