// SPDX-License-Identifier: MIT
#include "DisplayLayout.h"

void FDisplayLayout::SetDisplays( const TArray<FDisplayInfo>& InDisplays )
{
	Displays = InDisplays;
}

int32 FDisplayLayout::GetPrimaryIndex() const
{
	for ( int32 Index = 0; Index < Displays.Num(); ++Index )
	{
		if ( Displays[Index].bPrimary )
		{
			return Index;
		}
	}
	return Displays.IsEmpty() ? -1 : 0;
}

int32 FDisplayLayout::FindDisplayContaining( int32 X, int32 Y ) const
{
	for ( int32 Index = 0; Index < Displays.Num(); ++Index )
	{
		if ( Displays[Index].Contains( X, Y ) )
		{
			return Index;
		}
	}
	return -1;
}

int32 FDisplayLayout::FindNearestDisplay( int32 X, int32 Y ) const
{
	int32 Best = -1;
	int64 BestDistance = 0;
	for ( int32 Index = 0; Index < Displays.Num(); ++Index )
	{
		const FDisplayInfo& Display = Displays[Index];
		// 点から長方形までの距離の二乗（長方形の中なら 0）
		const int64 DeltaX = X < Display.Left ? Display.Left - X : ( X >= Display.GetRight() ? X - ( Display.GetRight() - 1 ) : 0 );
		const int64 DeltaY = Y < Display.Top ? Display.Top - Y : ( Y >= Display.GetBottom() ? Y - ( Display.GetBottom() - 1 ) : 0 );
		const int64 Distance = DeltaX * DeltaX + DeltaY * DeltaY;
		if ( Best < 0 || Distance < BestDistance )
		{
			Best = Index;
			BestDistance = Distance;
		}
	}
	return Best;
}

bool FDisplayLayout::GetCenteredPosition( int32 DisplayIndex, int32 Width, int32 Height, int32& OutX, int32& OutY ) const
{
	if ( DisplayIndex < 0 || DisplayIndex >= Displays.Num() )
	{
		return false;
	}
	const FDisplayInfo& Display = Displays[DisplayIndex];
	const int32 FreeWidth = Display.Width - Width;
	const int32 FreeHeight = Display.Height - Height;
	OutX = Display.Left + ( FreeWidth > 0 ? FreeWidth / 2 : 0 );
	OutY = Display.Top + ( FreeHeight > 0 ? FreeHeight / 2 : 0 );
	return true;
}
