#include "EditorGizmo.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float32 HitPixels = 7.0f;            // 取っ手に当たったとみなす、画面での太さ（半径のピクセル）
	constexpr float32 ParallelLimit = 0.05f;       // 光線と面の法線の内積がこれ未満なら、面とほぼ平行（交点が不安定）
	constexpr float32 AxisParallelLimit = 0.995f;  // 光線と軸の内積の絶対値がこれ以上なら、軸の真正面から見ている（位置が不安定）
	constexpr float32 MinFactor = 0.01f;           // 拡大の倍率の下限（0 や負にしない）

	float32 Component( const FVector3& V, int32 Axis ) { return Axis == 0 ? V.x : Axis == 1 ? V.y : V.z; }
	void SetComponent( FVector3& V, int32 Axis, float32 Value ) { ( Axis == 0 ? V.x : Axis == 1 ? V.y : V.z ) = Value; }

	int32 AxisIndexOf( EEditorGizmoHandle Handle )
	{
		switch ( Handle )
		{
		case EEditorGizmoHandle::AxisX: return 0;
		case EEditorGizmoHandle::AxisY: return 1;
		case EEditorGizmoHandle::AxisZ: return 2;
		case EEditorGizmoHandle::PlaneYZ: return 0;   // 面は、法線の軸の番号
		case EEditorGizmoHandle::PlaneXZ: return 1;
		case EEditorGizmoHandle::PlaneXY: return 2;
		default: return -1;
		}
	}

	bool IsAxisHandle( EEditorGizmoHandle Handle ) { return Handle == EEditorGizmoHandle::AxisX || Handle == EEditorGizmoHandle::AxisY || Handle == EEditorGizmoHandle::AxisZ; }
	bool IsPlaneHandle( EEditorGizmoHandle Handle ) { return Handle == EEditorGizmoHandle::PlaneYZ || Handle == EEditorGizmoHandle::PlaneXZ || Handle == EEditorGizmoHandle::PlaneXY; }

	float32 WrapPi( float32 Angle )
	{
		while ( Angle > NMath::PI ) Angle -= NMath::TWO_PI;
		while ( Angle < -NMath::PI ) Angle += NMath::TWO_PI;
		return Angle;
	}

	// 刻みに丸める（Step <= 0 なら、そのまま）
	float32 SnapValue( float32 Value, float32 Step )
	{
		return Step > 0.0f ? std::round( Value / Step ) * Step : Value;
	}

	// 軸の線分と光線の距離を、画面の太さの許容で比べる
	bool NearAxis( const FEditorGizmoFrame& Frame, int32 Axis, const FEditorRay& Ray, float32 Tolerance, float32& OutDistance )
	{
		FVector3 Start, End;
		NWorldEditor::GetGizmoAxisSegment( Frame, Axis, Start, End );
		float32 RayT = 0.0f, SegmentS = 0.0f;
		OutDistance = NWorldEditor::DistanceRayToSegment( Ray, Start, End, RayT, SegmentS );
		return OutDistance <= Tolerance;
	}
}

FEditorGizmoFrame NWorldEditor::MakeGizmoFrame( const FEditorCamera& Camera, const FEditorViewport& Viewport, const FVector3& Origin, const FQuaternion& Orientation, float32 ScreenLength )
{
	FEditorGizmoFrame Frame;
	Frame.Origin = Origin;
	Frame.Axes[0] = Orientation.Rotate( FVector3::Right );
	Frame.Axes[1] = Orientation.Rotate( FVector3::Up );
	Frame.Axes[2] = Orientation.Rotate( FVector3::Forward );
	Frame.PixelSize = Camera.GetPixelSize( Viewport, Origin );
	Frame.Length = Frame.PixelSize * ScreenLength;
	const FVector3 ToCamera = Camera.GetEye() - Origin;
	for ( int32 Axis = 0; Axis < 3; ++Axis ) Frame.Flip[Axis] = FVector3::Dot( Frame.Axes[Axis], ToCamera ) < 0.0f ? -1.0f : 1.0f;
	return Frame;
}

void NWorldEditor::GetGizmoAxisSegment( const FEditorGizmoFrame& Frame, int32 Axis, FVector3& OutStart, FVector3& OutEnd )
{
	OutStart = Frame.Origin;
	OutEnd = Frame.Origin + Frame.Axes[Axis] * ( Frame.Flip[Axis] * Frame.Length );
}

