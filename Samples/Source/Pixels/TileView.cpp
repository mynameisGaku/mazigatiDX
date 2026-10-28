#include "TileView.h"

#include "Library/Framework/Render/DrawContext.h"

namespace
{
	// コマの表示の大きさ（ピクセル。元は 32 x 32 なので、2 倍）と、並べるときの間隔
	constexpr int32 TileSize = 64;
	constexpr int32 TileGap = 12;
	// 1 段に並べるコマの数
	constexpr int32 TilesPerRow = 4;
	// 見出しから絵までの高さ（ピクセル）
	constexpr int32 TitleHeight = 24;

	// 画面の座標（float）を、整数の座標を取る描画に渡すために、一番近いピクセルに丸める
	FORCEINLINE int32 Px( float32 Value )
	{
		return NMath::RoundToInt( Value );
	}
}

CTileView::CTileView( const FVector2& InOrigin, const TArray<TSharedPtr<CTexture>>& InPieces )
	: Origin( InOrigin )
	, Pieces( InPieces )
{
}

void CTileView::Tick( float32 DeltaTime )
{
	Time += DeltaTime;
}

void CTileView::Draw( FDrawContext& CommandList )
{
	const int32 Left = Px( Origin.x );
	const int32 Top = Px( Origin.y ) + TitleHeight;
	CommandList.Text( Left, Px( Origin.y ), "SplitTexture( Sheet, Grid ) -> one image per cell", FColor32::White );

	// 全コマを並べる（番号つき）
	for ( int32 Index = 0; Index < Pieces.Num(); ++Index )
	{
		const int32 X = Left + ( Index % TilesPerRow ) * ( TileSize + TileGap );
		const int32 Y = Top + ( Index / TilesPerRow ) * ( TileSize + TileGap + 16 );
		CommandList.ImageStretch( static_cast<float32>( X ), static_cast<float32>( Y ), static_cast<float32>( X + TileSize ), static_cast<float32>( Y + TileSize ), Pieces[Index] );
		CommandList.TextF( X, Y + TileSize + 2, FColor32::Gray, "[%d]", Index );
	}

	// コマを順番に切り替える（毎秒 6 コマ）。1 つの画像を、コマごとに替えるだけで、アニメーションになる
	if ( Pieces.Num() > 0 )
	{
		const int32 Frame = static_cast<int32>( Time * 6.0f ) % Pieces.Num();
		const int32 AnimY = Top + 2 * ( TileSize + TileGap + 16 ) + 12;
		CommandList.Text( Left, AnimY - 20, "Cycling through the cells:", FColor32::White );
		CommandList.ImageStretch( static_cast<float32>( Left ), static_cast<float32>( AnimY ), static_cast<float32>( Left + TileSize * 2 ), static_cast<float32>( AnimY + TileSize * 2 ), Pieces[Frame] );
		CommandList.TextF( Left + TileSize * 2 + 16, AnimY + TileSize, FColor32::Gray, "cell %d", Frame );
	}
}
