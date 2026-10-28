#include "ImagesSample.h"

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Images/ImageShowcase.h"
#include "Source/Images/TestPattern.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CImagesSample> Registrar( FSampleInfo { "Images", "Images: drawing images", "One image drawn 1:1, stretched, as a free-form quad, a part of it, with a pivot and rotation, as tiles and as a circle gauge.", 17 } );

	// 左端の絵の中心
	const FVector2 ShowcaseOrigin( 100.0f, 180.0f );
}

void CImagesSample::OnEnter()
{
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	if ( Assets == nullptr )
	{
		return;
	}

	// 画像のファイルを置かなくてもよいよう、メモリの上で BMP を作って、画像にする
	const TArray<uint8> Bitmap = NTestPattern::MakeBitmap();
	const TSharedPtr<CTexture> Texture = Assets->CreateTextureFromMemory( "Samples/Images/TestPattern", Bitmap.GetData(), static_cast<usize>( Bitmap.Num() ) );
	if ( !Texture.IsValid() )
	{
		return;
	}
	SpawnObject<CImageShowcase>( ShowcaseOrigin, Texture );
}

void CImagesSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 56, "One 64 x 64 image (white / red / green / blue corners), drawn in seven ways. Draw.Image( X, Y, Texture ) takes a TSharedPtr<CTexture>.", FColor32::Gray );
}
