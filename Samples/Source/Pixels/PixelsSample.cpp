#include "PixelsSample.h"

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Pixels/PlasmaView.h"
#include "Source/Pixels/SpriteSheetMaker.h"
#include "Source/Pixels/TileView.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CPixelsSample> Registrar( FSampleInfo { "Pixels", "Pixels: images made from pixels", "Writes pixels on the CPU (FPixelImage) and shows them as an image that is updated every frame, overlays a semi-transparent glow, splits a sprite sheet and saves a PNG.", 23 } );

	// 左の絵（プラズマ）の見出しの位置と、右の絵（コマ）の見出しの位置
	const FVector2 PlasmaOrigin( 60.0f, 96.0f );
	const FVector2 TileOrigin( 560.0f, 96.0f );

	// プラズマの画像の大きさ（ピクセル）
	constexpr int32 PlasmaSize = 128;
}

void CPixelsSample::OnEnter()
{
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	if ( Assets == nullptr )
	{
		return;
	}

	// 画像のファイルを置かなくてもよいよう、画素を CPU 側で作って、画像にする
	const FPixelImage Canvas( PlasmaSize, PlasmaSize, FColor32( 0, 0, 0, 255 ) );
	const TSharedPtr<CDynamicTexture> Plasma = Assets->CreateDynamicTexture( "Samples/Pixels/Plasma", Canvas );
	const TSharedPtr<CDynamicTexture> Sheet = Assets->CreateDynamicTexture( "Samples/Pixels/Sheet", NSpriteSheetMaker::Make() );
	if ( !Plasma.IsValid() || !Sheet.IsValid() )
	{
		return;
	}

	// スプライトシートを、格子（4 x 2、1 コマ 32 x 32）で、1 コマずつの画像に分ける
	FSpriteSheetGrid Grid;
	Grid.Columns = NSpriteSheetMaker::Columns;
	Grid.Rows = NSpriteSheetMaker::Rows;
	Grid.CellWidth = NSpriteSheetMaker::CellSize;
	Grid.CellHeight = NSpriteSheetMaker::CellSize;
	const TArray<TSharedPtr<CTexture>> Pieces = Assets->SplitTexture( Sheet, Grid );

	SpawnObject<CPlasmaView>( PlasmaOrigin, Canvas, Plasma );
	SpawnObject<CTileView>( TileOrigin, Pieces );
}

void CPixelsSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 56, "Pixels: FPixelImage is an image in memory (CPU). CreateDynamicTexture puts it on the GPU, Update replaces its contents.", FColor32::Gray );
}
