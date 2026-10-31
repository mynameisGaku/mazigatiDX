#include "ShadowsView.h"

#include "Source/Shadows/ShadowsScenery.h"

CShadowsView::CShadowsView( const TSharedPtr<FShadowsDemoState>& InState )
	: State( InState )
{
}

void CShadowsView::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() )
	{
		return;
	}
	const int32 Right = FShadowsDemoState::ViewLeft + FShadowsDemoState::ViewWidth;
	const int32 Bottom = FShadowsDemoState::ViewTop + FShadowsDemoState::ViewHeight;

	// 見せる範囲にクリップして、空（上が濃く、地平線の近くは薄い）を塗る
	FClipScope Clip( CommandList, FShadowsDemoState::ViewLeft, FShadowsDemoState::ViewTop, Right, Bottom );
	const int32 Middle = FShadowsDemoState::ViewTop + FShadowsDemoState::ViewHeight / 2;
	CommandList.Box( FShadowsDemoState::ViewLeft, FShadowsDemoState::ViewTop, Right, Middle, FColor32( 96, 150, 214 ) );
	CommandList.Box( FShadowsDemoState::ViewLeft, Middle, Right, Bottom, FColor32( 168, 204, 234 ) );

	// このカメラで見える 3D を描く。使えない値のカメラのときは、何も描かない
	FCameraScope Scope( CommandList, State->Camera );
	if ( !Scope.IsPushed() )
	{
		return;
	}
	NShadowsScenery::RecordStage( CommandList, *State );
}
