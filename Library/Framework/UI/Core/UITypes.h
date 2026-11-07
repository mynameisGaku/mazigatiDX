// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Vector2.h"

//=============================================================================
// UI の基本の型（描画・DxLib・ゲームのオブジェクトを知らない値だけ）
//   座標はすべて画面の画素（左上が原点、右が +X、下が +Y）。
//=============================================================================
namespace NUI
{
	// 大きさの上限の既定値（実質、制限なし）
	constexpr float32 Unbounded = 1.0e9f;
}

// 四辺の余白（外側の余白 Margin・内側の余白 Padding・Canvas の位置の指定 Offsets に使う）
struct FUIMargin
{
	float32 Left = 0.0f;
	float32 Top = 0.0f;
	float32 Right = 0.0f;
	float32 Bottom = 0.0f;

	constexpr FUIMargin() = default;
	constexpr explicit FUIMargin( float32 All ) : Left( All ), Top( All ), Right( All ), Bottom( All ) {}
	constexpr FUIMargin( float32 Horizontal, float32 Vertical ) : Left( Horizontal ), Top( Vertical ), Right( Horizontal ), Bottom( Vertical ) {}
	constexpr FUIMargin( float32 InLeft, float32 InTop, float32 InRight, float32 InBottom ) : Left( InLeft ), Top( InTop ), Right( InRight ), Bottom( InBottom ) {}

	constexpr float32 GetHorizontal() const { return Left + Right; }
	constexpr float32 GetVertical() const { return Top + Bottom; }
	constexpr FVector2 GetSize() const { return FVector2( Left + Right, Top + Bottom ); }

	constexpr bool operator==( const FUIMargin& Other ) const { return Left == Other.Left && Top == Other.Top && Right == Other.Right && Bottom == Other.Bottom; }
	constexpr bool operator!=( const FUIMargin& Other ) const { return !( *this == Other ); }
};

// 矩形（左上の位置と大きさ）
struct FUIRect
{
	FVector2 Position;
	FVector2 Size;

	constexpr FUIRect() = default;
	constexpr FUIRect( float32 X, float32 Y, float32 Width, float32 Height ) : Position( X, Y ), Size( Width, Height ) {}
	constexpr FUIRect( const FVector2& InPosition, const FVector2& InSize ) : Position( InPosition ), Size( InSize ) {}

	constexpr float32 Left() const { return Position.x; }
	constexpr float32 Top() const { return Position.y; }
	constexpr float32 Right() const { return Position.x + Size.x; }
	constexpr float32 Bottom() const { return Position.y + Size.y; }
	constexpr FVector2 GetCenter() const { return FVector2( Position.x + Size.x * 0.5f, Position.y + Size.y * 0.5f ); }

	// 点が中にあるか（左・上の辺は含み、右・下の辺は含まない。隣り合う矩形で、同じ点が 2 つに入らない）
	constexpr bool Contains( const FVector2& Point ) const
	{
		return Point.x >= Position.x && Point.x < Position.x + Size.x && Point.y >= Position.y && Point.y < Position.y + Size.y;
	}

	// 四辺を内側へ縮めた矩形（大きさは 0 未満にならない）
	FUIRect Deflate( const FUIMargin& Margin ) const
	{
		return FUIRect( Position.x + Margin.Left, Position.y + Margin.Top, NMath::Max( 0.0f, Size.x - Margin.GetHorizontal() ), NMath::Max( 0.0f, Size.y - Margin.GetVertical() ) );
	}

	// 2 つの矩形の重なり（重ならなければ、大きさ 0 の矩形）
	FUIRect Intersect( const FUIRect& Other ) const
	{
		const float32 NewLeft = NMath::Max( Left(), Other.Left() );
		const float32 NewTop = NMath::Max( Top(), Other.Top() );
		const float32 NewRight = NMath::Min( Right(), Other.Right() );
		const float32 NewBottom = NMath::Min( Bottom(), Other.Bottom() );
		return FUIRect( NewLeft, NewTop, NMath::Max( 0.0f, NewRight - NewLeft ), NMath::Max( 0.0f, NewBottom - NewTop ) );
	}

	constexpr bool operator==( const FUIRect& Other ) const { return Position.x == Other.Position.x && Position.y == Other.Position.y && Size.x == Other.Size.x && Size.y == Other.Size.y; }
	constexpr bool operator!=( const FUIRect& Other ) const { return !( *this == Other ); }
};

// 置き場所の中での寄せ方（Stack・Grid・ふつうの子に使う）
enum class EUIAlignment : uint8
{
	Start,   // 左 / 上へ寄せる
	Center,  // 中央
	End,     // 右 / 下へ寄せる
	Stretch  // 置き場所いっぱいに広げる
};

// 見え方（レイアウトと当たり判定に影響する）
enum class EUIVisibility : uint8
{
	Visible,   // 見える・場所を取る・当たる
	Hidden,    // 見えない・場所は取る・当たらない
	Collapsed  // 見えない・場所も取らない・当たらない
};

// 並べる向き
enum class EUIOrientation : uint8
{
	Horizontal,
	Vertical
};

// Grid の列・行の大きさの決め方
enum class EUITrackKind : uint8
{
	Auto,   // 中の部品の大きさ（1 つの列・行だけにまたがる部品の最大）
	Fixed,  // 決まった画素
	Weight  // 残りを、重みの比で分ける
};

// Grid の列または行の指定
struct FUITrack
{
	EUITrackKind Kind = EUITrackKind::Weight;
	float32 Value = 1.0f; // Fixed: 画素 / Weight: 重み

	static FUITrack MakeAuto() { return FUITrack { EUITrackKind::Auto, 0.0f }; }
	static FUITrack MakeFixed( float32 Pixels ) { return FUITrack { EUITrackKind::Fixed, Pixels }; }
	static FUITrack MakeWeight( float32 Weight = 1.0f ) { return FUITrack { EUITrackKind::Weight, Weight }; }
};
