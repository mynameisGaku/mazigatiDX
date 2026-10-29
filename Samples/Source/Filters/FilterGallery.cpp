#include "FilterGallery.h"

#include "Library/Framework/Render/DrawContext.h"

namespace
{
	// 見出しから、タイルまでの高さ（ピクセル）
	constexpr int32 TitleHeight = 24;

	// 画面の座標（float）を、整数の座標を取る描画に渡すために、一番近いピクセルに丸める
	FORCEINLINE int32 Px( float32 Value )
	{
		return NMath::RoundToInt( Value );
	}

	// タイルの見出し（0 番は、元の画像。* は、動かしているもの。1 つのタイルの幅（112 ピクセル）に収まる長さ）
	const char* const Labels[CFilterGallery::NumTiles] = {
		"Source", "Mono", "Invert", "Gauss *", "Hsb *", "Level", "TwoColor", "GradientMap",
		"Multiply", "Add", "Screen", "Difference",
	};
}

CFilterGallery::CFilterGallery( const FVector2& InOrigin, const TSharedPtr<CDynamicTexture>& InSource, const TSharedPtr<CDynamicTexture>& InOverlay, const TSharedPtr<CDynamicTexture>& InHeatMap,
								const TArray<TSharedPtr<CRenderTarget>>& InTiles )
	: Origin( InOrigin )
	, Source( InSource )
	, Overlay( InOverlay )
	, HeatMap( InHeatMap )
	, Tiles( InTiles )
{
}

void CFilterGallery::Tick( float32 DeltaTime )
{
	Time += DeltaTime;
}

void CFilterGallery::Draw( FDrawContext& CommandList )
{
	if ( !Source.IsValid() || !Overlay.IsValid() || !HeatMap.IsValid() || Tiles.Num() != NumTiles )
	{
		return;
	}
	CommandList.Text( Px( Origin.x ), Px( Origin.y ), "ApplyFilter( Source, Dest, FGraphFilter ) and BlendImages( Base, Overlay, Dest, FGraphBlend )", FColor32::White );

	// 動かす値（ぼかしの強さは 200〜1000、色相は 1 秒に 60 度）
	const float32 Pulse = 0.5f + 0.5f * NMath::Sin( Time * 2.0f );
	for ( int32 Index = 0; Index < NumTiles; ++Index )
	{
		const int32 X = Px( Origin.x ) + ( Index % Columns ) * PitchX;
		const int32 Y = Px( Origin.y ) + TitleHeight + ( Index / Columns ) * PitchY;
		if ( Index == 0 )
		{
			CommandList.Image( static_cast<float32>( X ), static_cast<float32>( Y ), Source );
		}
		else
		{
			const TSharedPtr<CRenderTarget>& Tile = Tiles[Index];
			if ( !Tile.IsValid() )
			{
				continue;
			}
			// 元の画像に掛けた結果を、このタイルの描画先へ出し（元の画像は変わらない）、そのあと、ふつうの画像として貼る
			switch ( Index )
			{
			case 1:
				CommandList.ApplyFilter( Source, Tile, FGraphFilter::Mono() );
				break;
			case 2:
				CommandList.ApplyFilter( Source, Tile, FGraphFilter::Invert() );
				break;
			case 3:
				CommandList.ApplyFilter( Source, Tile, FGraphFilter::Gauss( 32, 200 + static_cast<int32>( 800.0f * Pulse ) ) );
				break;
			case 4:
				CommandList.ApplyFilter( Source, Tile, FGraphFilter::HsbRelative( static_cast<int32>( Time * 60.0f ) % 360, 0, 0 ) );
				break;
			case 5:
				CommandList.ApplyFilter( Source, Tile, FGraphFilter::Level( 30, 220, 150, 0, 255 ) );
				break;
			case 6:
				CommandList.ApplyFilter( Source, Tile, FGraphFilter::TwoColor( 128, FColor32( 20, 20, 90 ), FColor32( 255, 220, 120 ) ) );
				break;
			case 7:
				CommandList.ApplyFilter( Source, Tile, FGraphFilter::GradientMap( HeatMap->GetImageHandle() ) );
				break;
			case 8:
				CommandList.BlendImages( Source, Overlay, Tile, FGraphBlend::Make( EDrawGraphBlend::Multiply ) );
				break;
			case 9:
				CommandList.BlendImages( Source, Overlay, Tile, FGraphBlend::Make( EDrawGraphBlend::Add, 160 ) );
				break;
			case 10:
				CommandList.BlendImages( Source, Overlay, Tile, FGraphBlend::Make( EDrawGraphBlend::Screen ) );
				break;
			default:
				CommandList.BlendImages( Source, Overlay, Tile, FGraphBlend::Make( EDrawGraphBlend::Difference ) );
				break;
			}
			CommandList.Image( static_cast<float32>( X ), static_cast<float32>( Y ), Tile );
		}
		CommandList.Text( X, Y + TileSize + 4, Labels[Index], FColor32::Gray );
	}
	CommandList.Text( Px( Origin.x ), Px( Origin.y ) + TitleHeight + 3 * PitchY + 4, "* animated.  Bottom row: BlendImages( Source, Overlay, Dest, FGraphBlend )", FColor32::Gray );
}
