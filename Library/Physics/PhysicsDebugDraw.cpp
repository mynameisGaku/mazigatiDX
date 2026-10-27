#include "PhysicsSystem.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Physics/PhysicsInput.h"

#pragma push_macro( "min" )
#pragma push_macro( "max" )
#undef min
#undef max
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#pragma pop_macro( "max" )
#pragma pop_macro( "min" )

namespace
{
	constexpr int32   MinCircleSegments = 8;
	constexpr int32   MaxCircleSegments = 128;
	constexpr float32 CrossHalfSize     = 0.1f;   // 関節の取り付け点の十字の大きさ
	constexpr float32 HingeAxisHalfSize = 0.3f;   // 蝶番の軸の線の長さ（片側）
	constexpr float32 GroundNormalSize  = 0.5f;   // キャラクターの足元の法線の長さ

	//-------------------------------------------------------------------------
	// 線を 1 本ずつ渡し、本数を数える
	//-------------------------------------------------------------------------
	struct FLineSink
	{
		FLineSink( IPhysicsDebugDrawer& InDrawer, const FColor32& InColor ) : Drawer( InDrawer ), Color( InColor ) {}

		void Line( JPH::Vec3Arg A, JPH::Vec3Arg B )
		{
			Drawer.DrawLine( NPhysics::FromPhys( A ), NPhysics::FromPhys( B ), Color );
			++Count;
		}

		IPhysicsDebugDrawer& Drawer;
		FColor32             Color;
		int32                Count = 0;
	};

	//-------------------------------------------------------------------------
	// 位置と向き（図形の原点のワールド変換）
	//-------------------------------------------------------------------------
	struct FPose
	{
		JPH::Vec3 Position;
		JPH::Quat Rotation;

		JPH::Vec3 Apply( JPH::Vec3Arg Local ) const { return Position + Rotation * Local; }
		FPose Compose( JPH::Vec3Arg LocalPosition, JPH::QuatArg LocalRotation ) const { return { Apply( LocalPosition ), Rotation * LocalRotation }; }
	};

	// 図形を描くときの、共通の入力
	struct FShapeDraw
	{
		int32       Segments;
		int32       MaxTriangles;
		JPH::AABox  Region;     // 三角形を集める範囲（ワールド）
	};

	// 局所の円（中心 Center、軸 U / V の平面、半径 Radius）。弧は Start..End（ラジアン）
	void DrawArc( FLineSink& Sink, const FPose& Pose, JPH::Vec3Arg Center, JPH::Vec3Arg U, JPH::Vec3Arg V, float32 Radius, float32 Start, float32 End, int32 Steps )
	{
		JPH::Vec3 Previous = Pose.Apply( Center + Radius * ( NMath::Cos( Start ) * U + NMath::Sin( Start ) * V ) );
		for ( int32 i = 1; i <= Steps; ++i )
		{
			const float32 Angle = Start + ( End - Start ) * static_cast<float32>( i ) / static_cast<float32>( Steps );
			const JPH::Vec3 Current = Pose.Apply( Center + Radius * ( NMath::Cos( Angle ) * U + NMath::Sin( Angle ) * V ) );
			Sink.Line( Previous, Current );
			Previous = Current;
		}
	}

	void DrawBox( FLineSink& Sink, const FPose& Pose, JPH::Vec3Arg Half )
	{
		JPH::Vec3 Corner[8];
		for ( int32 i = 0; i < 8; ++i )
		{
			Corner[i] = Pose.Apply( JPH::Vec3( ( i & 1 ) ? Half.GetX() : -Half.GetX(), ( i & 2 ) ? Half.GetY() : -Half.GetY(), ( i & 4 ) ? Half.GetZ() : -Half.GetZ() ) );
		}
		// 角の番号の 1 ビットだけが違う組が、辺
		for ( int32 i = 0; i < 8; ++i )
		{
			for ( int32 Bit = 1; Bit <= 4; Bit <<= 1 )
			{
				if ( ( i & Bit ) == 0 ) Sink.Line( Corner[i], Corner[i | Bit] );
			}
		}
	}

	void DrawSphere( FLineSink& Sink, const FPose& Pose, float32 Radius, int32 Segments )
	{
		const JPH::Vec3 X = JPH::Vec3::sAxisX();
		const JPH::Vec3 Y = JPH::Vec3::sAxisY();
		const JPH::Vec3 Z = JPH::Vec3::sAxisZ();
		DrawArc( Sink, Pose, JPH::Vec3::sZero(), X, Y, Radius, 0.0f, NMath::TWO_PI, Segments );
		DrawArc( Sink, Pose, JPH::Vec3::sZero(), X, Z, Radius, 0.0f, NMath::TWO_PI, Segments );
		DrawArc( Sink, Pose, JPH::Vec3::sZero(), Y, Z, Radius, 0.0f, NMath::TWO_PI, Segments );
	}

