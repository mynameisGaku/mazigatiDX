#include "ViewShowcase.h"

#include "Library/Framework/Render/DrawContext.h"
#include "Source/Common/TestPattern.h"

namespace
{
	// クリップの絵の枠の大きさ（ピクセル）
	constexpr int32 ClipAreaWidth = 240;
	constexpr int32 ClipAreaHeight = 250;
	// クリップの窓の大きさ（ピクセル）
	constexpr int32 WindowHalfWidth = 70;
	constexpr int32 WindowHalfHeight = 45;
	// 描画モードの絵の大きさ（ピクセル）と、拡大する画像の一部分（画像の中の左上と大きさ）
	constexpr int32 FilterSize = 128;
	constexpr int32 SourceLeft = 6;
	constexpr int32 SourceTop = 6;
	constexpr int32 SourceSize = 4;
	// 2D の変換の絵（カメラの窓）の大きさ（ピクセル）
	constexpr int32 CameraWidth = 560;
	constexpr int32 CameraHeight = 290;
	// 絵と絵のあいだの間隔（ピクセル）
	constexpr int32 Gap = 40;
	// 見出しから絵までの高さ（ピクセル）
	constexpr int32 TitleHeight = 30;

	// 画面の座標（float）を、整数の座標を取る描画に渡すために、一番近いピクセルに丸める
	FORCEINLINE int32 Px( float32 Value )
	{
		return NMath::RoundToInt( Value );
	}
}

CViewShowcase::CViewShowcase( const FVector2& InOrigin, const TSharedPtr<CTexture>& InTexture )
	: Origin( InOrigin )
	, Texture( InTexture )
{
}

void CViewShowcase::Tick( float32 DeltaTime )
{
	Time += DeltaTime;
}

void CViewShowcase::Draw( FDrawContext& CommandList )
{
	const int32 Top = Px( Origin.y ) + TitleHeight;
	const int32 ClipLeft = Px( Origin.x );
	const int32 FilterLeft = ClipLeft + ClipAreaWidth + Gap;
	const int32 CameraLeft = FilterLeft + FilterSize * 2 + Gap * 2 + 20;

	CommandList.Text( ClipLeft, Px( Origin.y ), "FClipScope", FColor32::White );
	CommandList.Text( FilterLeft, Px( Origin.y ), "Filter( Nearest ) / Filter( Bilinear )", FColor32::White );
	CommandList.Text( CameraLeft, Px( Origin.y ), "Transform2D (a camera) + FClipScope (the window)", FColor32::White );
	DrawClip( CommandList, ClipLeft, Top );
	DrawFilter( CommandList, FilterLeft, Top );
	DrawCamera( CommandList, CameraLeft, Top );
}

void CViewShowcase::DrawClip( FDrawContext& CommandList, int32 Left, int32 Top ) const
{
	// 全体の枠（窓の外にも見えるよう、クリップの外で描く）
	CommandList.Box( Left, Top, Left + ClipAreaWidth, Top + ClipAreaHeight, FColor32( 90, 90, 90 ), false );

	// 窓は、枠の中をゆっくり回る
	const int32 CenterX = Left + ClipAreaWidth / 2 + Px( 50.0f * NMath::Cos( Time * 0.8f ) );
	const int32 CenterY = Top + ClipAreaHeight / 2 + Px( 60.0f * NMath::Sin( Time * 1.1f ) );
	{
		// この中の描画は、窓 (CenterX ± WindowHalfWidth, CenterY ± WindowHalfHeight) の外へはみ出さない
		FClipScope Window( CommandList, CenterX - WindowHalfWidth, CenterY - WindowHalfHeight, CenterX + WindowHalfWidth, CenterY + WindowHalfHeight );
		for ( int32 Offset = -ClipAreaHeight; Offset < ClipAreaWidth; Offset += 16 )
		{
			// 枠より広く描くが、窓の中の縞しか見えない
			CommandList.Line( Left + Offset, Top + ClipAreaHeight, Left + Offset + ClipAreaHeight, Top, FColor32( 90, 190, 120 ), 6 );
		}
		{
			// 窓の中の、さらに小さい窓（入れ子。外の窓との共通部分になる）
			FClipScope Inner( CommandList, CenterX - 20, CenterY - WindowHalfHeight - 20, CenterX + 20, CenterY + 20 );
			CommandList.Circle( CenterX, CenterY, 50, FColor32( 240, 200, 60 ) );
		}
	}
	// 窓の枠は、クリップの外で描く
	CommandList.Box( CenterX - WindowHalfWidth, CenterY - WindowHalfHeight, CenterX + WindowHalfWidth, CenterY + WindowHalfHeight, FColor32::White, false );
}

