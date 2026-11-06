// SPDX-License-Identifier: MIT
#include "NullDisplayInfoSubsystem.h"

#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Platform/BackendSelection.h"

REGISTER_SUBSYSTEM( CNullDisplayInfoSubsystem )

CNullDisplayInfoSubsystem::CNullDisplayInfoSubsystem()
{
	FDisplayInfo Primary;
	Primary.Name = "Null Display";
	Primary.Width = 1920;
	Primary.Height = 1080;
	Primary.RefreshRate = 60;
	Primary.bPrimary = true;
	Primary.DpiScale = 1.0f;
	SetDisplays( TArray<FDisplayInfo>( { Primary } ) );
}

bool CNullDisplayInfoSubsystem::ShouldCreateSubsystem( CEngine& Engine ) const
{
	return NBackendSelection::IsNullRequested();
}

int32 CNullDisplayInfoSubsystem::GetDisplayCount() const
{
	return Displays.Num();
}

int32 CNullDisplayInfoSubsystem::GetPrimaryDisplayIndex() const
{
	for ( int32 Index = 0; Index < Displays.Num(); ++Index )
	{
		if ( Displays[Index].bPrimary )
		{
			return Index;
		}
	}
	return -1;
}

bool CNullDisplayInfoSubsystem::GetDisplay( int32 Index, FDisplayInfo& Out ) const
{
	if ( !Displays.IsValidIndex( Index ) )
	{
		return false;
	}
	Out = Displays[Index];
	return true;
}

bool CNullDisplayInfoSubsystem::GetDisplayModes( int32 Index, TArray<FDisplayMode>& Out ) const
{
	if ( !Modes.IsValidIndex( Index ) || Modes[Index].IsEmpty() )
	{
		return false;
	}
	Out = Modes[Index];
	return true;
}

int32 CNullDisplayInfoSubsystem::GetWindowDisplayIndex() const
{
	return Displays.IsValidIndex( WindowDisplayIndex ) ? WindowDisplayIndex : -1;
}

float32 CNullDisplayInfoSubsystem::GetWindowDpiScale() const
{
	return WindowDpiScale;
}

void CNullDisplayInfoSubsystem::SetDisplays( const TArray<FDisplayInfo>& InDisplays )
{
	Displays = InDisplays;
	Modes.Reset();
	for ( const FDisplayInfo& Display : Displays )
	{
		FDisplayMode Mode;
		Mode.Width = Display.Width;
		Mode.Height = Display.Height;
		Mode.RefreshRate = Display.RefreshRate;
		Modes.Add( TArray<FDisplayMode>( { Mode } ) );
	}
}

void CNullDisplayInfoSubsystem::SetDisplayModes( int32 Index, const TArray<FDisplayMode>& InModes )
{
	if ( Modes.IsValidIndex( Index ) )
	{
		Modes[Index] = InModes;
	}
}

void CNullDisplayInfoSubsystem::SetWindowDisplayIndex( int32 Index )
{
	WindowDisplayIndex = Index;
}

void CNullDisplayInfoSubsystem::SetWindowDpiScale( float32 Scale )
{
	WindowDpiScale = Scale;
}