	// Y 軸方向のカプセル。HalfHeight は円柱部分の半分の高さ
	void DrawCapsule( FLineSink& Sink, const FPose& Pose, float32 HalfHeight, float32 Radius, int32 Segments )
	{
		const JPH::Vec3 X = JPH::Vec3::sAxisX();
		const JPH::Vec3 Y = JPH::Vec3::sAxisY();
		const JPH::Vec3 Z = JPH::Vec3::sAxisZ();
		const JPH::Vec3 Top( 0.0f, HalfHeight, 0.0f );
		const JPH::Vec3 Bottom( 0.0f, -HalfHeight, 0.0f );
		const int32 HalfSteps = NMath::Max( 4, Segments / 2 );

		// 円柱の上下の円と、縦の 4 本
		DrawArc( Sink, Pose, Top, X, Z, Radius, 0.0f, NMath::TWO_PI, Segments );
		DrawArc( Sink, Pose, Bottom, X, Z, Radius, 0.0f, NMath::TWO_PI, Segments );
		Sink.Line( Pose.Apply( Top + Radius * X ), Pose.Apply( Bottom + Radius * X ) );
		Sink.Line( Pose.Apply( Top - Radius * X ), Pose.Apply( Bottom - Radius * X ) );
		Sink.Line( Pose.Apply( Top + Radius * Z ), Pose.Apply( Bottom + Radius * Z ) );
		Sink.Line( Pose.Apply( Top - Radius * Z ), Pose.Apply( Bottom - Radius * Z ) );

		// 上下の半球（X-Y 平面と Z-Y 平面の半円）
		DrawArc( Sink, Pose, Top, X, Y, Radius, 0.0f, NMath::PI, HalfSteps );
		DrawArc( Sink, Pose, Top, Z, Y, Radius, 0.0f, NMath::PI, HalfSteps );
		DrawArc( Sink, Pose, Bottom, X, -Y, Radius, 0.0f, NMath::PI, HalfSteps );
		DrawArc( Sink, Pose, Bottom, Z, -Y, Radius, 0.0f, NMath::PI, HalfSteps );
	}

	// 凸包・三角形メッシュなど、決まった輪郭を持たない形は、三角形の辺で描く（上限まで）
	void DrawTriangles( FLineSink& Sink, const JPH::Shape& Shape, const FPose& Pose, JPH::Vec3Arg Scale, const FShapeDraw& Draw )
	{
		if ( Draw.MaxTriangles <= 0 ) return;

		JPH::Shape::GetTrianglesContext Context;
		Shape.GetTrianglesStart( Context, Draw.Region, Pose.Apply( Scale * Shape.GetCenterOfMass() ), Pose.Rotation, Scale );

		constexpr int32 BatchTriangles = 64;   // GetTrianglesNext が求める最小（32）以上
		JPH::Float3 Vertices[3 * BatchTriangles];
		int32 Remaining = Draw.MaxTriangles;
		while ( Remaining > 0 )
		{
			const int32 Got = Shape.GetTrianglesNext( Context, BatchTriangles, Vertices );
			if ( Got <= 0 ) break;

			const int32 Use = NMath::Min( Got, Remaining );
			for ( int32 t = 0; t < Use; ++t )
			{
				const JPH::Vec3 A( Vertices[3 * t + 0] );
				const JPH::Vec3 B( Vertices[3 * t + 1] );
				const JPH::Vec3 C( Vertices[3 * t + 2] );
				Sink.Line( A, B );
				Sink.Line( B, C );
				Sink.Line( C, A );
			}
			Remaining -= Use;
		}
	}

