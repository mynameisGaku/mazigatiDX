#include "MainView.h"

#include "Source/Camera/Scenery.h"

CMainView::CMainView( const TSharedPtr<FCameraDemoState>& InState )
	: State( InState )
{
}

void CMainView::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() )
	{
		return;
	}
	const int32 Right = FCameraDemoState::ViewLeft + FCameraDemoState::ViewWidth;
	const int32 Bottom = FCameraDemoState::ViewTop + FCameraDemoState::ViewHeight;

	// 見せる範囲にクリップして、空（上が暗く、下が少し明るい 2 段）を塗る
	FClipScope Clip( CommandList, FCameraDemoState::ViewLeft, FCameraDemoState::ViewTop, Right, Bottom );
	const int32 Middle = FCameraDemoState::ViewTop + FCameraDemoState::ViewHeight / 2;
	CommandList.Box( FCameraDemoState::ViewLeft, FCameraDemoState::ViewTop, Right, Middle, FColor32( 18, 22, 40 ) );
	CommandList.Box( FCameraDemoState::ViewLeft, Middle, Right, Bottom, FColor32( 30, 38, 66 ) );

	// このカメラで見える 3D を描く。使えない値のカメラのときは、何も描かない
	FCameraScope Scope( CommandList, State->Camera );
	if ( !Scope.IsPushed() )
	{
		return;
	}
	NScenery::RecordGround( CommandList );
	const FFrustum Frustum = State->Camera.GetFrustum( State->GetScreenViewport() );
	NScenery::RecordObjects( CommandList, State->Objects, &Frustum, State->HoveredIndex );
}
