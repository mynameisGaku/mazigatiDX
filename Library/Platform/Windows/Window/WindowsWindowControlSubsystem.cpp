// SPDX-License-Identifier: MIT
#include "WindowsWindowControlSubsystem.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/System/DisplayLayout.h"
#include "Library/Platform/System/WindowsInclude.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"

REGISTER_SUBSYSTEM( CWindowsWindowControlSubsystem )

bool CWindowsWindowControlSubsystem::ShouldCreateSubsystem( CEngine& ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DX12;
}

void CWindowsWindowControlSubsystem::Initialize( FSubsystemCollection& Collection )
{
	// ウィンドウができたあとに使うので、プラットフォームとディスプレイの情報を先に初期化させる
	Bind( Collection.InitializeDependency<INativeWindow>(), Collection.InitializeDependency<IPlatform>(), Collection.InitializeDependency<IDisplayInfo>() );
}

void CWindowsWindowControlSubsystem::Deinitialize()
{
	Bind( nullptr, nullptr, nullptr );
}

void CWindowsWindowControlSubsystem::Bind( INativeWindow* InWindow, IPlatform* InPlatform, IDisplayInfo* InDisplays )
{
	Window = InWindow;
	Platform = InPlatform;
	Displays = InDisplays;
}

void* CWindowsWindowControlSubsystem::GetHandle() const
{
	HWND Handle = Window != nullptr ? static_cast<HWND>( Window->GetNativeWindow() ) : nullptr;
	return Handle != nullptr && IsWindow( Handle ) ? Handle : nullptr;
}

bool CWindowsWindowControlSubsystem::IsWindowed() const
{
	return Platform != nullptr && Platform->GetWindowMode() == EWindowMode::Windowed;
}

bool CWindowsWindowControlSubsystem::GetWindowPosition( int32& OutX, int32& OutY ) const
{
	RECT Rect = {};
	if ( GetHandle() == nullptr || !GetWindowRect( static_cast<HWND>( GetHandle() ), &Rect ) )
	{
		return false;
	}
	OutX = Rect.left;
	OutY = Rect.top;
	return true;
}

bool CWindowsWindowControlSubsystem::SetWindowPosition( int32 X, int32 Y )
{
	if ( GetHandle() == nullptr || !IsWindowed() )
	{
		return false;
	}
	return SetWindowPos( static_cast<HWND>( GetHandle() ), nullptr, X, Y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE ) != FALSE;
}

bool CWindowsWindowControlSubsystem::MoveWindowToDisplay( int32 DisplayIndex )
{
	RECT Rect = {};
	if ( GetHandle() == nullptr || Displays == nullptr || !IsWindowed() || !GetWindowRect( static_cast<HWND>( GetHandle() ), &Rect ) )
	{
		return false;
	}
	// ディスプレイの並びから、中央の位置を求める
	TArray<FDisplayInfo> List;
	for ( int32 Index = 0; Index < Displays->GetDisplayCount(); ++Index )
	{
		FDisplayInfo Info;
		if ( Displays->GetDisplay( Index, Info ) )
		{
			List.Add( Info );
		}
	}
	FDisplayLayout Layout;
	Layout.SetDisplays( List );
	int32 X = 0;
	int32 Y = 0;
	if ( !Layout.GetCenteredPosition( DisplayIndex, Rect.right - Rect.left, Rect.bottom - Rect.top, X, Y ) )
	{
		return false;
	}
	return SetWindowPosition( X, Y );
}

float64 CWindowsWindowControlSubsystem::GetWindowScale() const
{
	RECT Client = {};
	if ( GetHandle() == nullptr || Platform == nullptr || Platform->GetScreenWidth() <= 0 || !GetClientRect( static_cast<HWND>( GetHandle() ), &Client ) || Client.right <= 0 )
	{
		return 1.0;
	}
	return static_cast<float64>( Client.right ) / static_cast<float64>( Platform->GetScreenWidth() );
}

bool CWindowsWindowControlSubsystem::SetWindowScale( float64 Scale )
{
	if ( GetHandle() == nullptr || Platform == nullptr || !( Scale > 0.0 ) || Scale > 16.0 || !IsWindowed() )
	{
		return false;
	}
	const HWND Handle = static_cast<HWND>( GetHandle() );
	// ゲーム画面に倍率を掛けた大きさがクライアント領域になるよう、枠を足した窓の大きさにする（左上は動かさない）
	RECT Rect = { 0, 0, static_cast<LONG>( Platform->GetScreenWidth() * Scale + 0.5 ), static_cast<LONG>( Platform->GetScreenHeight() * Scale + 0.5 ) };
	if ( Rect.right <= 0 || Rect.bottom <= 0 )
	{
		return false;
	}
	const DWORD Style = static_cast<DWORD>( GetWindowLongPtrW( Handle, GWL_STYLE ) );
	const DWORD ExStyle = static_cast<DWORD>( GetWindowLongPtrW( Handle, GWL_EXSTYLE ) );
	if ( !AdjustWindowRectEx( &Rect, Style, FALSE, ExStyle ) )
	{
		return false;
	}
	return SetWindowPos( Handle, nullptr, 0, 0, Rect.right - Rect.left, Rect.bottom - Rect.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE ) != FALSE;
}

bool CWindowsWindowControlSubsystem::IsWindowFocused() const
{
	return GetHandle() != nullptr && GetForegroundWindow() == static_cast<HWND>( GetHandle() );
}
