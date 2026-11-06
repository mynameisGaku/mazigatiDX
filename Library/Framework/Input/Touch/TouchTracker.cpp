// SPDX-License-Identifier: MIT
#include "TouchTracker.h"

namespace
{
	// 一覧の中に、同じ ID の指があるか
	bool ContainsId( const TArray<FTouchPoint>& List, uint32 Id )
	{
		for ( const FTouchPoint& Touch : List )
		{
			if ( Touch.Id == Id )
			{
				return true;
			}
		}
		return false;
	}
}

void FTouchTracker::Update( const TArray<FTouchPoint>& RawTouches )
{
	TArray<FTouchPoint> Next;
	for ( const FTouchPoint& Raw : RawTouches )
	{
		if ( ContainsId( Next, Raw.Id ) )
		{
			continue;
		}
		FTouchPoint Touch = Raw;
		Touch.bPressedThisFrame = !ContainsId( Touches, Raw.Id );
		Next.Add( Touch );
	}

	ReleasedIds.Reset();
	for ( const FTouchPoint& Previous : Touches )
	{
		if ( !ContainsId( Next, Previous.Id ) )
		{
			ReleasedIds.Add( Previous.Id );
		}
	}
	Touches = MoveTemp( Next );
}

void FTouchTracker::Reset()
{
	Touches.Reset();
	ReleasedIds.Reset();
}