	// 形を描く。飾りの形（拡大・ずらし）は、中の形へたどる
	void DrawShape( FLineSink& Sink, const JPH::Shape& Shape, const FPose& Pose, JPH::Vec3Arg Scale, const FShapeDraw& Draw )
	{
		switch ( Shape.GetSubType() )
		{
		case JPH::EShapeSubType::Box:
			DrawBox( Sink, Pose, static_cast<const JPH::BoxShape&>( Shape ).GetHalfExtent() * Scale.Abs() );
			break;
		case JPH::EShapeSubType::Sphere:
			DrawSphere( Sink, Pose, static_cast<const JPH::SphereShape&>( Shape ).GetRadius() * Scale.Abs().ReduceMax(), Draw.Segments );
			break;
		case JPH::EShapeSubType::Capsule:
		{
			const JPH::CapsuleShape& Capsule = static_cast<const JPH::CapsuleShape&>( Shape );
			const float32 Uniform = Scale.Abs().ReduceMax();
			DrawCapsule( Sink, Pose, Capsule.GetHalfHeightOfCylinder() * Uniform, Capsule.GetRadius() * Uniform, Draw.Segments );
			break;
		}
		case JPH::EShapeSubType::Scaled:
		{
			const JPH::ScaledShape& Scaled = static_cast<const JPH::ScaledShape&>( Shape );
			DrawShape( Sink, *Scaled.GetInnerShape(), Pose, Scale * Scaled.GetScale(), Draw );
			break;
		}
		case JPH::EShapeSubType::RotatedTranslated:
		{
			const JPH::RotatedTranslatedShape& Moved = static_cast<const JPH::RotatedTranslatedShape&>( Shape );
			DrawShape( Sink, *Moved.GetInnerShape(), Pose.Compose( Scale * Moved.GetPosition(), Moved.GetRotation() ), Scale, Draw );
			break;
		}
		default:
			DrawTriangles( Sink, Shape, Pose, Scale, Draw );
			break;
		}
	}

	// 外接の箱の 12 辺
	void DrawBounds( FLineSink& Sink, const JPH::AABox& Bounds )
	{
		DrawBox( Sink, FPose { Bounds.GetCenter(), JPH::Quat::sIdentity() }, Bounds.GetExtent() );
	}
}