void NWorldEditor::GetGizmoPlaneCorners( const FEditorGizmoFrame& Frame, int32 NormalAxis, FVector3 ( &OutCorners )[4] )
{
	const int32 AxisA = ( NormalAxis + 1 ) % 3;
	const int32 AxisB = ( NormalAxis + 2 ) % 3;
	const FVector3 A = Frame.Axes[AxisA] * Frame.Flip[AxisA];
	const FVector3 B = Frame.Axes[AxisB] * Frame.Flip[AxisB];
	const float32 Near = Frame.Length * 0.25f;
	const float32 Far = Frame.Length * 0.55f;
	OutCorners[0] = Frame.Origin + A * Near + B * Near;
	OutCorners[1] = Frame.Origin + A * Far + B * Near;
	OutCorners[2] = Frame.Origin + A * Far + B * Far;
	OutCorners[3] = Frame.Origin + A * Near + B * Far;
}

float32 NWorldEditor::GetGizmoRingRadius( const FEditorGizmoFrame& Frame )
{
	return Frame.Length * 0.8f;
}

float32 NWorldEditor::GetGizmoCenterRadius( const FEditorGizmoFrame& Frame )
{
	return std::max( Frame.Length * 0.12f, Frame.PixelSize * HitPixels );
}

EEditorGizmoHandle NWorldEditor::HitTestGizmo( const FEditorGizmoFrame& Frame, EEditorGizmoMode Mode, const FEditorRay& Ray, float32* OutT )
{
	const float32 Tolerance = Frame.PixelSize * HitPixels;

	if ( Mode == EEditorGizmoMode::Rotate )
	{
		// 3 つの輪。カメラ側の半分だけ（向こう側の輪は、手前の物の陰として扱う）
		EEditorGizmoHandle Best = EEditorGizmoHandle::None;
		float32 BestT = NMath::MaxFloat;
		const float32 Radius = GetGizmoRingRadius( Frame );
		for ( int32 Axis = 0; Axis < 3; ++Axis )
		{
			float32 T = 0.0f;
			if ( !RayVsPlane( Ray, Frame.Origin, Frame.Axes[Axis], T ) ) continue;
			const FVector3 Offset = Ray.PointAt( T ) - Frame.Origin;
			if ( std::fabs( Offset.Length() - Radius ) > Tolerance ) continue;
			if ( FVector3::Dot( Offset, Ray.Origin - Frame.Origin ) < 0.0f ) continue;
			if ( T < BestT )
			{
				BestT = T;
				Best = GetGizmoAxisHandle( Axis );
			}
		}
		if ( Best != EEditorGizmoHandle::None && OutT != nullptr ) *OutT = BestT;
		return Best;
	}

	// 中央（移動: 画面に平行に / 拡大: まとめて）
	float32 T = 0.0f;
	if ( RayVsSphere( Ray, Frame.Origin, GetGizmoCenterRadius( Frame ), T ) )
	{
		if ( OutT != nullptr ) *OutT = T;
		return EEditorGizmoHandle::Free;
	}

	// 面（移動だけ）
	EEditorGizmoHandle BestPlane = EEditorGizmoHandle::None;
	float32 BestPlaneT = NMath::MaxFloat;
	if ( Mode == EEditorGizmoMode::Translate )
	{
		for ( int32 Normal = 0; Normal < 3; ++Normal )
		{
			FVector3 Corners[4];
			GetGizmoPlaneCorners( Frame, Normal, Corners );
			const FVector3 Center = ( Corners[0] + Corners[2] ) * 0.5f;
			if ( RayVsQuad( Ray, Center, ( Corners[1] - Corners[0] ) * 0.5f, ( Corners[3] - Corners[0] ) * 0.5f, T ) && T < BestPlaneT )
			{
				BestPlaneT = T;
				BestPlane = GetGizmoPlaneHandle( Normal );
			}
		}
	}

	// 軸（光線にいちばん近いもの）
	EEditorGizmoHandle BestAxis = EEditorGizmoHandle::None;
	float32 BestDistance = NMath::MaxFloat;
	float32 BestAxisT = NMath::MaxFloat;
	for ( int32 Axis = 0; Axis < 3; ++Axis )
	{
		float32 Distance = 0.0f;
		if ( !NearAxis( Frame, Axis, Ray, Tolerance, Distance ) || Distance >= BestDistance ) continue;
		FVector3 Start, End;
		GetGizmoAxisSegment( Frame, Axis, Start, End );
		float32 SegmentS = 0.0f;
		DistanceRayToSegment( Ray, Start, End, BestAxisT, SegmentS );
		BestDistance = Distance;
		BestAxis = GetGizmoAxisHandle( Axis );
	}

	// 面と軸が重なったら、手前のほう
	if ( BestPlane != EEditorGizmoHandle::None && ( BestAxis == EEditorGizmoHandle::None || BestPlaneT <= BestAxisT ) )
	{
		if ( OutT != nullptr ) *OutT = BestPlaneT;
		return BestPlane;
	}
	if ( BestAxis != EEditorGizmoHandle::None && OutT != nullptr ) *OutT = BestAxisT;
	return BestAxis;
}

