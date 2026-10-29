#include "FiltersSample.h"

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Filters/FilterGallery.h"
#include "Source/Filters/FilterImages.h"
#include "Source/Filters/SpotlightView.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CFiltersSample> Registrar( FSampleInfo { "Filters", "Filters: image filters and masks", "Applies image filters (mono, invert, blur, hue, levels, two colors, gradient map) and blend modes to an image, and shows a picture only inside a moving spotlight mask.", 24 } );

	// 左のギャラリーの見出しの位置と、右のスポットライトの見出しの位置
	const FVector2 GalleryOrigin( 40.0f, 96.0f );
	const FVector2 SpotlightOrigin( 580.0f, 96.0f );
}

void CFiltersSample::OnEnter()
{
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	if ( Assets == nullptr )
	{
		return;
	}

	// 画像のファイルを置かなくてもよいよう、画素を CPU 側で作って、画像にする
	const TSharedPtr<CDynamicTexture> Source = Assets->CreateDynamicTexture( "Samples/Filters/Source", NFilterImages::MakeSource() );
	const TSharedPtr<CDynamicTexture> Overlay = Assets->CreateDynamicTexture( "Samples/Filters/Overlay", NFilterImages::MakeOverlay() );
	const TSharedPtr<CDynamicTexture> HeatMap = Assets->CreateDynamicTexture( "Samples/Filters/HeatMap", NFilterImages::MakeHeatMap() );
	const TSharedPtr<CDynamicTexture> Glow = Assets->CreateDynamicTexture( "Samples/Filters/Glow", NFilterImages::MakeGlow( CSpotlightView::GlowSize ) );
	if ( !Source.IsValid() || !Overlay.IsValid() || !HeatMap.IsValid() || !Glow.IsValid() )
	{
		return;
	}

	// ギャラリーの結果を出す描画先（元の画像と同じ大きさ。2D だけなので、Z バッファは要らない。0 番は元の画像そのままなので使わない）
	FRenderTargetDesc TileDesc;
	TileDesc.Width = CFilterGallery::TileSize;
	TileDesc.Height = CFilterGallery::TileSize;
	TileDesc.bAlpha = false;
	TileDesc.bDepth = false;
	TArray<TSharedPtr<CRenderTarget>> Tiles;
	Tiles.Add( TSharedPtr<CRenderTarget>() );
	for ( int32 Index = 1; Index < CFilterGallery::NumTiles; ++Index )
	{
		Tiles.Add( Assets->CreateRenderTarget( FString::Printf( "Samples/Filters/Tile%d", Index ), TileDesc ) );
	}

	// マスクの画像は、アルファを持つ描画先。マスクを使って描く先は、アルファのいらない描画先
	FRenderTargetDesc MaskDesc;
	MaskDesc.Width = CSpotlightView::ViewSize;
	MaskDesc.Height = CSpotlightView::ViewSize;
	MaskDesc.bAlpha = true;
	MaskDesc.bDepth = false;
	const TSharedPtr<CRenderTarget> Mask = Assets->CreateRenderTarget( "Samples/Filters/Mask", MaskDesc );
	MaskDesc.bAlpha = false;
	const TSharedPtr<CRenderTarget> View = Assets->CreateRenderTarget( "Samples/Filters/View", MaskDesc );

	SpawnObject<CFilterGallery>( GalleryOrigin, Source, Overlay, HeatMap, Tiles );
	SpawnObject<CSpotlightView>( SpotlightOrigin, Source, Glow, Mask, View );
}

void CFiltersSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 56, "Filters: ApplyFilter / BlendImages process images in recorded order. Masks: FMaskScope dims drawing by the mask image's values.", FColor32::Gray );
}
