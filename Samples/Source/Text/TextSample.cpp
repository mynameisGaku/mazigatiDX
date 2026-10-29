#include "TextSample.h"

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Text/FontShowcase.h"
#include "Source/Text/StyledShowcase.h"
#include "Source/Text/WrapShowcase.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CTextSample> Registrar( FSampleInfo { "Text", "Text: fonts, wrapping and styles", "Shows font attributes (antialiasing, italic, edge, letter spacing, line height, an icon inside the text), text wrapped into a box whose width changes, and scaled / rotated / shadowed / vertical text.", 25 } );

	// 3 つの列の見出しの位置
	const FVector2 FontsOrigin( 30.0f, 96.0f );
	const FVector2 WrapOrigin( 600.0f, 96.0f );
	const FVector2 StyledOrigin( 960.0f, 96.0f );

	// 折り返す文章（英語と日本語、句読点と括弧を含む）
	const char* const WrapText = "The quick brown fox jumps over the lazy dog. 吾輩は猫である。名前はまだ無い。どこで生れたかとんと見当がつかぬ。「ニャー」と鳴いていた事だけは記憶している。";

	// フォントの大きさ（見本の行・本文・飾り）
	constexpr int32 SampleSize = 22;
	constexpr int32 BodySize = 18;
	constexpr int32 StyledSize = 32;
}

void CTextSample::OnEnter()
{
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	if ( Assets == nullptr )
	{
		return;
	}

	// 1. フォントの指定の違い（同じ大きさで、指定を 1 つずつ変える）
	const auto MakeFont = [&]( const FFontStyle& Style )
	{
		return Assets->GetDefaultFont( SampleSize, Style );
	};
	TArray<FFontSampleRow> Rows;
	const auto AddRow = [&]( const char* Caption, const TSharedPtr<CFont>& Font, const char* Text, const FColor32& EdgeColor = FColor32( 0, 0, 0 ) )
	{
		FFontSampleRow Row;
		Row.Caption = Caption;
		Row.Font = Font;
		Row.Text = Text;
		Row.EdgeColor = EdgeColor;
		Rows.Add( Row );
	};
	{
		FFontStyle Style;
		AddRow( "Standard", MakeFont( Style ), "Hello あいうえお 123" );
	}
	{
		FFontStyle Style;
		Style.Antialias = EFontAntialias::None;
		AddRow( "No antialiasing", MakeFont( Style ), "Hello あいうえお 123" );
	}
	{
		FFontStyle Style;
		Style.Antialias = EFontAntialias::High16x16;
		AddRow( "16 x 16", MakeFont( Style ), "Hello あいうえお 123" );
	}
	{
		FFontStyle Style;
		Style.bItalic = true;
		Style.Thickness = 9;
		AddRow( "Italic, thick", MakeFont( Style ), "Hello あいうえお 123" );
	}
	{
		FFontStyle Style;
		Style.EdgeSize = 2;
		AddRow( "Edge (red)", MakeFont( Style ), "Hello あいうえお 123", FColor32( 255, 40, 40 ) );
	}
	{
		FFontStyle Style;
		Style.LetterSpacing = 6;
		AddRow( "Letter spacing 6", MakeFont( Style ), "Hello あいうえお 123" );
	}
	{
		FFontStyle Style;
		Style.LineHeight = 44;
		AddRow( "Line height 44", MakeFont( Style ), "First line\nSecond line" );
	}
	{
		// 文字の代わりに画像を入れる: 「@」を、アイコンに置き換える（専用のフォントを作る。登録は、そのフォントだけに効く。画像は、フォントが持つ）
		FPixelImage IconImage( 24, 24, FColor32( 220, 40, 40, 255 ) );
		IconImage.FillRect( 4, 4, 16, 16, FColor32( 255, 255, 255, 255 ) );
		IconImage.FillRect( 8, 8, 8, 8, FColor32( 220, 40, 40, 255 ) );
		const TSharedPtr<CDynamicTexture> Icon = Assets->CreateDynamicTexture( "Samples/Text/Icon", IconImage );
		FFontStyle Style;
		Style.LetterSpacing = 1;   // ほかの行と共有されないよう、指定を少し変える
		const TSharedPtr<CFont> IconFont = Assets->GetDefaultFont( SampleSize + 1, Style );
		if ( Icon.IsValid() && IconFont.IsValid() )
		{
			IconFont->AddCharImage( "@", Icon, 30, 0, 0 );
		}
		AddRow( "Icon in text", IconFont, "Press @ to jump" );
	}

	// 2. 折り返す本文のフォント
	const TSharedPtr<CFont> Body = Assets->GetDefaultFont( BodySize );

	// 3. 飾りのフォント
	const TSharedPtr<CFont> Styled = Assets->GetDefaultFont( StyledSize );

	SpawnObject<CFontShowcase>( FontsOrigin, Rows );
	SpawnObject<CWrapShowcase>( WrapOrigin, Body, FString( WrapText ) );
	SpawnObject<CStyledShowcase>( StyledOrigin, Styled );
}

void CTextSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 56, "Text: GetDefaultFont( size, FFontStyle ), CFont::Layout / RecordLayout (wrapping) and RecordStyled (scale, rotate, shadow, vertical).", FColor32::Gray );
}
