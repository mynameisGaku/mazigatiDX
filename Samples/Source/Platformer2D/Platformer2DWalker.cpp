#include "Platformer2DWalker.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Platformer2D/Platformer2DState.h"

namespace
{
	constexpr float32 WalkerHalfWidth = 14.0f;
	constexpr float32 WalkerHeight = 28.0f;
}

CPlatformer2DWalker::CPlatformer2DWalker( const TSharedPtr<FPlatformer2DState>& InState, const FVector2& InFeetPosition )
	: CPlatformer2DEnemy( InState, InFeetPosition, WalkerHalfWidth, WalkerHeight, "Platformer/Walker2D.gxstate" )
{}

void CPlatformer2DWalker::OnStomped( float32 PlayerX )
{
	// すぐ、生きていないことにする（同じフレームに、ほかのものが、また踏まない）。つぶれる姿へは、ステートマシンが遷移する
	bSquashed = true;
	CPlatformer2DEnemy::OnStomped( PlayerX );
}

void CPlatformer2DWalker::Draw( FDrawContext& CommandList )
{
	const int32 X = State->ToScreenX( Position.x );
	const int32 Y = static_cast<int32>( Position.y );
	if ( bSquashed )
	{
		CommandList.Oval( X, Y - 5, 16, 5, FColor32( 150, 90, 40 ), true );
		return;
	}
	const bool bStep = ( static_cast<int32>( State->Time * 6.0f ) % 2 ) == 0;
	// 足（交互に動く）
	CommandList.Oval( X - 8 + ( bStep ? 2 : -2 ), Y - 3, 8, 4, FColor32( 60, 36, 20 ), true );
	CommandList.Oval( X + 8 + ( bStep ? -2 : 2 ), Y - 3, 8, 4, FColor32( 60, 36, 20 ), true );
	// 体（茶色）と、目
	CommandList.Oval( X, Y - 16, 15, 13, FColor32( 168, 100, 44 ), true );
	CommandList.Oval( X, Y - 8, 11, 7, FColor32( 238, 206, 160 ), true );
	CommandList.Circle( X - 5, Y - 19, 4, FColor32::White, true );
	CommandList.Circle( X + 5, Y - 19, 4, FColor32::White, true );
	CommandList.Circle( X - 5 + static_cast<int32>( Direction * 1.5f ), Y - 19, 2, FColor32::Black, true );
	CommandList.Circle( X + 5 + static_cast<int32>( Direction * 1.5f ), Y - 19, 2, FColor32::Black, true );
	// まゆ（おこった顔）
	CommandList.Line( X - 9, Y - 26, X - 2, Y - 22, FColor32::Black, 2 );
	CommandList.Line( X + 9, Y - 26, X + 2, Y - 22, FColor32::Black, 2 );
}
