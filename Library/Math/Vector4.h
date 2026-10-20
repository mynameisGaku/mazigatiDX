#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// FVector4
//=============================================================================
struct alignas( 16 ) FVector4
{
	float32 x = 0.0f;
	float32 y = 0.0f;
	float32 z = 0.0f;
	float32 w = 0.0f;

	constexpr FVector4() = default;
	FORCEINLINE constexpr FVector4( float32 InX, float32 InY, float32 InZ, float32 InW ) : x( InX ), y( InY ), z( InZ ), w( InW ) {}
	FORCEINLINE constexpr FVector4( const FVector3& V, float32 InW ) : x( V.x ), y( V.y ), z( V.z ), w( InW ) {}
	FORCEINLINE constexpr explicit FVector4( float32 Scalar ) : x( Scalar ), y( Scalar ), z( Scalar ), w( Scalar ) {}

	static const FVector4 Zero;
	static const FVector4 One;

	FORCEINLINE constexpr FVector3 XYZ() const { return { x, y, z }; }

	//-------------------------------------------------------------------------
	// DxLib 変換
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr FVector4( const FLOAT4& V ) : x( V.x ), y( V.y ), z( V.z ), w( V.w ) {}
	FORCEINLINE constexpr operator FLOAT4() const { return FLOAT4{ x, y, z, w }; }

	FORCEINLINE FString ToString() const { return FString::Printf( "X=%.3f Y=%.3f Z=%.3f W=%.3f", x, y, z, w ); }

	//-------------------------------------------------------------------------
	// 演算子
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr FVector4 operator+( const FVector4& V ) const { return { x + V.x, y + V.y, z + V.z, w + V.w }; }
	FORCEINLINE constexpr FVector4 operator-( const FVector4& V ) const { return { x - V.x, y - V.y, z - V.z, w - V.w }; }
	FORCEINLINE constexpr FVector4 operator*( const FVector4& V ) const { return { x * V.x, y * V.y, z * V.z, w * V.w }; }
	FORCEINLINE constexpr FVector4 operator/( const FVector4& V ) const { return { x / V.x, y / V.y, z / V.z, w / V.w }; }
	FORCEINLINE constexpr FVector4 operator*( float32 S ) const { return { x * S, y * S, z * S, w * S }; }
	FORCEINLINE constexpr FVector4 operator/( float32 S ) const { return { x / S, y / S, z / S, w / S }; }
	FORCEINLINE constexpr FVector4 operator-() const { return { -x, -y, -z, -w }; }

	FVector4& operator+=( const FVector4& V ) { x += V.x; y += V.y; z += V.z; w += V.w; return *this; }
	FVector4& operator-=( const FVector4& V ) { x -= V.x; y -= V.y; z -= V.z; w -= V.w; return *this; }
	FVector4& operator*=( const FVector4& V ) { x *= V.x; y *= V.y; z *= V.z; w *= V.w; return *this; }
	FVector4& operator/=( const FVector4& V ) { x /= V.x; y /= V.y; z /= V.z; w /= V.w; return *this; }
	FVector4& operator*=( float32 S ) { x *= S; y *= S; z *= S; w *= S; return *this; }
	FVector4& operator/=( float32 S ) { x /= S; y /= S; z /= S; w /= S; return *this; }

	constexpr bool operator==( const FVector4& V ) const { return x == V.x && y == V.y && z == V.z && w == V.w; }
	constexpr bool operator!=( const FVector4& V ) const { return !( *this == V ); }

	float32& operator[]( int32 Index ) { return ( &x )[Index]; }
	const float32& operator[]( int32 Index ) const { return ( &x )[Index]; }

	//-------------------------------------------------------------------------
	// 関数
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr float32 LengthSquared() const { return x * x + y * y + z * z + w * w; }
	FORCEINLINE float32 Length() const { return NMath::Sqrt( LengthSquared() ); }

	FORCEINLINE FVector4 Normalized() const
	{
		const float32 Len = Length();
		return Len > NMath::SmallNumber ? *this / Len : FVector4();
	}
	FORCEINLINE void Normalize() { *this = Normalized(); }

	FORCEINLINE bool Equals( const FVector4& V, float32 Tolerance = NMath::Epsilon ) const
	{
		return NMath::Abs( x - V.x ) <= Tolerance && NMath::Abs( y - V.y ) <= Tolerance
			&& NMath::Abs( z - V.z ) <= Tolerance && NMath::Abs( w - V.w ) <= Tolerance;
	}

	FORCEINLINE static constexpr float32 Dot( const FVector4& A, const FVector4& B ) { return A.x * B.x + A.y * B.y + A.z * B.z + A.w * B.w; }
	FORCEINLINE static constexpr FVector4 Lerp( const FVector4& A, const FVector4& B, float32 Alpha ) { return A + ( B - A ) * Alpha; }
	FORCEINLINE static constexpr FVector4 Min( const FVector4& A, const FVector4& B ) { return { NMath::Min( A.x, B.x ), NMath::Min( A.y, B.y ), NMath::Min( A.z, B.z ), NMath::Min( A.w, B.w ) }; }
	FORCEINLINE static constexpr FVector4 Max( const FVector4& A, const FVector4& B ) { return { NMath::Max( A.x, B.x ), NMath::Max( A.y, B.y ), NMath::Max( A.z, B.z ), NMath::Max( A.w, B.w ) }; }
};

inline constexpr FVector4 FVector4::Zero { 0.0f, 0.0f, 0.0f, 0.0f };
inline constexpr FVector4 FVector4::One  { 1.0f, 1.0f, 1.0f, 1.0f };

FORCEINLINE constexpr FVector4 operator*( float32 S, const FVector4& V ) { return V * S; }