bool FEditorGizmoDrag::Begin( const FEditorGizmoFrame& InFrame, EEditorGizmoMode InMode, EEditorGizmoHandle InHandle, const FEditorRay& StartRay, const FEditorCamera& Camera )
{
	bActive = false;
	Frame = InFrame;
	Mode = InMode;
	Handle = InHandle;
	Last = FEditorGizmoDelta();
	AxisIndex = AxisIndexOf( Handle );
	AccumulatedAngle = 0.0f;
	LastRawAngle = 0.0f;
	if ( Handle == EEditorGizmoHandle::None ) return false;

	const bool bAxis = IsAxisHandle( Handle );
	if ( bAxis && Mode != EEditorGizmoMode::Rotate )
	{
		// 軸に沿って動かす: 軸の直線の上の、光線にいちばん近い点
		if ( std::fabs( FVector3::Dot( StartRay.Direction, Frame.Axes[AxisIndex] ) ) >= AxisParallelLimit ) return false;
		float32 RayT = 0.0f;
		if ( !NWorldEditor::ClosestPointsOnLines( StartRay.Origin, StartRay.Direction, Frame.Origin, Frame.Axes[AxisIndex], RayT, StartAxisParam ) ) return false;
		bActive = true;
		return true;
	}

	// 面の中で動かす・回す・画面に平行に動かす
	if ( bAxis ) PlaneNormal = Frame.Axes[AxisIndex];   // 回転: 軸に垂直な面
	else if ( IsPlaneHandle( Handle ) ) PlaneNormal = Frame.Axes[AxisIndex];
	else PlaneNormal = -Camera.GetForward();
	DiagonalDirection = ( Camera.GetRight() + Camera.GetUp() ).Normalized();

	if ( std::fabs( FVector3::Dot( StartRay.Direction, PlaneNormal ) ) < ParallelLimit ) return false;
	float32 T = 0.0f;
	if ( !NWorldEditor::RayVsPlane( StartRay, Frame.Origin, PlaneNormal, T ) ) return false;
	StartPoint = StartRay.PointAt( T );
	if ( Mode == EEditorGizmoMode::Rotate && ( StartPoint - Frame.Origin ).LengthSquared() < 1.0e-10f ) return false;
	bActive = true;
	return true;
}

