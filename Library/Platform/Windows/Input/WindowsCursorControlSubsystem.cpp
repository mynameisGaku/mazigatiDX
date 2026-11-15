// SPDX-License-Identifier: MIT
#include "WindowsCursorControlSubsystem.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/System/WindowsInclude.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"

REGISTER_SUBSYSTEM( CWindowsCursorControlSubsystem )

bool CWindowsCursorControlSubsystem::ShouldCreateSubsystem( CEngine& ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DX12;
}

void CWindowsCursorControlSubsystem::Initialize( FSubsystemCollection& Collection )
{
	Window = Collection.InitializeDependency<INativeWindow>();
	Platform = Collection.InitializeDependency<IPlatform>();
}

void CWindowsCursorControlSubsystem::Deinitialize()
{
	Window = nullptr;
	Platform = nullptr;
}

bool CWindowsCursorControlSubsystem::SetCursorVisible( bool bVisible )
{
	if ( Platform == nullptr )
	{
		return false;
	}
	Platform->SetMouseCursorVisible( bVisible );
	return true;
}

bool CWindowsCursorControlSubsystem::IsCursorVisible() const
{
	return Platform == nullptr || Platform->IsMouseCursorVisible();
}

bool CWindowsCursorControlSubsystem::SetCursorPosition( int32 X, int32 Y )
{
	const HWND Handle = Window != nullptr ? static_cast<HWND>( Window->GetNativeWindow() ) : nullptr;
	if ( Handle == nullptr || Platform == nullptr || GetForegroundWindow() != Handle )
	{
		return false;
	}
	const int32 Width = Platform->GetScreenWidth();
	const int32 Height = Platform->GetScreenHeight();
	RECT Client = {};
	if ( Width <= 0 || Height <= 0 || !GetClientRect( Handle, &Client ) || Client.right <= 0 || Client.bottom <= 0 )
	{
		return false;
	}
	// 論理解像度の座標を、クライアント領域の座標へ（IMouseInput の逆）
	POINT Point = {};
	Point.x = static_cast<LONG>( static_cast<int64>( X ) * Client.right / Width );
	Point.y = static_cast<LONG>( static_cast<int64>( Y ) * Client.bottom / Height );
	return ClientToScreen( Handle, &Point ) && SetCursorPos( Point.x, Point.y );
}
