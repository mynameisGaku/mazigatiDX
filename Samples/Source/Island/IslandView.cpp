#include "IslandView.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/Draw/DrawScopes.h"
#include "Source/Island/IslandScenery.h"

namespace
{
	// 空の帯の数（上の色から、地平線の色へ、帯ごとに近づける）
	constexpr int32 SkyBands = 8;
}

CIslandView::CIslandView( const TSharedPtr<FIslandState>& InState )
	: State( InState )
{
}

void CIslandView::Draw( FDrawContext& CommandList )
{
	if ( !State.IsValid() )
	{
		return;
	}
	const int32 Right = FIslandState::ViewLeft + FIslandState::ViewWidth;
	const int32 Bottom = FIslandState::ViewTop + FIslandState::ViewHeight;

	// 見せる範囲にクリップして、空を塗る（上が濃く、地平線の近くは、霧と同じ色）
	FClipScope Clip( CommandList, FIslandState::ViewLeft, FIslandState::ViewTop, Right, Bottom );
	const FColor32 Top = NIslandScenery::GetSkyTopColor( *State );
	const FColor32 Horizon = NIslandScenery::GetHorizonColor( *State );
	const int32 SkyHeight = FIslandState::ViewHeight * 2 / 3;
	for ( int32 Band = 0; Band < SkyBands; ++Band )
	{
		const float32 T = static_cast<float32>( Band ) / static_cast<float32>( SkyBands - 1 );
		const FColor32 Color( static_cast<uint8>( NMath::Lerp( static_cast<float32>( Top.r ), static_cast<float32>( Horizon.r ), T ) ),
			static_cast<uint8>( NMath::Lerp( static_cast<float32>( Top.g ), static_cast<float32>( Horizon.g ), T ) ),
			static_cast<uint8>( NMath::Lerp( static_cast<float32>( Top.b ), static_cast<float32>( Horizon.b ), T ) ) );
		const int32 Y0 = FIslandState::ViewTop + SkyHeight * Band / SkyBands;
		const int32 Y1 = Band == SkyBands - 1 ? Bottom : FIslandState::ViewTop + SkyHeight * ( Band + 1 ) / SkyBands;
		CommandList.Box( FIslandState::ViewLeft, Y0, Right, Y1, Color );
	}

	// このカメラで見える 3D を描く。使えない値のカメラのときは、何も描かない
	FCameraScope Scope( CommandList, State->Camera );
	if ( !Scope.IsPushed() )
	{
		return;
	}
	NIslandScenery::RecordStage( CommandList, *State );
}
