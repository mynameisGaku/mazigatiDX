#include "RenderTargetSample.h"

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Common/TestPattern.h"
#include "Source/RenderTarget/TargetShowcase.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CRenderTargetSample> Registrar( FSampleInfo { "RenderTarget", "RenderTarget: draw into an image", "Draws into a render target with a transparent background, shows it as an ordinary image, copies a part of the screen, reads a pixel and saves a PNG.", 21 } );

	// 左の絵の左上の見出しの位置
	const FVector2 ShowcaseOrigin( 60.0f, 96.0f );

	// 描画先の大きさ（ピクセル）
	constexpr int32 MapWidth = 160;
	constexpr int32 MapHeight = 120;
}

void CRenderTargetSample::OnEnter()
{
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	if ( Assets == nullptr )
	{
		return;
	}

	// ミニマップに描く画像は、画像のファイルを置かなくてもよいよう、メモリの上で BMP を作って、画像にする
	const TArray<uint8> Bitmap = NTestPattern::MakeBitmap();
	const TSharedPtr<CTexture> Pattern = Assets->CreateTextureFromMemory( "Samples/RenderTarget/TestPattern", Bitmap.GetData(), static_cast<usize>( Bitmap.Num() ) );

	// ミニマップは、透明を持つ描画先（背景を透明のまま残す）。2D だけなので、Z バッファは要らない。
	// スナップショットは、画面の一部を写す先なので、写す矩形（ミニマップと同じ大きさ）と同じ大きさにする
	FRenderTargetDesc Desc;
	Desc.Width = MapWidth;
	Desc.Height = MapHeight;
	Desc.bAlpha = true;
	Desc.bDepth = false;
	const TSharedPtr<CRenderTarget> Minimap = Assets->CreateRenderTarget( "Samples/RenderTarget/Minimap", Desc );
	Desc.bAlpha = false;
	const TSharedPtr<CRenderTarget> Snapshot = Assets->CreateRenderTarget( "Samples/RenderTarget/Snapshot", Desc );
	if ( !Pattern.IsValid() || !Minimap.IsValid() || !Snapshot.IsValid() )
	{
		return;
	}
	SpawnObject<CTargetShowcase>( ShowcaseOrigin, Pattern, Minimap, Snapshot );
}

void CRenderTargetSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 56, "Render target: FTargetScope( CommandList, Target ) draws into an image. It is an ordinary image (IImage) afterwards.", FColor32::Gray );
}
