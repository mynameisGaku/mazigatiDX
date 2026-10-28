#include "DrawStateSample.h"

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Common/TestPattern.h"
#include "Source/DrawState/BlendShowcase.h"
#include "Source/DrawState/ViewShowcase.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CDrawStateSample> Registrar( FSampleInfo { "DrawState", "DrawState: blend, clip, filter and transform", "Blend modes, a custom blend, an added color, the alpha test, a clip window, nearest and bilinear filtering, and a 2D camera.", 19 } );

	// 上の列（ブレンド）の左端の絵の中心と、下の列（クリップ・描画モード・2D の変換）の左上の見出しの位置
	const FVector2 BlendOrigin( 110.0f, 150.0f );
	const FVector2 ViewOrigin( 50.0f, 380.0f );
}

void CDrawStateSample::OnEnter()
{
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	if ( Assets == nullptr )
	{
		return;
	}

	// 画像のファイルを置かなくてもよいよう、メモリの上で BMP を作って、画像にする
	const TArray<uint8> Bitmap = NTestPattern::MakeBitmap();
	const TSharedPtr<CTexture> Texture = Assets->CreateTextureFromMemory( "Samples/DrawState/TestPattern", Bitmap.GetData(), static_cast<usize>( Bitmap.Num() ) );
	if ( !Texture.IsValid() )
	{
		return;
	}
	SpawnObject<CBlendShowcase>( BlendOrigin );
	SpawnObject<CViewShowcase>( ViewOrigin, Texture );
}

void CDrawStateSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 56, "Draw state: set it with CommandList.Blend / CustomBlend / AddColor / AlphaTest / Filter / PushClip / Transform2D. It lasts until the end of the object's Draw().", FColor32::Gray );
}
