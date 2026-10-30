#include "Shapes3DView.h"

#include "Source/Shapes3D/Shapes3DScenery.h"

CShapes3DView::CShapes3DView( const TSharedPtr<FShapes3DDemoState>& InState )
	: State( InState )
{
}

void CShapes3DView::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() )
	{
		return;
	}
	const int32 Right = FShapes3DDemoState::ViewLeft + FShapes3DDemoState::ViewWidth;
	const int32 Bottom = FShapes3DDemoState::ViewTop + FShapes3DDemoState::ViewHeight;

	// 見せる範囲にクリップして、夜空（上が暗く、地平線の近くは少し明るい）を塗る
	FClipScope Clip( CommandList, FShapes3DDemoState::ViewLeft, FShapes3DDemoState::ViewTop, Right, Bottom );
	const int32 Middle = FShapes3DDemoState::ViewTop + FShapes3DDemoState::ViewHeight / 2;
	CommandList.Box( FShapes3DDemoState::ViewLeft, FShapes3DDemoState::ViewTop, Right, Middle, FColor32( 6, 8, 20 ) );
	CommandList.Box( FShapes3DDemoState::ViewLeft, Middle, Right, Bottom, FColor32( 14, 18, 36 ) );

	// このカメラで見える 3D を描く。使えない値のカメラのときは、何も描かない
	FCameraScope Scope( CommandList, State->Camera );
	if ( !Scope.IsPushed() )
	{
		return;
	}
	NShapes3DScenery::RecordStage( CommandList, *State );
}
