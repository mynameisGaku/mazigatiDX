// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/PixelRaster.h"

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Sort.h"
#include "Library/Math/MathUtils.h"

namespace
{
	// 1 つの図形で、同じ画素を 2 回呼ばないための、覆った画素の記録（図形の外接矩形の大きさ）
	class FVisitedPixels
	{
	public:
		FVisitedPixels( int32 InMinX, int32 InMinY, int32 InMaxX, int32 InMaxY )
			: MinX( InMinX )
			, MinY( InMinY )
			, Width( InMaxX - InMinX + 1 )
			, Height( InMaxY - InMinY + 1 )
		{
			Seen.SetNum( Width * Height, 0 );
		}

		// まだ呼んでいない画素なら、印を付けて true
		bool Mark( int32 X, int32 Y )
		{
			const int32 LocalX = X - MinX;
			const int32 LocalY = Y - MinY;
			if ( LocalX < 0 || LocalY < 0 || LocalX >= Width || LocalY >= Height )
			{
				return false;
			}
			uint8& Cell = Seen[LocalY * Width + LocalX];
			if ( Cell != 0 )
			{
				return false;
			}
			Cell = 1;
			return true;
		}

	private:
		int32 MinX;
		int32 MinY;
		int32 Width;
		int32 Height;
		TArray<uint8> Seen;
	};

	// 1 つの図形が覆える画素の数の上限（大きすぎる図形で、記録が膨らまないように）
	constexpr int64 MaxPixelsPerShape = 4096 * 4096;

	bool IsFiniteVector( const FVector2& V )
	{
		return NMath::IsFinite( V.x ) && NMath::IsFinite( V.y );
	}

	int32 FloorToInt( float32 Value )
	{
		return static_cast<int32>( NMath::Floor( Value ) );
	}

	bool IsSmallEnough( int32 MinX, int32 MinY, int32 MaxX, int32 MaxY )
	{
		return MaxX >= MinX && MaxY >= MinY && static_cast<int64>( MaxX - MinX + 1 ) * static_cast<int64>( MaxY - MinY + 1 ) <= MaxPixelsPerShape;
	}

	// 中心からの距離の 2 乗が、[InnerSquared, OuterSquared] の画素（輪・円の共通の形）
	void Annulus( const FVector2& Center, float32 Inner, float32 Outer, float32 Start, float32 End, bool bWhole, const NPixelRaster::FVisit& Visit )
	{
		if ( !IsFiniteVector( Center ) || !NMath::IsFinite( Outer ) || Outer <= 0.0f )
		{
			return;
		}
		const int32 MinX = FloorToInt( Center.x - Outer );
		const int32 MaxX = FloorToInt( Center.x + Outer );
		const int32 MinY = FloorToInt( Center.y - Outer );
		const int32 MaxY = FloorToInt( Center.y + Outer );
		if ( !IsSmallEnough( MinX, MinY, MaxX, MaxY ) )
		{
			return;
		}
		const float32 InnerSquared = Inner > 0.0f ? Inner * Inner : -1.0f;
		const float32 OuterSquared = Outer * Outer;
		for ( int32 Y = MinY; Y <= MaxY; ++Y )
		{
			for ( int32 X = MinX; X <= MaxX; ++X )
			{
				const float32 OffsetX = static_cast<float32>( X ) + 0.5f - Center.x;
				const float32 OffsetY = static_cast<float32>( Y ) + 0.5f - Center.y;
				const float32 DistanceSquared = OffsetX * OffsetX + OffsetY * OffsetY;
				if ( DistanceSquared > OuterSquared || DistanceSquared < InnerSquared )
				{
					continue;
				}
				if ( !bWhole )
				{
					// Start から測った角度が、[0, End - Start] に入るか
					float32 Angle = NMath::Atan2( OffsetY, OffsetX ) - Start;
					Angle = Angle - NMath::TWO_PI * NMath::Floor( Angle / NMath::TWO_PI );
					if ( Angle > End - Start )
					{
						continue;
					}
				}
				Visit( X, Y );
			}
		}
	}
}

