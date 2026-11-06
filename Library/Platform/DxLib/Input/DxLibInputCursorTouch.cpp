// SPDX-License-Identifier: MIT
#include "DxLibInput.h"

#include "Library/Platform/DxLib/DxLibInclude.h"

//=============================================================================
// CDxLibInput のうち、カーソルの操作（ICursorControl）とタッチ入力（ITouchInput）
//=============================================================================
bool CDxLibInput::SetCursorVisible( bool bVisible )
{
	if ( SetMouseDispFlag( bVisible ? TRUE : FALSE ) != 0 )
	{
		return false;
	}
	bCursorVisible = bVisible;
	return true;
}

bool CDxLibInput::IsCursorVisible() const
{
	return bCursorVisible;
}

bool CDxLibInput::SetCursorPosition( int32 X, int32 Y )
{
	return SetMousePoint( X, Y ) == 0;
}

bool CDxLibInput::IsTouchAvailable() const
{
	// DxLib は、タッチ画面が無い機器では、触れている指の数を 0 と答える（機器の有無は分からない）
	return true;
}

const TArray<FTouchPoint>& CDxLibInput::GetTouches() const
{
	return TouchTracker.GetTouches();
}

const TArray<uint32>& CDxLibInput::GetReleasedTouchIds() const
{
	return TouchTracker.GetReleasedIds();
}

void CDxLibInput::UpdateTouch()
{
	TArray<FTouchPoint> Raw;
	const int32 Count = GetTouchInputNum();
	for ( int32 Index = 0; Index < Count; ++Index )
	{
		int X = 0;
		int Y = 0;
		int Id = 0;
		float Pressure = 0.0f;
		if ( GetTouchInput( Index, &X, &Y, &Id, nullptr, &Pressure ) != 0 )
		{
			continue;
		}
		FTouchPoint Touch;
		Touch.Id = static_cast<uint32>( Id );
		Touch.X = X;
		Touch.Y = Y;
		Touch.Pressure = Pressure < 0.0f ? 0.0f : ( Pressure > 1.0f ? 1.0f : Pressure );
		Raw.Add( Touch );
	}
	TouchTracker.Update( Raw );
}