int32 CPhysicsSystem::DrawDebug( IPhysicsDebugDrawer& Drawer, const FPhysicsDebugDrawSettings& DrawSettings ) const
{
	if ( !bInitialized ) return 0;

	// 範囲の指定が使えないなら、何も描かない
	JPH::Vec3 RegionCenter = JPH::Vec3::sZero();
	float32   RegionRadius = 0.0f;
	JPH::AABox RegionBox( JPH::Vec3::sReplicate( -1.0e30f ), JPH::Vec3::sReplicate( 1.0e30f ) );
	if ( DrawSettings.bUseRegion )
	{
		if ( !NPhysicsInput::IsFiniteVector( DrawSettings.RegionCenter ) || !NMath::IsFinite( DrawSettings.RegionRadius ) || DrawSettings.RegionRadius <= 0.0f ) return 0;
		RegionCenter = NPhysics::ToPhys( DrawSettings.RegionCenter );
		RegionRadius = DrawSettings.RegionRadius;
		RegionBox    = JPH::AABox( RegionCenter - JPH::Vec3::sReplicate( RegionRadius ), RegionCenter + JPH::Vec3::sReplicate( RegionRadius ) );
	}
	auto InRegion = [&]( const JPH::AABox& Bounds ) { return !DrawSettings.bUseRegion || Bounds.GetSqDistanceTo( RegionCenter ) <= RegionRadius * RegionRadius; };
	auto PointInRegion = [&]( JPH::Vec3Arg Point ) { return !DrawSettings.bUseRegion || ( Point - RegionCenter ).LengthSq() <= RegionRadius * RegionRadius; };

	FShapeDraw ShapeDraw;
	ShapeDraw.Segments     = NMath::Clamp( DrawSettings.CircleSegments, MinCircleSegments, MaxCircleSegments );
	ShapeDraw.MaxTriangles = DrawSettings.MaxMeshTriangles;

	const JPH::BodyInterface& BodyInterface = JoltSystem->GetBodyInterface();
	int32 Total = 0;

	//-------------------------------------------------------------------------
	// 剛体
	//-------------------------------------------------------------------------
	for ( const auto& [Key, BodyHandle] : Bodies )
	{
		const FRigidBody& Body = *BodyHandle;
		const JPH::BodyID BodyID = Body.GetBodyID();

		// センサーは種類に関わらず bDrawSensors で、そのほかは種類ごとの指定で決まる
		const bool bSensor = BodyInterface.IsSensor( BodyID );
		const bool bCharacter = IsCharacterBody( &Body );
		bool bDraw = DrawSettings.bDrawDynamic;
		if ( bSensor ) bDraw = DrawSettings.bDrawSensors;
		else if ( Body.GetBodyType() == EBodyType::Static ) bDraw = DrawSettings.bDrawStatic;
		else if ( Body.GetBodyType() == EBodyType::Kinematic ) bDraw = DrawSettings.bDrawKinematic;
		if ( !bDraw ) continue;

		const JPH::TransformedShape Transformed = BodyInterface.GetTransformedShape( BodyID );
		if ( Transformed.mShape == nullptr ) continue;
		const JPH::AABox Bounds = Transformed.GetWorldSpaceBounds();
		if ( !InRegion( Bounds ) ) continue;

		FColor32 Color = DrawSettings.DynamicColor;
		if ( bCharacter ) Color = DrawSettings.CharacterColor;
		else if ( bSensor ) Color = DrawSettings.SensorColor;
		else if ( Body.GetBodyType() == EBodyType::Static ) Color = DrawSettings.StaticColor;
		else if ( Body.GetBodyType() == EBodyType::Kinematic ) Color = DrawSettings.KinematicColor;
		else if ( !BodyInterface.IsActive( BodyID ) ) Color = DrawSettings.SleepingColor;

		// 形の原点（剛体の位置）と向き。形が重心からずれていても、形の中の座標は原点が基準
		JPH::RVec3 Position;
		JPH::Quat  Rotation;
		BodyInterface.GetPositionAndRotation( BodyID, Position, Rotation );

		FShapeDraw Draw = ShapeDraw;
		Draw.Region = DrawSettings.bUseRegion ? Bounds.Intersect( RegionBox ) : Bounds;

		FLineSink Sink( Drawer, Color );
		DrawShape( Sink, *Transformed.mShape, FPose { JPH::Vec3( Position ), Rotation }, JPH::Vec3::sOne(), Draw );
		Total += Sink.Count;

		if ( DrawSettings.bDrawBounds )
		{
			FLineSink BoundsSink( Drawer, DrawSettings.BoundsColor );
			DrawBounds( BoundsSink, Bounds );
			Total += BoundsSink.Count;
		}
	}

	//-------------------------------------------------------------------------
	// 関節
	//-------------------------------------------------------------------------
	if ( DrawSettings.bDrawConstraints )
	{
		for ( const TUniquePtr<FPhysicsConstraint>& Constraint : Constraints )
		{
			if ( Constraint->Joint == nullptr ) continue;

			const JPH::TwoBodyConstraint& Joint = *Constraint->Joint;
			const JPH::Vec3 PointB( Joint.GetBody1()->GetCenterOfMassTransform() * Joint.GetConstraintToBody1Matrix().GetTranslation() );
			const JPH::Vec3 PointA( Joint.GetBody2()->GetCenterOfMassTransform() * Joint.GetConstraintToBody2Matrix().GetTranslation() );
			if ( !PointInRegion( PointA ) && !PointInRegion( PointB ) ) continue;

			FLineSink Sink( Drawer, Joint.GetEnabled() ? DrawSettings.ConstraintColor : DrawSettings.DisabledColor );
			Sink.Line( PointB, PointA );
			const JPH::Vec3 Axes[3] = { JPH::Vec3::sAxisX(), JPH::Vec3::sAxisY(), JPH::Vec3::sAxisZ() };
			for ( const JPH::Vec3& Axis : Axes ) Sink.Line( PointA - CrossHalfSize * Axis, PointA + CrossHalfSize * Axis );

			if ( Constraint->Type == EConstraintType::Hinge )
			{
				const JPH::Vec3 Axis = Joint.GetBody2()->GetRotation() * static_cast<const JPH::HingeConstraint&>( Joint ).GetLocalSpaceHingeAxis2();
				Sink.Line( PointA - HingeAxisHalfSize * Axis, PointA + HingeAxisHalfSize * Axis );
			}
			Total += Sink.Count;
		}
	}

	//-------------------------------------------------------------------------
	// キャラクターの足元の法線
	//-------------------------------------------------------------------------
	if ( DrawSettings.bDrawCharacters )
	{
		for ( const TUniquePtr<FPhysicsCharacter>& Character : Characters )
		{
			if ( Character->Character == nullptr || Character->GetGroundState() == ECharacterGroundState::InAir ) continue;

			const JPH::Vec3 Feet( Character->Character->GetPosition() );
			if ( !PointInRegion( Feet ) ) continue;

			FLineSink Sink( Drawer, DrawSettings.CharacterColor );
			Sink.Line( Feet, Feet + GroundNormalSize * Character->Character->GetGroundNormal() );
			Total += Sink.Count;
		}
	}

	return Total;
}