FEditorGizmoDelta FEditorGizmoDrag::Update( const FEditorRay& Ray, float32 Snap )
{
	if ( !bActive ) return Last;

	if ( IsAxisHandle( Handle ) && Mode != EEditorGizmoMode::Rotate )
	{
		if ( std::fabs( FVector3::Dot( Ray.Direction, Frame.Axes[AxisIndex] ) ) >= AxisParallelLimit ) return Last;
		float32 RayT = 0.0f, Param = 0.0f;
		if ( !NWorldEditor::ClosestPointsOnLines( Ray.Origin, Ray.Direction, Frame.Origin, Frame.Axes[AxisIndex], RayT, Param ) ) return Last;

		FEditorGizmoDelta Delta;
		if ( Mode == EEditorGizmoMode::Translate )
		{
			Delta.Translation = Frame.Axes[AxisIndex] * SnapValue( Param - StartAxisParam, Snap );
		}
		else
		{
			// 手前へ反転して出している軸は、表示している向き（外側）へ引くと大きくなる
			const float32 Factor = std::max( 1.0f + SnapValue( Frame.Flip[AxisIndex] * ( Param - StartAxisParam ) / Frame.Length, Snap ), MinFactor );
			SetComponent( Delta.Scale, AxisIndex, Factor );
		}
		Last = Delta;
		return Last;
	}

	if ( std::fabs( FVector3::Dot( Ray.Direction, PlaneNormal ) ) < ParallelLimit ) return Last;
	float32 T = 0.0f;
	if ( !NWorldEditor::RayVsPlane( Ray, Frame.Origin, PlaneNormal, T ) ) return Last;
	const FVector3 Point = Ray.PointAt( T );

	FEditorGizmoDelta Delta;
	if ( Mode == EEditorGizmoMode::Rotate )
	{
		const FVector3 From = StartPoint - Frame.Origin;
		const FVector3 To = Point - Frame.Origin;
		if ( To.LengthSquared() < 1.0e-10f ) return Last;
		const float32 Raw = std::atan2( FVector3::Dot( PlaneNormal, FVector3::Cross( From, To ) ), FVector3::Dot( From, To ) );
		AccumulatedAngle += WrapPi( Raw - LastRawAngle );   // 1 周を超えても、続けて数える
		LastRawAngle = Raw;
		const float32 Angle = SnapValue( AccumulatedAngle, Snap );
		Delta.Rotation = FQuaternion::FromAxisAngle( PlaneNormal, Angle );
		Delta.AngleRadians = Angle;
	}
	else if ( Mode == EEditorGizmoMode::Translate )
	{
		// 面の中の動きを、ギズモの 3 軸に分けて、それぞれ刻む
		const FVector3 Move = Point - StartPoint;
		FVector3 Result = FVector3::Zero;
		for ( int32 Axis = 0; Axis < 3; ++Axis ) Result = Result + Frame.Axes[Axis] * SnapValue( FVector3::Dot( Move, Frame.Axes[Axis] ), Snap );
		Delta.Translation = Result;
	}
	else
	{
		// まとめて拡大: 画面の右上へ動かすと大きく、左下へ動かすと小さく
		const float32 Amount = FVector3::Dot( Point - StartPoint, DiagonalDirection ) / Frame.Length;
		const float32 Factor = std::max( 1.0f + SnapValue( Amount, Snap ), MinFactor );
		Delta.Scale = FVector3( Factor, Factor, Factor );
	}
	Last = Delta;
	return Last;
}

FVector3 NWorldEditor::ApplyGizmoDeltaToPoint( const FVector3& Point, const FEditorGizmoDelta& Delta, EEditorGizmoMode Mode, const FEditorGizmoFrame& Frame )
{
	switch ( Mode )
	{
	case EEditorGizmoMode::Translate:
		return Point + Delta.Translation;
	case EEditorGizmoMode::Rotate:
		return Frame.Origin + Delta.Rotation.Rotate( Point - Frame.Origin );
	case EEditorGizmoMode::Scale:
	{
		const FVector3 Offset = Point - Frame.Origin;
		FVector3 Result = Frame.Origin;
		for ( int32 Axis = 0; Axis < 3; ++Axis ) Result = Result + Frame.Axes[Axis] * ( FVector3::Dot( Offset, Frame.Axes[Axis] ) * Component( Delta.Scale, Axis ) );
		return Result;
	}
	}
	return Point;
}

FTransform NWorldEditor::ApplyGizmoDelta( const FTransform& Start, const FEditorGizmoDelta& Delta, EEditorGizmoMode Mode, const FEditorGizmoFrame& Frame )
{
	FTransform Result = Start;
	switch ( Mode )
	{
	case EEditorGizmoMode::Translate:
		Result.Position = Start.Position + Delta.Translation;
		break;
	case EEditorGizmoMode::Rotate:
		Result.Position = Frame.Origin + Delta.Rotation.Rotate( Start.Position - Frame.Origin );
		Result.Rotation = ( Delta.Rotation * Start.Rotation ).Normalized();
		break;
	case EEditorGizmoMode::Scale:
	{
		const bool bUniform = Delta.Scale.x == Delta.Scale.y && Delta.Scale.y == Delta.Scale.z;
		const FVector3 ObjectAxes[3] = { Start.Rotation.Rotate( FVector3::Right ), Start.Rotation.Rotate( FVector3::Up ), Start.Rotation.Rotate( FVector3::Forward ) };
		FVector3 Factors = FVector3::One;
		for ( int32 Local = 0; Local < 3; ++Local )
		{
			float32 Factor = 1.0f;
			if ( bUniform ) Factor = Delta.Scale.x;
			else
			{
				// ギズモの軸ごとの倍率を、オブジェクト自身の軸に、向きの近さに応じて配る
				for ( int32 Axis = 0; Axis < 3; ++Axis ) Factor += ( Component( Delta.Scale, Axis ) - 1.0f ) * std::fabs( FVector3::Dot( ObjectAxes[Local], Frame.Axes[Axis] ) );
			}
			SetComponent( Factors, Local, std::max( Factor, MinFactor ) );
		}
		Result.Scale = Start.Scale * Factors;
		break;
	}
	}
	return Result;
}