void NPixelRaster::Line( const FVector2& A, const FVector2& B, int32 Width, const FVisit& Visit )
{
	if ( !IsFiniteVector( A ) || !IsFiniteVector( B ) )
	{
		return;
	}
	int32 X = FloorToInt( A.x );
	int32 Y = FloorToInt( A.y );
	const int32 EndX = FloorToInt( B.x );
	const int32 EndY = FloorToInt( B.y );
	const int32 Thickness = NMath::Max( 1, Width );
	const int32 Low = ( Thickness - 1 ) / 2;
	const int32 High = Thickness - 1 - Low;
	const int32 MinX = NMath::Min( X, EndX ) - Low;
	const int32 MaxX = NMath::Max( X, EndX ) + High;
	const int32 MinY = NMath::Min( Y, EndY ) - Low;
	const int32 MaxY = NMath::Max( Y, EndY ) + High;
	if ( !IsSmallEnough( MinX, MinY, MaxX, MaxY ) )
	{
		return;
	}
	FVisitedPixels Visited( MinX, MinY, MaxX, MaxY );
	const int32 DeltaX = NMath::Abs( EndX - X );
	const int32 DeltaY = -NMath::Abs( EndY - Y );
	const int32 StepX = X < EndX ? 1 : -1;
	const int32 StepY = Y < EndY ? 1 : -1;
	int32 Error = DeltaX + DeltaY;
	for ( ;; )
	{
		for ( int32 OffsetY = -Low; OffsetY <= High; ++OffsetY )
		{
			for ( int32 OffsetX = -Low; OffsetX <= High; ++OffsetX )
			{
				if ( Visited.Mark( X + OffsetX, Y + OffsetY ) )
				{
					Visit( X + OffsetX, Y + OffsetY );
				}
			}
		}
		if ( X == EndX && Y == EndY )
		{
			break;
		}
		const int32 Twice = Error * 2;
		if ( Twice >= DeltaY )
		{
			Error += DeltaY;
			X += StepX;
		}
		if ( Twice <= DeltaX )
		{
			Error += DeltaX;
			Y += StepY;
		}
	}
}

void NPixelRaster::Disc( const FVector2& Center, float32 Radius, const FVisit& Visit )
{
	Annulus( Center, 0.0f, Radius, 0.0f, 0.0f, true, Visit );
}

void NPixelRaster::Ring( const FVector2& Center, float32 Radius, int32 Width, const FVisit& Visit )
{
	const float32 Half = static_cast<float32>( NMath::Max( 1, Width ) ) * 0.5f;
	Annulus( Center, Radius - Half, Radius + Half, 0.0f, 0.0f, true, Visit );
}

void NPixelRaster::Arc( const FVector2& Center, float32 Radius, float32 Start, float32 End, int32 Width, const FVisit& Visit )
{
	if ( !NMath::IsFinite( Start ) || !NMath::IsFinite( End ) || End < Start )
	{
		return;
	}
	const float32 Half = static_cast<float32>( NMath::Max( 1, Width ) ) * 0.5f;
	Annulus( Center, Radius - Half, Radius + Half, Start, End, End - Start >= NMath::TWO_PI, Visit );
}

