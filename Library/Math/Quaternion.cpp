#include "Quaternion.h"


FQuaternion FQuaternion::FromBasis( const FVector3& AxisX, const FVector3& AxisY, const FVector3& AxisZ )
{
	// 列ベクトル形式の回転行列 m[row][col] として扱う
	const float32 m00 = AxisX.x, m01 = AxisY.x, m02 = AxisZ.x;
	const float32 m10 = AxisX.y, m11 = AxisY.y, m12 = AxisZ.y;
	const float32 m20 = AxisX.z, m21 = AxisY.z, m22 = AxisZ.z;

	FQuaternion Q;
	const float32 Trace = m00 + m11 + m22;
	if ( Trace > 0.0f )
	{
		const float32 S = 0.5f / NMath::Sqrt( Trace + 1.0f );
		Q.w = 0.25f / S;
		Q.x = ( m21 - m12 ) * S;
		Q.y = ( m02 - m20 ) * S;
		Q.z = ( m10 - m01 ) * S;
	}
	else if ( m00 > m11 && m00 > m22 )
	{
		const float32 S = 2.0f * NMath::Sqrt( 1.0f + m00 - m11 - m22 );
		Q.w = ( m21 - m12 ) / S;
		Q.x = 0.25f * S;
		Q.y = ( m01 + m10 ) / S;
		Q.z = ( m02 + m20 ) / S;
	}
	else if ( m11 > m22 )
	{
		const float32 S = 2.0f * NMath::Sqrt( 1.0f + m11 - m00 - m22 );
		Q.w = ( m02 - m20 ) / S;
		Q.x = ( m01 + m10 ) / S;
		Q.y = 0.25f * S;
		Q.z = ( m12 + m21 ) / S;
	}
	else
	{
		const float32 S = 2.0f * NMath::Sqrt( 1.0f + m22 - m00 - m11 );
		Q.w = ( m10 - m01 ) / S;
		Q.x = ( m02 + m20 ) / S;
		Q.y = ( m12 + m21 ) / S;
		Q.z = 0.25f * S;
	}
	return Q.Normalized();
}

FQuaternion FQuaternion::LookRotation( const FVector3& Forward, const FVector3& Up )
{
	const FVector3 F = Forward.Normalized();
	if ( F.IsNearlyZero() ) return Identity;

	FVector3 R = FVector3::Cross( Up, F ).Normalized();
	if ( R.IsNearlyZero() )
	{
		// Forward と Up が平行
		R = FVector3::Cross( FVector3::Forward, F ).Normalized();
		if ( R.IsNearlyZero() ) R = FVector3::Right;
	}
	const FVector3 U = FVector3::Cross( F, R );
	return FromBasis( R, U, F );
}

FQuaternion FQuaternion::FromToRotation( const FVector3& From, const FVector3& To )
{
	const FVector3 A = From.Normalized();
	const FVector3 B = To.Normalized();
	const float32  D = FVector3::Dot( A, B );

	if ( D < -1.0f + NMath::Epsilon )
	{
		// 真逆: 任意の直交軸で 180 度
		FVector3 Axis = FVector3::Cross( FVector3::Right, A );
		if ( Axis.LengthSquared() < NMath::Epsilon ) Axis = FVector3::Cross( FVector3::Up, A );
		return FromAxisAngle( Axis.Normalized(), NMath::PI );
	}

	const FVector3 C = FVector3::Cross( A, B );
	return FQuaternion( C.x, C.y, C.z, 1.0f + D ).Normalized();
}

FVector3 FQuaternion::ToEuler() const
{
	// 列ベクトル形式の回転行列 R = Ry * Rx * Rz の要素から求める
	const float32 R12 = 2.0f * ( y * z - w * x );
	if ( NMath::Abs( R12 ) < 0.99999f )
	{
		const float32 R02 = 2.0f * ( x * z + w * y );
		const float32 R22 = 1.0f - 2.0f * ( x * x + y * y );
		const float32 R10 = 2.0f * ( x * y + w * z );
		const float32 R11 = 1.0f - 2.0f * ( x * x + z * z );
		return { NMath::Asin( -R12 ), NMath::Atan2( R02, R22 ), NMath::Atan2( R10, R11 ) };
	}

	// ジンバルロック: Roll = 0 として Yaw に寄せる
	const float32 R20 = 2.0f * ( x * z - w * y );
	const float32 R00 = 1.0f - 2.0f * ( y * y + z * z );
	return { R12 < 0.0f ? NMath::HALF_PI : -NMath::HALF_PI, NMath::Atan2( -R20, R00 ), 0.0f };
}

FQuaternion FQuaternion::Slerp( const FQuaternion& A, const FQuaternion& B, float32 Alpha )
{
	float32 CosTheta = Dot( A, B );
	float32 Sign     = 1.0f;
	if ( CosTheta < 0.0f )
	{
		CosTheta = -CosTheta;
		Sign     = -1.0f;
	}

	if ( CosTheta > 0.9995f ) return Nlerp( A, B, Alpha );

	const float32 Theta    = NMath::Acos( CosTheta );
	const float32 InvSin   = 1.0f / NMath::Sin( Theta );
	const float32 ScaleA   = NMath::Sin( ( 1.0f - Alpha ) * Theta ) * InvSin;
	const float32 ScaleB   = NMath::Sin( Alpha * Theta ) * InvSin * Sign;
	return FQuaternion(
		A.x * ScaleA + B.x * ScaleB,
		A.y * ScaleA + B.y * ScaleB,
		A.z * ScaleA + B.z * ScaleB,
		A.w * ScaleA + B.w * ScaleB );
}
