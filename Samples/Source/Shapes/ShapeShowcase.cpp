#include "ShapeShowcase.h"

#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"

namespace
{
	// 図形 1 つぶんの幅（ピクセル）。名前が重ならない間隔
	constexpr float32 CellWidth = 200.0f;
	// 図形の大きさの基準（ピクセル）。三角形・四角形の頂点が、中心からこの距離にある
	constexpr float32 Size = 56.0f;
	constexpr float32 TwoPi = 2.0f * NMath::PI;

	// 図形ごとの色。どの色も各成分が 0 か 255 か（半分）なので、なめらかな縁の途中の濃さと見分けやすい
	const FColor32 Red( 255, 0, 0 );
	const FColor32 Green( 0, 255, 0 );
	const FColor32 Cyan( 0, 255, 255 );
	const FColor32 Orange( 255, 128, 0 );
	const FColor32 Magenta( 255, 0, 255 );
	const FColor32 Yellow( 255, 255, 0 );

	// 整数の座標の図形に渡す座標（float を、一番近いピクセルに丸める）
	FORCEINLINE int32 Px( float32 Value )
	{
		return NMath::RoundToInt( Value );
	}

	// 中心 Center、半径 Radius の円周上の、角度 Angle（ラジアン）の点
	FORCEINLINE FVector2 OnCircle( const FVector2& Center, float32 Radius, float32 Angle )
	{
		return FVector2( Center.x + Radius * NMath::Cos( Angle ), Center.y + Radius * NMath::Sin( Angle ) );
	}
}

CShapeShowcase::CShapeShowcase( const FVector2& InOrigin, bool bInSmooth )
	: Origin( InOrigin )
	, bSmooth( bInSmooth )
{}

void CShapeShowcase::Tick( float32 DeltaTime )
{
	Time += DeltaTime;
}

void CShapeShowcase::Draw( FDrawContext& CommandList )
{

	// 6 つの図形を、左から並べる。名前は、図形の下
	const char* const PlainNames[6] = { "Triangle", "Quad", "Oval", "RoundBox", "Circle", "Line" };
	const char* const SmoothNames[6] = { "TriangleAA", "QuadAA", "OvalAA", "RoundBoxAA", "CircleAA", "LineAA" };
	for ( int32 Index = 0; Index < 6; ++Index )
	{
		const FVector2 Center( Origin.x + CellWidth * static_cast<float32>( Index ), Origin.y );
		switch ( Index )
		{
		case 0: DrawTriangle( CommandList, Center ); break;
		case 1: DrawQuad( CommandList, Center ); break;
		case 2: DrawOval( CommandList, Center ); break;
		case 3: DrawRoundBox( CommandList, Center ); break;
		case 4: DrawCircle( CommandList, Center ); break;
		default: DrawLine( CommandList, Center ); break;
		}
		CommandList.Text( Px( Center.x ) - 40, Px( Center.y ) + 90, bSmooth ? SmoothNames[Index] : PlainNames[Index], FColor32::White );
	}
}

void CShapeShowcase::DrawTriangle( FDrawContext& CommandList, const FVector2& Center ) const
{
	// 正三角形が、ゆっくり回る
	const float32 Angle = Time;
	const FVector2 A = OnCircle( Center, Size, Angle );
	const FVector2 B = OnCircle( Center, Size, Angle + TwoPi / 3.0f );
	const FVector2 C = OnCircle( Center, Size, Angle + TwoPi * 2.0f / 3.0f );
	if ( bSmooth )
	{
		CommandList.TriangleAA( A.x, A.y, B.x, B.y, C.x, C.y, Red );
	}
	else
	{
		CommandList.Triangle( Px( A.x ), Px( A.y ), Px( B.x ), Px( B.y ), Px( C.x ), Px( C.y ), Red );
	}
}

void CShapeShowcase::DrawQuad( FDrawContext& CommandList, const FVector2& Center ) const
{
	// 正方形（4 つの頂点を順にたどる四角形）が、逆向きにゆっくり回る
	const float32 Angle = -Time * 0.7f + NMath::PI * 0.25f;
	const FVector2 A = OnCircle( Center, Size, Angle );
	const FVector2 B = OnCircle( Center, Size, Angle + TwoPi * 0.25f );
	const FVector2 C = OnCircle( Center, Size, Angle + TwoPi * 0.5f );
	const FVector2 D = OnCircle( Center, Size, Angle + TwoPi * 0.75f );
	if ( bSmooth )
	{
		CommandList.QuadAA( A.x, A.y, B.x, B.y, C.x, C.y, D.x, D.y, Green );
	}
	else
	{
		CommandList.Quad( Px( A.x ), Px( A.y ), Px( B.x ), Px( B.y ), Px( C.x ), Px( C.y ), Px( D.x ), Px( D.y ), Green );
	}
}

void CShapeShowcase::DrawOval( FDrawContext& CommandList, const FVector2& Center ) const
{
	// 中心が、小さな円を描いて動く（動きがゆっくりなほど、整数の座標では 1 ピクセルずつのガタつきが目立つ）
	const FVector2 Moved = OnCircle( Center, 8.0f, Time * 2.0f );
	if ( bSmooth )
	{
		CommandList.OvalAA( Moved.x, Moved.y, 62.0f, 30.0f, Cyan );
	}
	else
	{
		CommandList.Oval( Px( Moved.x ), Px( Moved.y ), 62, 30, Cyan );
	}
}

void CShapeShowcase::DrawRoundBox( FDrawContext& CommandList, const FVector2& Center ) const
{
	// 横幅が伸び縮みする、角の丸い四角形
	const float32 HalfWidth = 40.0f + 8.0f * NMath::Sin( Time * 1.5f );
	const float32 HalfHeight = 28.0f;
	if ( bSmooth )
	{
		CommandList.RoundBoxAA( Center.x - HalfWidth, Center.y - HalfHeight, Center.x + HalfWidth, Center.y + HalfHeight, 14.0f, 14.0f, Orange );
	}
	else
	{
		CommandList.RoundBox( Px( Center.x - HalfWidth ), Px( Center.y - HalfHeight ), Px( Center.x + HalfWidth ), Px( Center.y + HalfHeight ), 14, 14, Orange );
	}
}

void CShapeShowcase::DrawCircle( FDrawContext& CommandList, const FVector2& Center ) const
{
	// 塗りつぶした円と、そのまわりの輪（太さ 3）が、小さな円を描いて動く
	const FVector2 Moved = OnCircle( Center, 10.0f, Time * 3.0f + 1.0f );
	if ( bSmooth )
	{
		CommandList.CircleAA( Moved.x, Moved.y, 34.0f, Magenta );
		CommandList.CircleAA( Moved.x, Moved.y, 46.0f, Magenta, false, 3.0f );
	}
	else
	{
		CommandList.Circle( Px( Moved.x ), Px( Moved.y ), 34, Magenta );
		CommandList.Circle( Px( Moved.x ), Px( Moved.y ), 46, Magenta, false, 3 );
	}
}

void CShapeShowcase::DrawLine( FDrawContext& CommandList, const FVector2& Center ) const
{
	// 中心を通る太い線が、回る
	const float32 Angle = Time * 1.3f;
	const FVector2 A = OnCircle( Center, Size, Angle );
	const FVector2 B = OnCircle( Center, Size, Angle + NMath::PI );
	if ( bSmooth )
	{
		CommandList.LineAA( A.x, A.y, B.x, B.y, Yellow, 4.0f );
	}
	else
	{
		CommandList.Line( Px( A.x ), Px( A.y ), Px( B.x ), Px( B.y ), Yellow, 4 );
	}
}
