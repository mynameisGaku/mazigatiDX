#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Math/MathUtils.h"

//=============================================================================
// FVector2
//=============================================================================
struct FVector2
{
	float32 x = 0.0f;
	float32 y = 0.0f;

	constexpr FVector2() = default;
	FORCEINLINE constexpr FVector2( float32 InX, float32 InY ) : x( InX ), y( InY ) {}
	FORCEINLINE constexpr explicit FVector2( float32 Scalar ) : x( Scalar ), y( Scalar ) {}

	//-------------------------------------------------------------------------
	// DxLib 変換 (FLOAT2 は u, v)
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr FVector2( const FLOAT2& V ) : x( V.u ), y( V.v ) {}
	FORCEINLINE constexpr operator FLOAT2() const { return FLOAT2{ x, y }; }

	FORCEINLINE FString ToString() const { return FString::Printf( "X=%.3f Y=%.3f", x, y ); }

	static const FVector2 Zero;
	static const FVector2 One;
	static const FVector2 UnitX;
	static const FVector2 UnitY;

	//-------------------------------------------------------------------------
	// 演算子
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr FVector2 operator+( const FVector2& V ) const { return { x + V.x, y + V.y }; }
	FORCEINLINE constexpr FVector2 operator-( const FVector2& V ) const { return { x - V.x, y - V.y }; }
	FORCEINLINE constexpr FVector2 operator*( const FVector2& V ) const { return { x * V.x, y * V.y }; }
	FORCEINLINE constexpr FVector2 operator/( const FVector2& V ) const { return { x / V.x, y / V.y }; }
	FORCEINLINE constexpr FVector2 operator*( float32 S ) const { return { x * S, y * S }; }
	FORCEINLINE constexpr FVector2 operator/( float32 S ) const { return { x / S, y / S }; }
	FORCEINLINE constexpr FVector2 operator-() const { return { -x, -y }; }

	FVector2& operator+=( const FVector2& V ) { x += V.x; y += V.y; return *this; }
	FVector2& operator-=( const FVector2& V ) { x -= V.x; y -= V.y; return *this; }
	FVector2& operator*=( const FVector2& V ) { x *= V.x; y *= V.y; return *this; }
	FVector2& operator/=( const FVector2& V ) { x /= V.x; y /= V.y; return *this; }
	FVector2& operator*=( float32 S ) { x *= S; y *= S; return *this; }
	FVector2& operator/=( float32 S ) { x /= S; y /= S; return *this; }

	constexpr bool operator==( const FVector2& V ) const { return x == V.x && y == V.y; }
	constexpr bool operator!=( const FVector2& V ) const { return !( *this == V ); }

	float32& operator[]( int32 Index ) { return ( &x )[Index]; }
	const float32& operator[]( int32 Index ) const { return ( &x )[Index]; }

	//-------------------------------------------------------------------------
	// 関数
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr float32 LengthSquared() const { return x * x + y * y; }
	FORCEINLINE float32 Length() const { return NMath::Sqrt( LengthSquared() ); }

	// 正規化したコピー（長さ 0 なら Zero）
	FORCEINLINE FVector2 Normalized() const
	{
		const float32 Len = Length();
		return Len > NMath::SmallNumber ? *this / Len : FVector2();
	}
	FORCEINLINE void Normalize() { *this = Normalized(); }

	FORCEINLINE bool IsNearlyZero( float32 Tolerance = NMath::Epsilon ) const
	{
		return NMath::Abs( x ) <= Tolerance && NMath::Abs( y ) <= Tolerance;
	}
	FORCEINLINE bool Equals( const FVector2& V, float32 Tolerance = NMath::Epsilon ) const
	{
		return NMath::Abs( x - V.x ) <= Tolerance && NMath::Abs( y - V.y ) <= Tolerance;
	}

	FORCEINLINE static constexpr float32 Dot( const FVector2& A, const FVector2& B ) { return A.x * B.x + A.y * B.y; }
	// 2D 外積（Z 成分）
	FORCEINLINE static constexpr float32 Cross( const FVector2& A, const FVector2& B ) { return A.x * B.y - A.y * B.x; }
	FORCEINLINE static float32 Distance( const FVector2& A, const FVector2& B ) { return ( B - A ).Length(); }
	FORCEINLINE static constexpr float32 DistanceSquared( const FVector2& A, const FVector2& B ) { return ( B - A ).LengthSquared(); }
	FORCEINLINE static constexpr FVector2 Lerp( const FVector2& A, const FVector2& B, float32 Alpha ) { return A + ( B - A ) * Alpha; }
	FORCEINLINE static constexpr FVector2 Min( const FVector2& A, const FVector2& B ) { return { NMath::Min( A.x, B.x ), NMath::Min( A.y, B.y ) }; }
	FORCEINLINE static constexpr FVector2 Max( const FVector2& A, const FVector2& B ) { return { NMath::Max( A.x, B.x ), NMath::Max( A.y, B.y ) }; }
	// 法線 N（正規化済み）で反射
	FORCEINLINE static constexpr FVector2 Reflect( const FVector2& V, const FVector2& N ) { return V - N * ( 2.0f * Dot( V, N ) ); }
	// 反時計回りに 90 度回転
	FORCEINLINE constexpr FVector2 Perpendicular() const { return { -y, x }; }
};

inline constexpr FVector2 FVector2::Zero  { 0.0f, 0.0f };
inline constexpr FVector2 FVector2::One   { 1.0f, 1.0f };
inline constexpr FVector2 FVector2::UnitX { 1.0f, 0.0f };
inline constexpr FVector2 FVector2::UnitY { 0.0f, 1.0f };

FORCEINLINE constexpr FVector2 operator*( float32 S, const FVector2& V ) { return V * S; }
