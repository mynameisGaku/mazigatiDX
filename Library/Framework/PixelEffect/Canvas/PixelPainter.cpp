// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/Canvas/PixelPainter.h"

#include "Library/Framework/PixelEffect/Canvas/PixelDither.h"
#include "Library/Framework/PixelEffect/Canvas/PixelFont.h"
#include "Library/Framework/PixelEffect/Canvas/PixelRaster.h"
#include "Library/Math/MathUtils.h"

CPixelPainter::CPixelPainter( IPixelSurface& InSurface, const FVector2& InOrigin, bool bInFlipHorizontal )
	: Surface( InSurface )
	, Origin( InOrigin )
	, bFlipHorizontal( bInFlipHorizontal )
{
}

void CPixelPainter::Dot( const FVector2& Position, const FColor32& Color, const FPixelStroke& Stroke ) const
{
	const FVector2 Point = ToSurfacePixelCenter( Position );
	if ( !NMath::IsFinite( Point.x ) || !NMath::IsFinite( Point.y ) )
	{
		return;
	}
	Plot( static_cast<int32>( NMath::Floor( Point.x ) ), static_cast<int32>( NMath::Floor( Point.y ) ), Color, Stroke );
}

void CPixelPainter::Line( const FVector2& From, const FVector2& To, const FColor32& Color, const FPixelStroke& Stroke ) const
{
	// 両端は、点と同じく画素の中心で選ぶ（左右反転でも、端の画素が鏡写しになる）
	NPixelRaster::Line( ToSurfacePixelCenter( From ), ToSurfacePixelCenter( To ), Stroke.Width, [ & ]( int32 X, int32 Y )
	{
		Plot( X, Y, Color, Stroke );
	} );
}

void CPixelPainter::Disc( const FVector2& Center, float32 Radius, const FColor32& Color, const FPixelStroke& Stroke ) const
{
	NPixelRaster::Disc( ToSurface( Center ), Radius, [ & ]( int32 X, int32 Y )
	{
		Plot( X, Y, Color, Stroke );
	} );
}

void CPixelPainter::Ring( const FVector2& Center, float32 Radius, const FColor32& Color, const FPixelStroke& Stroke ) const
{
	NPixelRaster::Ring( ToSurface( Center ), Radius, Stroke.Width, [ & ]( int32 X, int32 Y )
	{
		Plot( X, Y, Color, Stroke );
	} );
}

void CPixelPainter::Arc( const FVector2& Center, float32 Radius, float32 Start, float32 End, const FColor32& Color, const FPixelStroke& Stroke ) const
{
	// 左右反転すると、角度 a は π - a になる（範囲の始まりと終わりが入れ替わる）
	const float32 SurfaceStart = bFlipHorizontal ? NMath::PI - End : Start;
	const float32 SurfaceEnd = bFlipHorizontal ? NMath::PI - Start : End;
	NPixelRaster::Arc( ToSurface( Center ), Radius, SurfaceStart, SurfaceEnd, Stroke.Width, [ & ]( int32 X, int32 Y )
	{
		Plot( X, Y, Color, Stroke );
	} );
}

void CPixelPainter::Ellipse( const FVector2& Center, const FVector2& Radius, const FColor32& Color, const FPixelStroke& Stroke ) const
{
	NPixelRaster::Ellipse( ToSurface( Center ), Radius, Stroke.Width, [ & ]( int32 X, int32 Y )
	{
		Plot( X, Y, Color, Stroke );
	} );
}

void CPixelPainter::Polygon( TArrayView<const FVector2> Points, const FColor32& Color, const FPixelStroke& Stroke ) const
{
	TArray<FVector2> SurfacePoints;
	SurfacePoints.Reserve( Points.Num() );
	for ( int32 Index = 0; Index < Points.Num(); ++Index )
	{
		SurfacePoints.Add( ToSurface( Points[Index] ) );
	}
	NPixelRaster::Polygon( SurfacePoints, [ & ]( int32 X, int32 Y )
	{
		Plot( X, Y, Color, Stroke );
	} );
}

