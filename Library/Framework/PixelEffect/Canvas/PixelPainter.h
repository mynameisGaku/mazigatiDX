// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/PixelEffect/Canvas/IPixelSurface.h"
#include "Library/Framework/PixelEffect/Playback/PixelEffectTypes.h"

//=============================================================================
// CPixelPainter
//   IPixelSurface へ、図形・文字・シルエット・光を描く道具。どれも、覆う画素を NPixelRaster で求め、1 画素ずつ描き先へ渡す。
//   座標は、Origin を (0, 0) とした相対（エフェクトの基準点からの画素。右が +x、下が +y）。
//   bFlipHorizontal なら、x を左右に反転して描く（向きのある演出を、左向きにも使える。文字は位置だけ反転し、字は裏返さない）。
//   ディザの模様は描き先の画素の位置で決まる（反転しても模様は裏返らず、動く図形でも模様が画面に貼り付いたまま揺れない）。
//   描き方は FPixelStroke（濃さはディザで間引く・太さ・同じ画素に足す光）で決め、図形ごとの「薄い版」「光る版」は作らない。
//
//   CPixelPainter Painter( Surface, Placement.Position );
//   Painter.Disc( FVector2( 0, -8 ), 6.0f, Orange );
//   Painter.Line( FVector2( -10, 0 ), FVector2( 10, 0 ), White, FPixelStroke { 0.5f, 1, 0.8f } );
//   Painter.Glow( FVector2( 0, -8 ), 14.0f, Orange, 0.6f );
//=============================================================================
class CPixelPainter
{
public:
	CPixelPainter( IPixelSurface& InSurface, const FVector2& InOrigin = FVector2(), bool bInFlipHorizontal = false );

	FORCEINLINE IPixelSurface& GetSurface() const
	{
		return Surface;
	}

	FORCEINLINE const FVector2& GetOrigin() const
	{
		return Origin;
	}

	FORCEINLINE bool IsFlippedHorizontally() const
	{
		return bFlipHorizontal;
	}

	// 1 画素
	void Dot( const FVector2& Position, const FColor32& Color, const FPixelStroke& Stroke = FPixelStroke() ) const;

	// 線（両端を含む。太さは Stroke.Width）
	void Line( const FVector2& From, const FVector2& To, const FColor32& Color, const FPixelStroke& Stroke = FPixelStroke() ) const;

	// 塗りの円・輪・輪の一部（角度はラジアン。画面の下向きが正）
	void Disc( const FVector2& Center, float32 Radius, const FColor32& Color, const FPixelStroke& Stroke = FPixelStroke() ) const;
	void Ring( const FVector2& Center, float32 Radius, const FColor32& Color, const FPixelStroke& Stroke = FPixelStroke() ) const;
	void Arc( const FVector2& Center, float32 Radius, float32 Start, float32 End, const FColor32& Color, const FPixelStroke& Stroke = FPixelStroke() ) const;

	// 楕円の輪（Radius は横と縦の半径。太さは Stroke.Width）
	void Ellipse( const FVector2& Center, const FVector2& Radius, const FColor32& Color, const FPixelStroke& Stroke = FPixelStroke() ) const;

	// 塗りの多角形と四角
	void Polygon( TArrayView<const FVector2> Points, const FColor32& Color, const FPixelStroke& Stroke = FPixelStroke() ) const;
	void Rect( const FVector2& TopLeft, const FVector2& Size, const FColor32& Color, const FPixelStroke& Stroke = FPixelStroke() ) const;

	// 中心から外へ弱まる光（色は塗らない）。Strength は中心の強さ
	void Glow( const FVector2& Center, float32 Radius, const FColor32& Color, float32 Strength ) const;

	// ドット文字（Position は、寄せ方に合わせた行の基準点。左寄せなら左上）
	void Text( const FString& Text, const FVector2& Position, const FColor32& Color, const FPixelTextStyle& Style = FPixelTextStyle() ) const;

	// シルエットを 1 色で（TopLeft が左上）
	void Mask( const FPixelMask& Shape, const FVector2& TopLeft, const FColor32& Color, const FPixelStroke& Stroke = FPixelStroke() ) const;

	// 立ち絵（Feet が足元の中心）。Alpha はディザの濃さ
	void Actor( const FPixelActor& Figure, const FVector2& Feet, float32 Alpha = 1.0f ) const;

	// 相対の位置の、いまの色
	FColor32 GetPixel( const FVector2& Position ) const;

private:
	// 相対の座標を、描き先の画素へ
	FVector2 ToSurface( const FVector2& Position ) const;

	// 相対の座標を含む画素の中心を、描き先へ（1 画素を選ぶとき。中心で選ぶので、左右反転でも鏡写しになる）
	FVector2 ToSurfacePixelCenter( const FVector2& Position ) const;

	// 描き先の画素 (X, Y) を、Stroke の描き方で塗る（ディザで間引き、光を足す）
	void Plot( int32 X, int32 Y, const FColor32& Color, const FPixelStroke& Stroke ) const;

	IPixelSurface& Surface;
	FVector2 Origin;
	bool bFlipHorizontal;
};
