#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// FQuaternion
//   回転を表す単位クォータニオン (x, y, z, w)。
//   乗算はハミルトン積: (A * B).Rotate( v ) == A.Rotate( B.Rotate( v ) )
//   つまり A * B は「B を適用した後に A を適用」。
//
//   オイラー角は ラジアン で FVector3( Pitch(X), Yaw(Y), Roll(Z) )。
//   適用順は Roll(Z) -> Pitch(X) -> Yaw(Y)。
//=============================================================================
struct alignas( 16 ) FQuaternion
{
	float32 x = 0.0f;
	float32 y = 0.0f;
	float32 z = 0.0f;
	float32 w = 1.0f;

	constexpr FQuaternion() = default;
	FORCEINLINE constexpr FQuaternion( float32 InX, float32 InY, float32 InZ, float32 InW ) : x( InX ), y( InY ), z( InZ ), w( InW ) {}

	static const FQuaternion Identity;

	// DxLib にクォータニオン型は無いので、行列として渡す: MV1SetRotationMatrix( Handle, FMatrix4x4::Rotate( Q ) )
	FORCEINLINE FString ToString() const { return FString::Printf( "X=%.3f Y=%.3f Z=%.3f W=%.3f", x, y, z, w ); }

	//-------------------------------------------------------------------------
	// 生成
	//-------------------------------------------------------------------------
	// Axis（正規化済み）周りに AngleRad 回転
	FORCEINLINE static FQuaternion FromAxisAngle( const FVector3& Axis, float32 AngleRad )
	{
		const float32 Half = AngleRad * 0.5f;
		const float32 S    = NMath::Sin( Half );
		return { Axis.x * S, Axis.y * S, Axis.z * S, NMath::Cos( Half ) };
	}

	// オイラー角 (Pitch, Yaw, Roll) [rad] -> クォータニオン
	FORCEINLINE static FQuaternion EulerToQuat( const FVector3& EulerRad )
	{
		const FQuaternion Pitch = FromAxisAngle( FVector3::Right,   EulerRad.x );
		const FQuaternion Yaw   = FromAxisAngle( FVector3::Up,      EulerRad.y );
		const FQuaternion Roll  = FromAxisAngle( FVector3::Forward, EulerRad.z );
		return Yaw * Pitch * Roll;
	}
	FORCEINLINE static FQuaternion EulerToQuat( float32 PitchRad, float32 YawRad, float32 RollRad )
	{
		return EulerToQuat( FVector3( PitchRad, YawRad, RollRad ) );
	}

	// 回転後の 3 軸（正規直交）からクォータニオンを作る
	static FQuaternion FromBasis( const FVector3& AxisX, const FVector3& AxisY, const FVector3& AxisZ );

	// Forward 方向を向く回転（Up は上方向のヒント）
	static FQuaternion LookRotation( const FVector3& Forward, const FVector3& Up = FVector3::Up );

	// From 方向を To 方向へ向ける最短回転
	static FQuaternion FromToRotation( const FVector3& From, const FVector3& To );

	//-------------------------------------------------------------------------
	// 演算子
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr FQuaternion operator*( const FQuaternion& Q ) const
	{
		return {
			w * Q.x + x * Q.w + y * Q.z - z * Q.y,
			w * Q.y - x * Q.z + y * Q.w + z * Q.x,
			w * Q.z + x * Q.y - y * Q.x + z * Q.w,
			w * Q.w - x * Q.x - y * Q.y - z * Q.z,
		};
	}
	FQuaternion& operator*=( const FQuaternion& Q ) { *this = *this * Q; return *this; }

	// ベクトルを回転
	FORCEINLINE constexpr FVector3 operator*( const FVector3& V ) const { return Rotate( V ); }

	constexpr bool operator==( const FQuaternion& Q ) const { return x == Q.x && y == Q.y && z == Q.z && w == Q.w; }
	constexpr bool operator!=( const FQuaternion& Q ) const { return !( *this == Q ); }