void CPixelPainter::Rect( const FVector2& TopLeft, const FVector2& Size, const FColor32& Color, const FPixelStroke& Stroke ) const
{
	// 左右反転すると、右端が左上の x になる
	const FVector2 Corner = bFlipHorizontal ? ToSurface( FVector2( TopLeft.x + Size.x, TopLeft.y ) ) : ToSurface( TopLeft );
	NPixelRaster::Rect( Corner, Size, [ & ]( int32 X, int32 Y )
	{
		Plot( X, Y, Color, Stroke );
	} );
}

void CPixelPainter::Glow( const FVector2& Center, float32 Radius, const FColor32& Color, float32 Strength ) const
{
	if ( !( Strength > 0.0f ) || !( Radius > 0.0f ) )
	{
		return;
	}
	const FVector2 SurfaceCenter = ToSurface( Center );
	NPixelRaster::Disc( SurfaceCenter, Radius, [ & ]( int32 X, int32 Y )
	{
		// 中心からの距離に応じて、2 乗で弱める
		const float32 OffsetX = static_cast<float32>( X ) + 0.5f - SurfaceCenter.x;
		const float32 OffsetY = static_cast<float32>( Y ) + 0.5f - SurfaceCenter.y;
		const float32 Falloff = 1.0f - NMath::Min( 1.0f, NMath::Sqrt( OffsetX * OffsetX + OffsetY * OffsetY ) / Radius );
		Surface.AddLight( X, Y, Color, Falloff * Falloff * Strength );
	} );
}

void CPixelPainter::Text( const FString& Text, const FVector2& Position, const FColor32& Color, const FPixelTextStyle& Style ) const
{
	const int32 Scale = NMath::Max( 1, Style.Scale );
	const float32 Width = static_cast<float32>( NPixelFont::MeasureWidth( Text, Style.Font ) * Scale );
	FVector2 TopLeft = ToSurface( Position );
	// 左右反転では、字は裏返さず、寄せ方だけを左右入れ替える
	EPixelTextAlign Alignment = Style.Alignment;
	if ( bFlipHorizontal && Alignment != EPixelTextAlign::Center )
	{
		Alignment = Alignment == EPixelTextAlign::Left ? EPixelTextAlign::Right : EPixelTextAlign::Left;
	}
	if ( Alignment == EPixelTextAlign::Center )
	{
		TopLeft.x -= NMath::Floor( Width * 0.5f );
	}
	else if ( Alignment == EPixelTextAlign::Right )
	{
		TopLeft.x -= Width;
	}
	if ( !NMath::IsFinite( TopLeft.x ) || !NMath::IsFinite( TopLeft.y ) )
	{
		return;
	}
	const int32 Left = static_cast<int32>( NMath::Floor( TopLeft.x ) );
	const int32 Top = static_cast<int32>( NMath::Floor( TopLeft.y ) );

	// 縁取りは、文字の画素を上下左右に 1 つずらして先に描き、その上に文字を描く
	const auto DrawGlyphs = [ & ]( int32 ShiftX, int32 ShiftY, const FColor32& InkColor )
	{
		NPixelFont::Rasterize( Text, Style.Font, [ & ]( int32 GlyphX, int32 GlyphY )
		{
			for ( int32 SubY = 0; SubY < Scale; ++SubY )
			{
				for ( int32 SubX = 0; SubX < Scale; ++SubX )
				{
					Surface.SetPixel( Left + GlyphX * Scale + SubX + ShiftX, Top + GlyphY * Scale + SubY + ShiftY, InkColor );
				}
			}
		} );
	};
	if ( Style.bOutline )
	{
		DrawGlyphs( -1, 0, Style.OutlineColor );
		DrawGlyphs( 1, 0, Style.OutlineColor );
		DrawGlyphs( 0, -1, Style.OutlineColor );
		DrawGlyphs( 0, 1, Style.OutlineColor );
	}
	DrawGlyphs( 0, 0, Color );
}