void CViewShowcase::DrawFilter( FDrawContext& CommandList, int32 Left, int32 Top ) const
{
	// 同じ画像の一部分 (4 x 4。左上の白いマスと、そのまわりの色のマスの境目) を、32 倍に拡大する。拡大・縮小・回転して描くときだけ、描画モードが効く
	const EDrawFilter Modes[2] = { EDrawFilter::Nearest, EDrawFilter::Bilinear };
	const char* const Names[2] = { "Nearest", "Bilinear" };
	for ( int32 Index = 0; Index < 2; ++Index )
	{
		const int32 X = Left + ( FilterSize + Gap ) * Index;
		CommandList.Filter( Modes[Index] );
		CommandList.ImagePartStretch( static_cast<float32>( X ), static_cast<float32>( Top ), static_cast<float32>( X + FilterSize ), static_cast<float32>( Top + FilterSize ), SourceLeft, SourceTop, SourceSize, SourceSize, Texture );
		CommandList.Text( X, Top + FilterSize + 8, Names[Index], FColor32::White );
	}
	// 戻しておく（このあとの画像が、なめらかに拡大されるように変わってしまわない）
	CommandList.Filter( EDrawFilter::Nearest );
}

void CViewShowcase::DrawWorld( FDrawContext& CommandList ) const
{
	// 世界の格子（64 ごと）。原点の線は明るく
	for ( int32 Index = -10; Index <= 10; ++Index )
	{
		const FColor32 Color = Index == 0 ? FColor32( 150, 150, 150 ) : FColor32( 60, 60, 70 );
		CommandList.Line( Index * 64, -640, Index * 64, 640, Color );
		CommandList.Line( -640, Index * 64, 640, Index * 64, Color );
	}
	// 画像・図形を、世界の座標のまま置く
	const float32 Half = static_cast<float32>( NTestPattern::Size ) * 0.5f;
	CommandList.Image( -Half, -Half, Texture );
	CommandList.Image( 160.0f - Half, -100.0f - Half, Texture );
	CommandList.Image( -200.0f - Half, 90.0f - Half, Texture );
	CommandList.Circle( 220, 110, 28, FColor32( 240, 200, 60 ) );
	CommandList.Box( -130, -150, -70, -90, FColor32( 230, 90, 60 ) );
}

void CViewShowcase::DrawCamera( FDrawContext& CommandList, int32 Left, int32 Top ) const
{
	// 窓の枠
	CommandList.Box( Left, Top, Left + CameraWidth, Top + CameraHeight, FColor32( 90, 90, 90 ), false );

	// カメラは、世界の中を回りながら、拡大・回転する。世界の座標 -> 画面の座標 の行列 =
	// 世界の座標を、カメラの位置が原点になるようにずらす -> 拡大 -> 回転 -> 窓の中心へ（行ベクトルなので、左から順に掛かる）
	const FVector3 CameraPosition( 120.0f * NMath::Cos( Time * 0.5f ), 80.0f * NMath::Sin( Time * 0.7f ), 0.0f );
	const float32 Zoom = 1.0f + 0.4f * NMath::Sin( Time * 0.6f );
	const float32 Angle = 0.35f * NMath::Sin( Time * 0.4f );
	const FVector3 WindowCenter( static_cast<float32>( Left + CameraWidth / 2 ), static_cast<float32>( Top + CameraHeight / 2 ), 0.0f );
	const FMatrix4x4 View = FMatrix4x4::Translate( FVector3( -CameraPosition.x, -CameraPosition.y, 0.0f ) ) * FMatrix4x4::Scale( Zoom ) * FMatrix4x4::RotateZ( Angle ) * FMatrix4x4::Translate( WindowCenter );

	// クリップは、変換のあとの画面の座標で決まる（変換に影響されない）ので、窓の外へはみ出さない
	FClipScope Window( CommandList, Left, Top, Left + CameraWidth, Top + CameraHeight );
	CommandList.Transform2D( View );
	DrawWorld( CommandList );
	// 戻しておく（このあと変換なしで描きたいものがあれば、そのまま描ける）
	CommandList.ResetTransform2D();
}
