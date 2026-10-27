#include "EditorGeometry.h"


#include "Library/Core/Collection/Sort.h"
#include "Library/Core/Types/Numeric.h"

namespace
{
	constexpr float32 Tiny = 1.0e-8f;

	// 球との交点（手前側）。光線の始点が球の外で、前方にあるときだけ true（始点が中なら、手前の交点は後ろ（T < 0）になるので、当たらない）
	bool HitSphere( const FEditorRay& Ray, const FVector3& Center, float32 Radius, float32& OutT )
	{
		const FVector3 ToOrigin = Ray.Origin - Center;
		const float32 A = FVector3::Dot( Ray.Direction, Ray.Direction );
		const float32 B = FVector3::Dot( ToOrigin, Ray.Direction );
		const float32 C = FVector3::Dot( ToOrigin, ToOrigin ) - Radius * Radius;
		if ( A < Tiny ) return false;
		const float32 Discriminant = B * B - A * C;
		if ( Discriminant < 0.0f ) return false;
		const float32 T = ( -B - NMath::Sqrt( Discriminant ) ) / A;
		if ( T < 0.0f ) return false;
		OutT = T;
		return true;
	}
}

bool NWorldEditor::RayVsBox( const FEditorRay& Ray, const FVector3& Min, const FVector3& Max, float32& OutT, FVector3* OutNormal )
{
	const float32 Origin[3] = { Ray.Origin.x, Ray.Origin.y, Ray.Origin.z };
	const float32 Direction[3] = { Ray.Direction.x, Ray.Direction.y, Ray.Direction.z };
	const float32 Low[3] = { Min.x, Min.y, Min.z };
	const float32 High[3] = { Max.x, Max.y, Max.z };

	// 始点が箱の中なら、入る位置は後ろ（Near < 0）になるので、当たらない
	float32 Near = -NMath::Infinity;
	float32 Far = NMath::Infinity;
	int32 NearAxis = -1;
	float32 NearSign = 0.0f;
	for ( int32 Axis = 0; Axis < 3; ++Axis )
	{
		if ( NMath::Abs( Direction[Axis] ) < Tiny )
		{
			// この軸には動かない。範囲の外なら、決して当たらない
			if ( Origin[Axis] < Low[Axis] || Origin[Axis] > High[Axis] ) return false;
			continue;
		}
		const float32 Inverse = 1.0f / Direction[Axis];
		float32 T1 = ( Low[Axis] - Origin[Axis] ) * Inverse;
		float32 T2 = ( High[Axis] - Origin[Axis] ) * Inverse;
		// 進む向きが正なら、低い側の面から入る（法線は負の向き）
		const float32 EnterSign = Direction[Axis] > 0.0f ? -1.0f : 1.0f;
		if ( T1 > T2 ) NAlgo::Swap( T1, T2 );
		if ( T1 > Near )
		{
			Near = T1;
			NearAxis = Axis;
			NearSign = EnterSign;
		}
		Far = NMath::Min( Far, T2 );
		if ( Near > Far ) return false;
	}
	if ( NearAxis < 0 || Near < 0.0f ) return false;

	OutT = Near;
	if ( OutNormal != nullptr )
	{
		FVector3 Normal = FVector3::Zero;
		( NearAxis == 0 ? Normal.x : NearAxis == 1 ? Normal.y : Normal.z ) = NearSign;
		*OutNormal = Normal;
	}
	return true;
}

bool NWorldEditor::RayVsOrientedBox( const FEditorRay& Ray, const FVector3& Center, const FQuaternion& Rotation, const FVector3& HalfExtents, float32& OutT, FVector3* OutNormal )
{
	// 箱の局所座標へ光線を移す（回転だけなので、長さ T は同じ）
	FEditorRay Local;
	Local.Origin = Rotation.UnrotateVector( Ray.Origin - Center );
	Local.Direction = Rotation.UnrotateVector( Ray.Direction );
	FVector3 LocalNormal;
	if ( !RayVsBox( Local, -HalfExtents, HalfExtents, OutT, OutNormal != nullptr ? &LocalNormal : nullptr ) ) return false;
	if ( OutNormal != nullptr ) *OutNormal = Rotation.Rotate( LocalNormal );
	return true;
}

