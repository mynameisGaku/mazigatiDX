#include "FontShowcase.h"

#include "Library/Framework/Render/DrawContext.h"

namespace
{
	// 見出しから最初の行までの高さ、見出しの列の幅、行のあいだの隙間（ピクセル）
	constexpr int32 TitleHeight = 28;
	constexpr int32 CaptionWidth = 150;
	constexpr int32 RowGap = 8;

	// 画面の座標（float）を、整数の座標を取る描画に渡すために、一番近いピクセルに丸める
	FORCEINLINE int32 Px( float32 Value )
	{
		return NMath::RoundToInt( Value );
	}

	// 文字列の行数（改行の数 + 1）
	int32 CountLines( const FString& Text )
	{
		int32 NumLines = 1;
		for ( const char Character : Text )
		{
			NumLines += Character == '\n' ? 1 : 0;
		}
		return NumLines;
	}
}

CFontShowcase::CFontShowcase( const FVector2& InOrigin, const TArray<FFontSampleRow>& InRows )
	: Origin( InOrigin )
	, Rows( InRows )
{
}

void CFontShowcase::Draw( FDrawContext& CommandList )
{
	const int32 Left = Px( Origin.x );
	int32 Y = Px( Origin.y );
	CommandList.Text( Left, Y, "FFontStyle: one text, many fonts", FColor32::White );
	Y += TitleHeight;
	for ( const FFontSampleRow& Row : Rows )
	{
		if ( !Row.Font.IsValid() )
		{
			continue;
		}
		// 行の高さは、作るときに測った寸法から決める（(行数 - 1) x 行の送り幅 + 1 行の文字の高さ）
		const FFontMetrics& Metrics = Row.Font->GetMetrics();
		const int32 Height = ( CountLines( Row.Text ) - 1 ) * Metrics.LineHeight + Metrics.GlyphHeight;
		CommandList.Text( Left, Y + 4, *Row.Caption, FColor32::Gray );
		Row.Font->Record( CommandList, Left + CaptionWidth, Y, *Row.Text, FColor32::White, EDrawTextAlign::Left, Row.EdgeColor );
		Y += Height + RowGap;
	}
}
