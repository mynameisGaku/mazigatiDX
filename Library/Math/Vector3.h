#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Math/MathUtils.h"

//=============================================================================
// FVector3
//   座標系は DxLib に合わせた左手系 (X: 右, Y: 上, Z: 奥)。
//   alignas(16) により sizeof は 16 バイト（SIMD ロード用）。
//=============================================================================
struct alignas( 16 ) FVector3
{
	float32 x = 0.0f;
	float32 y = 0.0f;
	float32 z = 0.0f;

	constexpr FVector3() = default;
	FORCEINLINE constexpr FVector3( float32 InX, float32 InY, float32 InZ ) : x( InX ), y( InY ), z( InZ ) {}
	FORCEINLINE constexpr explicit FVector3( float32 Scalar ) : x( Scalar ), y( Scalar ), z( Scalar ) {}

	//-------------------------------------------------------------------------
	// DxLib 変換
	//   FVector3 Pos = VGet( 1, 2, 3 );
	//   DrawSphere3D( Pos, 1.0f, 16, Color, Color, TRUE );   // VECTOR に暗黙変換
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr FVector3( const VECTOR& V ) : x( V.x ), y( V.y ), z( V.z ) {}
	FORCEINLINE constexpr operator VECTOR() const { return VECTOR{ x, y, z }; }

	FORCEINLINE constexpr explicit FVector3( const VECTOR_D& V ) : x( float32( V.x ) ), y( float32( V.y ) ), z( float32( V.z ) ) {}
	FORCEINLINE constexpr VECTOR_D ToDxD() const { return VECTOR_D{ x, y, z }; }

	FORCEINLINE FString ToString() const { return FString::Printf( "X=%.3f Y=%.3f Z=%.3f", x, y, z ); }

	static const FVector3 Zero;
	static const FVector3 One;
	static const FVector3 Up;        // ( 0,  1,  0)
	static const FVector3 Down;      // ( 0, -1,  0)
	static const FVector3 Right;     // ( 1,  0,  0)
	static const FVector3 Left;      // (-1,  0,  0)
	static const FVector3 Forward;   // ( 0,  0,  1)
	static const FVector3 Backward;  // ( 0,  0, -1)

	//-------------------------------------------------------------------------
	// 演算子
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr FVector3 operator+( const FVector3& V ) const { return { x + V.x, y + V.y, z + V.z }; }
	FORCEINLINE constexpr FVector3 operator-( const FVector3& V ) const { return { x - V.x, y - V.y, z - V.z }; }
	FORCEINLINE constexpr FVector3 operator*( const FVector3& V ) const { return { x * V.x, y * V.y, z * V.z }; }
	FORCEINLINE constexpr FVector3 operator/( const FVector3& V ) const { return { x / V.x, y / V.y, z / V.z }; }
	FORCEINLINE constexpr FVector3 operator*( float32 S ) const { return { x * S, y * S, z * S }; }
	FORCEINLINE constexpr FVector3 operator/( float32 S ) const { return { x / S, y / S, z / S }; }
	FORCEINLINE constexpr FVector3 operator-() const { return { -x, -y, -z }; }

	FVector3& operator+=( const FVector3& V ) { x += V.x; y += V.y; z += V.z; return *this; }
	FVector3& operator-=( const FVector3& V ) { x -= V.x; y -= V.y; z -= V.z; return *this; }
	FVector3& operator*=( const FVector3& V ) { x *= V.x; y *= V.y; z *= V.z; return *this; }
	FVector3& operator/=( const FVector3& V ) { x /= V.x; y /= V.y; z /= V.z; return *this; }
	FVector3& operator*=( float32 S ) { x *= S; y *= S; z *= S; return *this; }
	FVector3& operator/=( float32 S ) { x /= S; y /= S; z /= S; return *this; }

	constexpr bool operator==( const FVector3& V ) const { return x == V.x && y == V.y && z == V.z; }
	constexpr bool operator!=( const FVector3& V ) const { return !( *this == V ); }

	float32& operator[]( int32 Index ) { return ( &x )[Index]; }
	const float32& operator[]( int32 Index ) const { return ( &x )[Index]; }

	//-------------------------------------------------------------------------
	// 関数
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr float32 LengthSquared() const { return x * x + y * y + z * z; }
	FORCEINLINE float32 Length() const { return NMath::Sqrt( LengthSquared() ); }