	//-------------------------------------------------------------------------
	// 関数
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr FVector3 Rotate( const FVector3& V ) const
	{
		// v' = v + 2w(u x v) + 2u x (u x v)
		const FVector3 U( x, y, z );
		const FVector3 T = FVector3::Cross( U, V ) * 2.0f;
		return V + T * w + FVector3::Cross( U, T );
	}
	FORCEINLINE constexpr FVector3 UnrotateVector( const FVector3& V ) const { return Conjugate().Rotate( V ); }

	FORCEINLINE constexpr FVector3 GetForward() const { return Rotate( FVector3::Forward ); }
	FORCEINLINE constexpr FVector3 GetRight() const { return Rotate( FVector3::Right ); }
	FORCEINLINE constexpr FVector3 GetUp() const { return Rotate( FVector3::Up ); }

	FORCEINLINE constexpr float32 LengthSquared() const { return x * x + y * y + z * z + w * w; }
	FORCEINLINE float32 Length() const { return NMath::Sqrt( LengthSquared() ); }

	FORCEINLINE FQuaternion Normalized() const
	{
		const float32 Len = Length();
		return Len > NMath::SmallNumber ? FQuaternion( x / Len, y / Len, z / Len, w / Len ) : Identity;
	}
	FORCEINLINE void Normalize() { *this = Normalized(); }

	FORCEINLINE constexpr FQuaternion Conjugate() const { return { -x, -y, -z, w }; }

	FORCEINLINE FQuaternion Inverse() const
	{
		const float32 LenSq = LengthSquared();
		if ( LenSq <= NMath::SmallNumber ) return Identity;
		const float32 Inv = 1.0f / LenSq;
		return { -x * Inv, -y * Inv, -z * Inv, w * Inv };
	}

	// クォータニオン -> オイラー角 (Pitch, Yaw, Roll) [rad]
	FVector3 ToEuler() const;

	// 回転軸と角度 [rad]
	FORCEINLINE void ToAxisAngle( FVector3& OutAxis, float32& OutAngleRad ) const
	{
		const FQuaternion Q = Normalized();
		OutAngleRad = 2.0f * NMath::Acos( Q.w );
		const float32 S = NMath::Sqrt( NMath::Max( 1.0f - Q.w * Q.w, 0.0f ) );
		OutAxis = S > NMath::Epsilon ? FVector3( Q.x / S, Q.y / S, Q.z / S ) : FVector3::Right;
	}

	FORCEINLINE bool Equals( const FQuaternion& Q, float32 Tolerance = NMath::Epsilon ) const
	{
		// q と -q は同じ回転
		return NMath::Abs( Dot( *this, Q ) ) >= 1.0f - Tolerance;
	}

	FORCEINLINE static constexpr float32 Dot( const FQuaternion& A, const FQuaternion& B ) { return A.x * B.x + A.y * B.y + A.z * B.z + A.w * B.w; }

	// 2 つの回転の角度差 [rad]
	FORCEINLINE static float32 AngleBetween( const FQuaternion& A, const FQuaternion& B )
	{
		return 2.0f * NMath::Acos( NMath::Abs( Dot( A.Normalized(), B.Normalized() ) ) );
	}

	// 正規化線形補間（高速・近似）
	FORCEINLINE static FQuaternion Nlerp( const FQuaternion& A, const FQuaternion& B, float32 Alpha )
	{
		const float32 Sign = Dot( A, B ) < 0.0f ? -1.0f : 1.0f;
		return FQuaternion(
			A.x + ( B.x * Sign - A.x ) * Alpha,
			A.y + ( B.y * Sign - A.y ) * Alpha,
			A.z + ( B.z * Sign - A.z ) * Alpha,
			A.w + ( B.w * Sign - A.w ) * Alpha ).Normalized();
	}

	// 球面線形補間
	static FQuaternion Slerp( const FQuaternion& A, const FQuaternion& B, float32 Alpha );
};

inline constexpr FQuaternion FQuaternion::Identity { 0.0f, 0.0f, 0.0f, 1.0f };