bool NWorldEditor::RayVsSphere( const FEditorRay& Ray, const FVector3& Center, float32 Radius, float32& OutT )
{
	return HitSphere( Ray, Center, Radius, OutT );
}

bool NWorldEditor::RayVsCapsule( const FEditorRay& Ray, const FVector3& A, const FVector3& B, float32 Radius, float32& OutT )
{
	const FVector3 Axis = B - A;
	const float32 AxisLengthSquared = FVector3::Dot( Axis, Axis );
	if ( AxisLengthSquared < Tiny ) return HitSphere( Ray, A, Radius, OutT );

	const FVector3 ToOrigin = Ray.Origin - A;
	const float32 OriginAlong = FVector3::Dot( Axis, ToOrigin );

	// 始点が中なら、円柱の入る位置は後ろ（T < 0）で、両端の半球は外側の半分だけを見るので、当たらない
	float32 Best = NMath::Infinity;

	// 円柱の側面
	const float32 DirectionAlong = FVector3::Dot( Axis, Ray.Direction );
	const float32 Quadratic = AxisLengthSquared - DirectionAlong * DirectionAlong;
	if ( Quadratic > Tiny )
	{
		const float32 DirectionToOrigin = FVector3::Dot( Ray.Direction, ToOrigin );
		const float32 Linear = AxisLengthSquared * DirectionToOrigin - OriginAlong * DirectionAlong;
		const float32 Constant = AxisLengthSquared * FVector3::Dot( ToOrigin, ToOrigin ) - OriginAlong * OriginAlong - Radius * Radius * AxisLengthSquared;
		const float32 Discriminant = Linear * Linear - Quadratic * Constant;
		if ( Discriminant >= 0.0f )
		{
			const float32 T = ( -Linear - NMath::Sqrt( Discriminant ) ) / Quadratic;
			const float32 Along = OriginAlong + T * DirectionAlong;
			if ( T >= 0.0f && Along > 0.0f && Along < AxisLengthSquared ) Best = T;
		}
	}

	// 両端の半球（その球の、外側の半分に当たったときだけ）
	for ( int32 End = 0; End < 2; ++End )
	{
		float32 T = 0.0f;
		const FVector3 Center = End == 0 ? A : B;
		if ( !HitSphere( Ray, Center, Radius, T ) ) continue;
		const float32 Along = FVector3::Dot( Axis, Ray.PointAt( T ) - A );
		if ( End == 0 ? Along <= 0.0f : Along >= AxisLengthSquared ) Best = NMath::Min( Best, T );
	}

	if ( Best == NMath::Infinity ) return false;
	OutT = Best;
	return true;
}

bool NWorldEditor::RayVsPlane( const FEditorRay& Ray, const FVector3& PlanePoint, const FVector3& PlaneNormal, float32& OutT )
{
	const float32 Denominator = FVector3::Dot( PlaneNormal, Ray.Direction );
	if ( NMath::Abs( Denominator ) < Tiny ) return false;
	const float32 T = FVector3::Dot( PlaneNormal, PlanePoint - Ray.Origin ) / Denominator;
	if ( T < 0.0f ) return false;
	OutT = T;
	return true;
}

bool NWorldEditor::RayVsQuad( const FEditorRay& Ray, const FVector3& Center, const FVector3& HalfRight, const FVector3& HalfUp, float32& OutT )
{
	const float32 RightLengthSquared = HalfRight.LengthSquared();
	const float32 UpLengthSquared = HalfUp.LengthSquared();
	if ( RightLengthSquared < Tiny || UpLengthSquared < Tiny ) return false;

	const FVector3 Normal = FVector3::Cross( HalfRight, HalfUp );
	if ( Normal.LengthSquared() < Tiny ) return false;

	float32 T = 0.0f;
	if ( !RayVsPlane( Ray, Center, Normal.Normalized(), T ) ) return false;
	const FVector3 Offset = Ray.PointAt( T ) - Center;
	const float32 U = FVector3::Dot( Offset, HalfRight ) / RightLengthSquared;
	const float32 V = FVector3::Dot( Offset, HalfUp ) / UpLengthSquared;
	if ( NMath::Abs( U ) > 1.0f || NMath::Abs( V ) > 1.0f ) return false;
	OutT = T;
	return true;
}