	// 正規化したコピー（長さ 0 なら Zero）
	FORCEINLINE FVector3 Normalized() const
	{
		const float32 Len = Length();
		return Len > NMath::SmallNumber ? *this / Len : FVector3();
	}
	FORCEINLINE void Normalize() { *this = Normalized(); }

	FORCEINLINE bool IsNearlyZero( float32 Tolerance = NMath::Epsilon ) const
	{
		return NMath::Abs( x ) <= Tolerance && NMath::Abs( y ) <= Tolerance && NMath::Abs( z ) <= Tolerance;
	}
	FORCEINLINE bool Equals( const FVector3& V, float32 Tolerance = NMath::Epsilon ) const
	{
		return NMath::Abs( x - V.x ) <= Tolerance && NMath::Abs( y - V.y ) <= Tolerance && NMath::Abs( z - V.z ) <= Tolerance;
	}

	FORCEINLINE static constexpr float32 Dot( const FVector3& A, const FVector3& B ) { return A.x * B.x + A.y * B.y + A.z * B.z; }
	FORCEINLINE static constexpr FVector3 Cross( const FVector3& A, const FVector3& B )
	{
		return { A.y * B.z - A.z * B.y, A.z * B.x - A.x * B.z, A.x * B.y - A.y * B.x };
	}
	FORCEINLINE static float32 Distance( const FVector3& A, const FVector3& B ) { return ( B - A ).Length(); }
	FORCEINLINE static constexpr float32 DistanceSquared( const FVector3& A, const FVector3& B ) { return ( B - A ).LengthSquared(); }
	FORCEINLINE static constexpr FVector3 Lerp( const FVector3& A, const FVector3& B, float32 Alpha ) { return A + ( B - A ) * Alpha; }
	FORCEINLINE static constexpr FVector3 Min( const FVector3& A, const FVector3& B ) { return { NMath::Min( A.x, B.x ), NMath::Min( A.y, B.y ), NMath::Min( A.z, B.z ) }; }
	FORCEINLINE static constexpr FVector3 Max( const FVector3& A, const FVector3& B ) { return { NMath::Max( A.x, B.x ), NMath::Max( A.y, B.y ), NMath::Max( A.z, B.z ) }; }

	// 法線 N（正規化済み）で反射
	FORCEINLINE static constexpr FVector3 Reflect( const FVector3& V, const FVector3& N ) { return V - N * ( 2.0f * Dot( V, N ) ); }
	// V を Onto 方向に射影
	FORCEINLINE static constexpr FVector3 Project( const FVector3& V, const FVector3& Onto )
	{
		const float32 LenSq = Onto.LengthSquared();
		return LenSq > NMath::SmallNumber ? Onto * ( Dot( V, Onto ) / LenSq ) : FVector3();
	}
	// 法線 N（正規化済み）の平面に射影
	FORCEINLINE static constexpr FVector3 ProjectOnPlane( const FVector3& V, const FVector3& N ) { return V - N * Dot( V, N ); }
	// 2 ベクトル間の角度（ラジアン）
	FORCEINLINE static float32 Angle( const FVector3& A, const FVector3& B )
	{
		const float32 Denom = NMath::Sqrt( A.LengthSquared() * B.LengthSquared() );
		return Denom > NMath::SmallNumber ? NMath::Acos( Dot( A, B ) / Denom ) : 0.0f;
	}
};

inline constexpr FVector3 FVector3::Zero     {  0.0f,  0.0f,  0.0f };
inline constexpr FVector3 FVector3::One      {  1.0f,  1.0f,  1.0f };
inline constexpr FVector3 FVector3::Up       {  0.0f,  1.0f,  0.0f };
inline constexpr FVector3 FVector3::Down     {  0.0f, -1.0f,  0.0f };
inline constexpr FVector3 FVector3::Right    {  1.0f,  0.0f,  0.0f };
inline constexpr FVector3 FVector3::Left     { -1.0f,  0.0f,  0.0f };
inline constexpr FVector3 FVector3::Forward  {  0.0f,  0.0f,  1.0f };
inline constexpr FVector3 FVector3::Backward {  0.0f,  0.0f, -1.0f };

FORCEINLINE constexpr FVector3 operator*( float32 S, const FVector3& V ) { return V * S; }
