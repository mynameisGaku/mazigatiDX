// SPDX-License-Identifier: MIT
#include "DxLibWindowControlSubsystem.h"

#include "Library/Core/Interfaces/Platform/IDisplayInfo.h"
#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/DxLib/DxLibBackendSubsystem.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/System/DisplayLayout.h"

REGISTER_SUBSYSTEM( CDxLibWindowControlSubsystem )

namespace
{
	// 位置と倍率を変えられるのは、通常のウィンドウのときだけ
	bool IsWindowed()
	{
		const IPlatform* Platform = GetAppSubsystem<IPlatform>();
		return Platform != nullptr && Platform->GetWindowMode() == EWindowMode::Windowed;
	}
}

bool CDxLibWindowControlSubsystem::ShouldCreateSubsystem( CEngine& Engine ) const
{
	return NBackendSelection::GetRequested() == EPlatformBackend::DxLib;
}

void CDxLibWindowControlSubsystem::Initialize( FSubsystemCollection& Collection )
{
	// ウィンドウができたあとに使うので、プラットフォームとディスプレイの情報を先に初期化させる
	Collection.InitializeDependency<IPlatform>();
	Collection.InitializeDependency<IDisplayInfo>();
}

bool CDxLibWindowControlSubsystem::GetWindowPosition( int32& OutX, int32& OutY ) const
{
	const HWND Window = GetMainWindowHandle();
	RECT Rect = {};
	if ( Window == nullptr || !GetWindowRect( Window, &Rect ) )
	{
		return false;
	}
	OutX = Rect.left;
	OutY = Rect.top;
	return true;
}

bool CDxLibWindowControlSubsystem::SetWindowPosition( int32 X, int32 Y )
{
	const HWND Window = GetMainWindowHandle();
	if ( Window == nullptr || !IsWindowed() )
	{
		return false;
	}
	return SetWindowPos( Window, nullptr, X, Y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE ) != FALSE;
}

bool CDxLibWindowControlSubsystem::MoveWindowToDisplay( int32 DisplayIndex )
{
	const HWND Window = GetMainWindowHandle();
	const IDisplayInfo* Displays = GetAppSubsystem<IDisplayInfo>();
	RECT Rect = {};
	if ( Window == nullptr || Displays == nullptr || !IsWindowed() || !GetWindowRect( Window, &Rect ) )
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

float64 CDxLibWindowControlSubsystem::GetWindowScale() const
{
	const CDxLibBackendSubsystem* Backend = GetAppSubsystem<CDxLibBackendSubsystem>();
	return Backend != nullptr ? Backend->GetSettings().WindowExtendRate : 1.0;
}

bool CDxLibWindowControlSubsystem::SetWindowScale( float64 Scale )
{
	CDxLibBackendSubsystem* Backend = GetAppSubsystem<CDxLibBackendSubsystem>();
	if ( Backend == nullptr || !( Scale > 0.0 ) || !IsWindowed() )
	{
		return false;
	}
	Backend->SetWindowExtendRate( Scale );
	return true;
}

bool CDxLibWindowControlSubsystem::IsWindowFocused() const
{
	const HWND Window = GetMainWindowHandle();
	return Window != nullptr && GetForegroundWindow() == Window;
}
