#include "WrapShowcase.h"

#include "Library/Framework/Render/DrawContext.h"

namespace
{
	// 見出しから箱までの高さ、動く箱の幅の範囲、決まった箱の幅と、行数の上限、決まった箱の位置（動く箱の上端から。いちばん狭いときの行数でも重ならない高さ）（ピクセル）
	constexpr int32 TitleHeight = 28;
	constexpr int32 MinWidth = 180;
	constexpr int32 MaxWidth = 340;
	constexpr int32 FixedWidth = 240;
	constexpr int32 FixedMaxLines = 3;
	constexpr int32 FixedBoxOffset = 320;

	// 画面の座標（float）を、整数の座標を取る描画に渡すために、一番近いピクセルに丸める
	FORCEINLINE int32 Px( float32 Value )
	{
		return NMath::RoundToInt( Value );
	}
}

CWrapShowcase::CWrapShowcase( const FVector2& InOrigin, const TSharedPtr<CFont>& InFont, const FString& InText )
	: Origin( InOrigin )
	, Font( InFont )
	, Text( InText )
{
	// 決まった幅の箱は、最初に 1 度だけ折り返す（コンストラクターは、メインスレッドで呼ばれる）
	if ( Font.IsValid() )
	{
		FTextLayoutOptions Options;
		Options.MaxWidth = FixedWidth;
		Options.MaxLines = FixedMaxLines;
		Fixed = Font->Layout( *Text, Options );
	}
	UpdateMovingLayout();
}

void CWrapShowcase::UpdateMovingLayout()
{
	MovingWidth = MinWidth + Px( static_cast<float32>( MaxWidth - MinWidth ) * ( 0.5f + 0.5f * NMath::Sin( Time * 0.8f ) ) );
	if ( !Font.IsValid() )
	{
		return;
	}
	FTextLayoutOptions Options;
	Options.MaxWidth = MovingWidth;
	Moving = Font->Layout( *Text, Options );
}

void CWrapShowcase::Tick( float32 DeltaTime )
{
	Time += DeltaTime;
	UpdateMovingLayout();
}

int32 CWrapShowcase::GetBoxHeight( const FTextLayout& Layout ) const
{
	if ( !Font.IsValid() || Layout.Lines.Num() == 0 )
	{
		return 0;
	}
	const FFontMetrics& Metrics = Font->GetMetrics();
	return ( Layout.Lines.Num() - 1 ) * Metrics.LineHeight + Metrics.GlyphHeight;
}

void CWrapShowcase::Draw( FDrawContext& CommandList )
{
	const int32 Left = Px( Origin.x );
	int32 Top = Px( Origin.y );
	CommandList.Text( Left, Top, "CFont::Layout: wrap into a box", FColor32::White );
	Top += TitleHeight;
	if ( !Font.IsValid() )
	{
		return;
	}

	// 動く箱: 幅が変わるたびに、折り返しが変わる
	const int32 MovingHeight = GetBoxHeight( Moving );
	CommandList.Box( Left - 2, Top - 2, Left + MovingWidth + 2, Top + MovingHeight + 2, FColor32( 90, 90, 90 ), false );
	Font->RecordLayout( CommandList, Moving, Left, Top, MovingWidth, FColor32::White );
	CommandList.TextF( Left, Top + MovingHeight + 8, FColor32::Gray, "MaxWidth %d -> %d lines (widest line %d)", MovingWidth, Moving.Lines.Num(), Moving.Width );

	// 決まった幅の箱: 右揃え、3 行まで。切れたら、印を出す
	const int32 FixedTop = Top + FixedBoxOffset;
	const int32 FixedHeight = GetBoxHeight( Fixed );
	CommandList.Text( Left, FixedTop - 24, "Right aligned, at most 3 lines", FColor32::Gray );
	CommandList.Box( Left - 2, FixedTop - 2, Left + FixedWidth + 2, FixedTop + FixedHeight + 2, FColor32( 90, 90, 90 ), false );
	Font->RecordLayout( CommandList, Fixed, Left, FixedTop, FixedWidth, FColor32::White, EDrawTextAlign::Right );
	if ( Fixed.bTruncated )
	{
		CommandList.Text( Left + FixedWidth - 24, FixedTop + FixedHeight + 6, "...", FColor32( 255, 220, 0 ) );
	}
}
