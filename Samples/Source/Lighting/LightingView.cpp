#include "LightingView.h"

#include "Source/Lighting/LightingScenery.h"

CLightingView::CLightingView( const TSharedPtr<FLightingDemoState>& InState )
	: State( InState )
{
}

void CLightingView::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() )
	{
		return;
	}
	const int32 Right = FLightingDemoState::ViewLeft + FLightingDemoState::ViewWidth;
	const int32 Bottom = FLightingDemoState::ViewTop + FLightingDemoState::ViewHeight;

	// 見せる範囲にクリップして、夜空（上が暗く、地平線の近くは少し明るい）を塗る
	FClipScope Clip( CommandList, FLightingDemoState::ViewLeft, FLightingDemoState::ViewTop, Right, Bottom );
	const int32 Middle = FLightingDemoState::ViewTop + FLightingDemoState::ViewHeight / 2;
	CommandList.Box( FLightingDemoState::ViewLeft, FLightingDemoState::ViewTop, Right, Middle, FColor32( 8, 10, 22 ) );
	CommandList.Box( FLightingDemoState::ViewLeft, Middle, Right, Bottom, FColor32( 22, 26, 46 ) );

	// このカメラで見える 3D を描く。使えない値のカメラのときは、何も描かない
	FCameraScope Scope( CommandList, State->Camera );
	if ( !Scope.IsPushed() )
	{
		return;
	}
	NLightingScenery::RecordStage( CommandList, *State );
}
