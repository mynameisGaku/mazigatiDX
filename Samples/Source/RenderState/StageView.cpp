#include "StageView.h"

#include "Source/RenderState/StageScenery.h"

CStageView::CStageView( const TSharedPtr<FRenderStateDemoState>& InState )
	: State( InState )
{
}

void CStageView::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() )
	{
		return;
	}
	const int32 Right = FRenderStateDemoState::ViewLeft + FRenderStateDemoState::ViewWidth;
	const int32 Bottom = FRenderStateDemoState::ViewTop + FRenderStateDemoState::ViewHeight;

	// 見せる範囲にクリップして、空（上が暗く、下は距離のフォグの色）を塗る
	FClipScope Clip( CommandList, FRenderStateDemoState::ViewLeft, FRenderStateDemoState::ViewTop, Right, Bottom );
	const int32 Middle = FRenderStateDemoState::ViewTop + FRenderStateDemoState::ViewHeight / 2;
	CommandList.Box( FRenderStateDemoState::ViewLeft, FRenderStateDemoState::ViewTop, Right, Middle, FColor32( 18, 22, 40 ) );
	CommandList.Box( FRenderStateDemoState::ViewLeft, Middle, Right, Bottom, FColor32( 30, 38, 66 ) );

	// このカメラで見える 3D を描く。使えない値のカメラのときは、何も描かない
	FCameraScope Scope( CommandList, State->Camera );
	if ( !Scope.IsPushed() )
	{
		return;
	}
	NStageScenery::RecordStage( CommandList, *State );
}