void CPixelPainter::Mask( const FPixelMask& Shape, const FVector2& TopLeft, const FColor32& Color, const FPixelStroke& Stroke ) const
{
	const FVector2 Corner = bFlipHorizontal ? ToSurface( FVector2( TopLeft.x + static_cast<float32>( Shape.Width ), TopLeft.y ) ) : ToSurface( TopLeft );
	if ( !Shape.IsValid() || !NMath::IsFinite( Corner.x ) || !NMath::IsFinite( Corner.y ) )
	{
		return;
	}
	const int32 Left = static_cast<int32>( NMath::Floor( Corner.x ) );
	const int32 Top = static_cast<int32>( NMath::Floor( Corner.y ) );
	for ( int32 Y = 0; Y < Shape.Height; ++Y )
	{
		for ( int32 X = 0; X < Shape.Width; ++X )
		{
			if ( Shape.IsSet( bFlipHorizontal ? Shape.Width - 1 - X : X, Y ) )
			{
				Plot( Left + X, Top + Y, Color, Stroke );
			}
		}
	}
}

void CPixelPainter::Actor( const FPixelActor& Figure, const FVector2& Feet, float32 Alpha ) const
{
	const FVector2 Corner = ToSurface( Feet ) - FVector2( static_cast<float32>( Figure.Width / 2 ), static_cast<float32>( Figure.Height ) );
	if ( Figure.Pixels.Num() != Figure.Width * Figure.Height || !NMath::IsFinite( Corner.x ) || !NMath::IsFinite( Corner.y ) )
	{
		return;
	}
	const int32 Left = static_cast<int32>( NMath::Floor( Corner.x ) );
	const int32 Top = static_cast<int32>( NMath::Floor( Corner.y ) );
	FPixelStroke Stroke;
	Stroke.Alpha = Alpha;
	for ( int32 Y = 0; Y < Figure.Height; ++Y )
	{
		for ( int32 X = 0; X < Figure.Width; ++X )
		{
			const int32 SourceX = bFlipHorizontal ? Figure.Width - 1 - X : X;
			const FColor32& Color = Figure.Pixels[Y * Figure.Width + SourceX];
			if ( Color.a != 0 )
			{
				Plot( Left + X, Top + Y, Color, Stroke );
			}
		}
	}
}

FColor32 CPixelPainter::GetPixel( const FVector2& Position ) const
{
	const FVector2 Point = ToSurfacePixelCenter( Position );
	if ( !NMath::IsFinite( Point.x ) || !NMath::IsFinite( Point.y ) )
	{
		return FColor32( 0, 0, 0, 0 );
	}
	return Surface.GetPixel( static_cast<int32>( NMath::Floor( Point.x ) ), static_cast<int32>( NMath::Floor( Point.y ) ) );
}

FVector2 CPixelPainter::ToSurface( const FVector2& Position ) const
{
	return Origin + FVector2( bFlipHorizontal ? -Position.x : Position.x, Position.y );
}

FVector2 CPixelPainter::ToSurfacePixelCenter( const FVector2& Position ) const
{
	return ToSurface( FVector2( NMath::Floor( Position.x ) + 0.5f, NMath::Floor( Position.y ) + 0.5f ) );
}

void CPixelPainter::Plot( int32 X, int32 Y, const FColor32& Color, const FPixelStroke& Stroke ) const
{
	if ( !NPixelDither::CanDraw( X, Y, Stroke.Alpha ) )
	{
		return;
	}
	Surface.SetPixel( X, Y, Color );
	if ( Stroke.Glow > 0.0f )
	{
		Surface.AddLight( X, Y, Color, Stroke.Glow );
	}
}