void NPixelRaster::Ellipse( const FVector2& Center, const FVector2& Radius, int32 Width, const FVisit& Visit )
{
	if ( !IsFiniteVector( Center ) || !IsFiniteVector( Radius ) || Radius.x <= 0.0f || Radius.y <= 0.0f )
	{
		return;
	}
	// 外側の楕円の中で、内側の楕円の外にある画素（どちらも半径を太さの半分ずつ広げ・狭める）
	const float32 Half = static_cast<float32>( NMath::Max( 1, Width ) ) * 0.5f;
	const FVector2 Outer( Radius.x + Half, Radius.y + Half );
	const FVector2 Inner( Radius.x - Half, Radius.y - Half );
	const int32 MinX = FloorToInt( Center.x - Outer.x );
	const int32 MaxX = FloorToInt( Center.x + Outer.x );
	const int32 MinY = FloorToInt( Center.y - Outer.y );
	const int32 MaxY = FloorToInt( Center.y + Outer.y );
	if ( !IsSmallEnough( MinX, MinY, MaxX, MaxY ) )
	{
		return;
	}
	for ( int32 Y = MinY; Y <= MaxY; ++Y )
	{
		for ( int32 X = MinX; X <= MaxX; ++X )
		{
			const float32 OffsetX = static_cast<float32>( X ) + 0.5f - Center.x;
			const float32 OffsetY = static_cast<float32>( Y ) + 0.5f - Center.y;
			const float32 OuterValue = ( OffsetX * OffsetX ) / ( Outer.x * Outer.x ) + ( OffsetY * OffsetY ) / ( Outer.y * Outer.y );
			if ( OuterValue > 1.0f )
			{
				continue;
			}
			const bool bInsideInner = Inner.x > 0.0f && Inner.y > 0.0f && ( OffsetX * OffsetX ) / ( Inner.x * Inner.x ) + ( OffsetY * OffsetY ) / ( Inner.y * Inner.y ) < 1.0f;
			if ( !bInsideInner )
			{
				Visit( X, Y );
			}
		}
	}
}

void NPixelRaster::Polygon( TArrayView<const FVector2> Points, const FVisit& Visit )
{
	if ( Points.Num() < 3 )
	{
		return;
	}
	float32 Top = Points[0].y;
	float32 Bottom = Points[0].y;
	for ( int32 Index = 0; Index < Points.Num(); ++Index )
	{
		if ( !IsFiniteVector( Points[Index] ) )
		{
			return;
		}
		Top = NMath::Min( Top, Points[Index].y );
		Bottom = NMath::Max( Bottom, Points[Index].y );
	}
	TArray<float32> Crossings;
	for ( int32 Y = FloorToInt( Top ); Y <= FloorToInt( Bottom ); ++Y )
	{
		// 画素の中心の高さで、辺と交わる x を集めて、左から 2 つずつの間を塗る
		const float32 SampleY = static_cast<float32>( Y ) + 0.5f;
		Crossings.Reset();
		for ( int32 Index = 0; Index < Points.Num(); ++Index )
		{
			const FVector2& From = Points[Index];
			const FVector2& To = Points[( Index + 1 ) % Points.Num()];
			if ( ( From.y <= SampleY && To.y > SampleY ) || ( To.y <= SampleY && From.y > SampleY ) )
			{
				Crossings.Add( From.x + ( SampleY - From.y ) / ( To.y - From.y ) * ( To.x - From.x ) );
			}
		}
		NAlgo::Sort( Crossings.GetData(), Crossings.Num() );
		for ( int32 Pair = 0; Pair + 1 < Crossings.Num(); Pair += 2 )
		{
			// 中心が [左, 右) に入る画素
			const int32 Left = static_cast<int32>( NMath::Ceil( Crossings[Pair] - 0.5f ) );
			const int32 Right = static_cast<int32>( NMath::Ceil( Crossings[Pair + 1] - 0.5f ) ) - 1;
			for ( int32 X = Left; X <= Right; ++X )
			{
				Visit( X, Y );
			}
		}
	}
}

void NPixelRaster::Rect( const FVector2& TopLeft, const FVector2& Size, const FVisit& Visit )
{
	if ( !IsFiniteVector( TopLeft ) || !IsFiniteVector( Size ) || Size.x <= 0.0f || Size.y <= 0.0f )
	{
		return;
	}
	const int32 Left = static_cast<int32>( NMath::Ceil( TopLeft.x - 0.5f ) );
	const int32 Right = static_cast<int32>( NMath::Ceil( TopLeft.x + Size.x - 0.5f ) ) - 1;
	const int32 Top = static_cast<int32>( NMath::Ceil( TopLeft.y - 0.5f ) );
	const int32 Bottom = static_cast<int32>( NMath::Ceil( TopLeft.y + Size.y - 0.5f ) ) - 1;
	if ( !IsSmallEnough( Left, Top, Right, Bottom ) )
	{
		return;
	}
	for ( int32 Y = Top; Y <= Bottom; ++Y )
	{
		for ( int32 X = Left; X <= Right; ++X )
		{
			Visit( X, Y );
		}
	}
}
