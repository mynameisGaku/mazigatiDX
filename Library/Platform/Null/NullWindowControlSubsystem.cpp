// SPDX-License-Identifier: MIT
#include "NullWindowControlSubsystem.h"

#include "Library/Core/Interfaces/Platform/IDisplayInfo.h"
#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Platform/BackendSelection.h"
#include "Library/Platform/System/DisplayLayout.h"

REGISTER_SUBSYSTEM( CNullWindowControlSubsystem )

namespace
{
	// 位置と倍率を変えられるか（IPlatform が無ければ変えられる。あれば、通常のウィンドウのときだけ）
	bool CanChangeWindow()
	{
		const IPlatform* Platform = GetAppSubsystem<IPlatform>();
		return Platform == nullptr || Platform->GetWindowMode() == EWindowMode::Windowed;
	}
}

bool CNullWindowControlSubsystem::ShouldCreateSubsystem( CEngine& Engine ) const
{
	return NBackendSelection::IsNullRequested();
}

bool CNullWindowControlSubsystem::GetWindowPosition( int32& OutX, int32& OutY ) const
{
	OutX = PositionX;
	OutY = PositionY;
	return true;
}

bool CNullWindowControlSubsystem::SetWindowPosition( int32 X, int32 Y )
{
	if ( !CanChangeWindow() )
	{
		return false;
	}
	PositionX = X;
	PositionY = Y;
	return true;
}

bool CNullWindowControlSubsystem::MoveWindowToDisplay( int32 DisplayIndex )
{
	const IDisplayInfo* Displays = GetAppSubsystem<IDisplayInfo>();
	if ( Displays == nullptr || !CanChangeWindow() )
	{
		return false;
	}
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
	if ( !Layout.GetCenteredPosition( DisplayIndex, WindowWidth, WindowHeight, X, Y ) )
	{
		return false;
	}
	return SetWindowPosition( X, Y );
}

float64 CNullWindowControlSubsystem::GetWindowScale() const
{
	return Scale;
}

bool CNullWindowControlSubsystem::SetWindowScale( float64 InScale )
{
	if ( !( InScale > 0.0 ) || !CanChangeWindow() )
	{
		return false;
	}
	Scale = InScale;
	return true;
}

bool CNullWindowControlSubsystem::IsWindowFocused() const
{
	return bFocused;
}

void CNullWindowControlSubsystem::SetWindowFocused( bool bInFocused )
{
	bFocused = bInFocused;
}

void CNullWindowControlSubsystem::SetWindowSize( int32 Width, int32 Height )
{
	WindowWidth = Width;
	WindowHeight = Height;
}
