#include "ImageShowcase.h"

#include "Library/Framework/Render/DrawContext.h"
#include "Source/Images/TestPattern.h"

namespace
{
	// 絵 1 つぶんの幅（ピクセル）。名前が重ならない間隔
	constexpr float32 CellWidth = 180.0f;
	// 画像の大きさ（ピクセル）
	constexpr float32 ImageSize = static_cast<float32>( NTestPattern::Size );

	// 画面の座標（float）を、整数の座標を取る描画（Text）に渡すために、一番近いピクセルに丸める
	FORCEINLINE int32 Px( float32 Value )
	{
		return NMath::RoundToInt( Value );
	}
}

CImageShowcase::CImageShowcase( const FVector2& InOrigin, const TSharedPtr<CTexture>& InTexture )
	: Origin( InOrigin )
	, Texture( InTexture )
{
}

void CImageShowcase::Tick( float32 DeltaTime )
{
	Time += DeltaTime;
}

void CImageShowcase::Draw( FDrawContext& CommandList )
{
	const char* const Names[7] = { "Image", "ImageStretch", "ImageQuad", "ImagePart", "SpritePivot", "Tile", "CircleGauge" };
	for ( int32 Index = 0; Index < 7; ++Index )
	{
		const FVector2 Center( Origin.x + CellWidth * static_cast<float32>( Index ), Origin.y );
		switch ( Index )
		{
		case 0: DrawImage( CommandList, Center ); break;
		case 1: DrawStretch( CommandList, Center ); break;
		case 2: DrawQuad( CommandList, Center ); break;
		case 3: DrawPart( CommandList, Center ); break;
		case 4: DrawPivot( CommandList, Center ); break;
		case 5: DrawTile( CommandList, Center ); break;
		default: DrawGauge( CommandList, Center ); break;
		}
		CommandList.Text( Px( Center.x ) - 40, Px( Center.y ) + 90, Names[Index], FColor32::White );
	}
}

void CImageShowcase::DrawImage( FDrawContext& CommandList, const FVector2& Center ) const
{
	// 等倍。(X, Y) が画像の左上
	CommandList.Image( Center.x - ImageSize * 0.5f, Center.y - ImageSize * 0.5f, Texture );
}

void CImageShowcase::DrawStretch( FDrawContext& CommandList, const FVector2& Center ) const
{
	// 矩形に合わせて拡大・縮小（横に 1.5 倍、縦に 0.75 倍）
	CommandList.ImageStretch( Center.x - 48.0f, Center.y - 24.0f, Center.x + 48.0f, Center.y + 24.0f, Texture );
}

void CImageShowcase::DrawQuad( FDrawContext& CommandList, const FVector2& Center ) const
{
	// 4 つの角へ自由に変形。上の辺の幅が、伸び縮みする台形（画像の 左上・右上・右下・左下 の順）
	const float32 Top = 24.0f + 16.0f * NMath::Sin( Time * 1.5f );
	CommandList.ImageQuad( Center.x - Top, Center.y - 32.0f, Center.x + Top, Center.y - 32.0f, Center.x + 40.0f, Center.y + 32.0f, Center.x - 40.0f, Center.y + 32.0f, Texture );
}

void CImageShowcase::DrawPart( FDrawContext& CommandList, const FVector2& Center ) const
{
	// 画像の一部分（左上の 32 x 32）を等倍で。上は、そのまま。下は、上下左右を反転して
	CommandList.ImagePart( Center.x - 16.0f, Center.y - 34.0f, 0, 0, 32, 32, Texture );
	CommandList.ImagePart( Center.x - 16.0f, Center.y + 2.0f, 0, 0, 32, 32, Texture, true, true );
}

void CImageShowcase::DrawPivot( FDrawContext& CommandList, const FVector2& Center ) const
{
	// 基準点（画像の中心）が (X, Y) に来て、そのまわりを回りながら、横と縦が別々の拡大率で伸び縮みする
	const float32 Pulse = 0.3f * NMath::Sin( Time * 2.0f );
	CommandList.SpritePivot( Center.x, Center.y, Texture, ImageSize * 0.5f, ImageSize * 0.5f, 0.8f + Pulse, 0.8f - Pulse, Time );
}

void CImageShowcase::DrawTile( FDrawContext& CommandList, const FVector2& Center ) const
{
	// 矩形を、画像の繰り返し（半分の大きさ）で埋める。並びが、ゆっくり右下へずれていく
	const int32 OffsetX = static_cast<int32>( Time * 24.0f );
	const int32 OffsetY = static_cast<int32>( Time * 12.0f );
	CommandList.Tile( Px( Center.x ) - 80, Px( Center.y ) - 32, Px( Center.x ) + 80, Px( Center.y ) + 32, Texture, OffsetX, OffsetY, 0.5f );
}

void CImageShowcase::DrawGauge( FDrawContext& CommandList, const FVector2& Center ) const
{
	// 円グラフのように、真上から時計回りに描く量が、0 から 1 を繰り返す（クールダウンの表示などに）
	const float32 Fraction = NMath::Fmod( Time * 0.25f, 1.0f );
	CommandList.CircleGauge( Center.x, Center.y, Fraction, Texture );
}