bool NWorldEditor::ClosestPointsOnLines( const FVector3& P1, const FVector3& D1, const FVector3& P2, const FVector3& D2, float32& OutT1, float32& OutT2 )
{
	// 桁落ちを避けるため、倍精度で計算する
	const auto Dot = []( const FVector3& X, const FVector3& Y )
	{
		return static_cast<double>( X.x ) * Y.x + static_cast<double>( X.y ) * Y.y + static_cast<double>( X.z ) * Y.z;
	};
	const FVector3 Offset = P1 - P2;
	const double A = Dot( D1, D1 );
	const double E = Dot( D2, D2 );
	const double B = Dot( D1, D2 );
	const double C = Dot( D1, Offset );
	const double F = Dot( D2, Offset );
	if ( A < Tiny || E < Tiny ) return false;
	const double Denominator = A * E - B * B;
	if ( Denominator < 1.0e-9 * A * E ) return false;   // ほぼ平行
	const double T1 = ( B * F - C * E ) / Denominator;
	const double T2 = ( B * T1 + F ) / E;
	OutT1 = static_cast<float32>( T1 );
	OutT2 = static_cast<float32>( T2 );
	return true;
}

float32 NWorldEditor::DistanceRayToSegment( const FEditorRay& Ray, const FVector3& A, const FVector3& B, float32& OutRayT, float32& OutSegmentS )
{
	// 光線を、十分に長い線分として扱って、線分どうしのいちばん近い点を求める（倍精度）
	constexpr double RayLength = 10000.0;
	const auto Dot = []( const FVector3& X, const FVector3& Y )
	{
		return static_cast<double>( X.x ) * Y.x + static_cast<double>( X.y ) * Y.y + static_cast<double>( X.z ) * Y.z;
	};
	const FVector3 D1 = Ray.Direction;   // 長さ RayLength の線分の向き（長さ 1 に直す）
	const FVector3 D2 = B - A;
	const FVector3 Offset = Ray.Origin - A;
	const double LengthA = RayLength * RayLength * Dot( D1, D1 );
	const double E = Dot( D2, D2 );
	const double F = Dot( D2, Offset );

	// s: 光線上の位置（0..1 が 0..RayLength）、t: 線分上の位置（0..1）
	double S = 0.0;
	double T = 0.0;
	const double C = RayLength * Dot( D1, Offset );
	const double Cross = RayLength * Dot( D1, D2 );
	if ( LengthA < Tiny && E < Tiny )
	{
	}
	else if ( LengthA < Tiny )
	{
		T = NMath::Clamp( F / E, 0.0, 1.0 );
	}
	else if ( E < Tiny )
	{
		S = NMath::Clamp( -C / LengthA, 0.0, 1.0 );
	}
	else
	{
		const double Denominator = LengthA * E - Cross * Cross;
		S = Denominator > 0.0 ? NMath::Clamp( ( Cross * F - C * E ) / Denominator, 0.0, 1.0 ) : 0.0;
		T = ( Cross * S + F ) / E;
		if ( T < 0.0 )
		{
			T = 0.0;
			S = NMath::Clamp( -C / LengthA, 0.0, 1.0 );
		}
		else if ( T > 1.0 )
		{
			T = 1.0;
			S = NMath::Clamp( ( Cross - C ) / LengthA, 0.0, 1.0 );
		}
	}

	OutRayT = static_cast<float32>( S * RayLength );
	OutSegmentS = static_cast<float32>( T );
	const FVector3 OnRay = Ray.Origin + Ray.Direction * OutRayT;
	const FVector3 OnSegment = A + D2 * OutSegmentS;
	return FVector3::Distance( OnRay, OnSegment );
}
