#include "PhysicsSystem.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Physics/PhysicsInput.h"

#pragma push_macro( "min" )
#pragma push_macro( "max" )
#undef min
#undef max
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#pragma pop_macro( "max" )
#pragma pop_macro( "min" )

namespace
{
	// 値の確かめと向きの正規化は、関節と共有する（PhysicsInput.h）
	using NPhysicsInput::IsFiniteRotation;
	using NPhysicsInput::IsFiniteVector;
	using NPhysicsInput::NormalizeDirection;

	// 始点・向き・距離が使えるか確かめ、長さ 1 の向きを返す
	bool PrepareCast( const FVector3& Origin, const FVector3& Direction, float32 MaxDistance, FVector3& OutDirection )
	{
		if ( !IsFiniteVector( Origin ) || !NMath::IsFinite( MaxDistance ) || MaxDistance <= 0.0f ) return false;
		return NormalizeDirection( Direction, OutDirection );
	}

	// 問い合わせの形を、Jolt の形状にする。大きさが 0 以下・NaN・無限大なら false
	bool MakeCollisionShape( const FQueryShape& Query, FCollisionShape& OutShape )
	{
		auto IsSize = []( float32 Value ) { return NMath::IsFinite( Value ) && Value > 0.0f; };

		switch ( Query.Type )
		{
		case FQueryShape::EType::Sphere:
			if ( !IsSize( Query.Radius ) ) return false;
			OutShape = FCollisionShape::Sphere( Query.Radius );
			return true;
		case FQueryShape::EType::Box:
			if ( !IsSize( Query.HalfExtents.x ) || !IsSize( Query.HalfExtents.y ) || !IsSize( Query.HalfExtents.z ) ) return false;
			OutShape = FCollisionShape::Box( Query.HalfExtents );
			return true;
		case FQueryShape::EType::Capsule:
			if ( !IsSize( Query.HalfHeight ) || !IsSize( Query.Radius ) ) return false;
			OutShape = FCollisionShape::Capsule( Query.HalfHeight, Query.Radius );
			return true;
		}
		return false;
	}

	// 形の置き場所（ワールド）。回転は長さ 1 にする
	JPH::RMat44 MakeWorldTransform( const FVector3& Position, const FQuaternion& Rotation )
	{
		return JPH::RMat44::sRotationTranslation( NPhysics::ToPhys( Rotation.Normalized() ), NPhysics::ToPhysR( Position ) );
	}

	//-------------------------------------------------------------------------
	// 対象を絞る
	//   CreateBody で作った剛体だけを通す。種類（静的・キネマティック・動的）とセンサーは、Jolt が剛体を掴んだ後で見る
	//-------------------------------------------------------------------------
	class FQueryBodyFilter final : public JPH::BodyFilter
	{
	public:
		FQueryBodyFilter( const CPhysicsSystem& InSystem, const FPhysicsQueryFilter& InFilter )
			: System( InSystem )
			, Filter( InFilter )
		{}

		bool ShouldCollide( const JPH::BodyID& BodyID ) const override
		{
			const FRigidBody* Body = System.FindBody( BodyID );
			return Body != nullptr && !Filter.IgnoreBodies.Contains( Body );
		}

		bool ShouldCollideLocked( const JPH::Body& Body ) const override
		{
			if ( Body.IsSensor() && !Filter.bIncludeSensors ) return false;

			switch ( Body.GetMotionType() )
			{
			case JPH::EMotionType::Static:    return Filter.bIncludeStatic;
			case JPH::EMotionType::Kinematic: return Filter.bIncludeKinematic;
			case JPH::EMotionType::Dynamic:   return Filter.bIncludeDynamic;
			}
			return false;
		}

	private:
		const CPhysicsSystem&      System;
		const FPhysicsQueryFilter& Filter;
	};

	//-------------------------------------------------------------------------
	// 対象のレイヤーを絞る
	//   Jolt が剛体を掴む前（ブロードフェーズ）に、衝突レイヤーで落とす
	//-------------------------------------------------------------------------
	class FQueryLayerFilter final : public JPH::ObjectLayerFilter
	{
	public:
		explicit FQueryLayerFilter( uint32 InLayerMask )
			: LayerMask( InLayerMask )
		{}

		bool ShouldCollide( JPH::ObjectLayer Layer ) const override
		{
			return ( LayerMask & NCollisionLayer::ToMask( NPhysics::ToCollisionLayer( Layer ) ) ) != 0;
		}

	private:
		uint32 LayerMask;
	};

	// レイの当たりを、ゲーム側の結果にする。剛体が消えていたら false
	bool FillRayHit( const JPH::PhysicsSystem& Jolt, const CPhysicsSystem& System, const JPH::RRayCast& Ray, float32 MaxDistance,
					 const FVector3& Direction, const JPH::RayCastResult& Hit, FRayHit& OutHit )
	{
		FRigidBody* Body = System.FindBody( Hit.mBodyID );
		if ( Body == nullptr ) return false;

		const JPH::RVec3 Point = Ray.GetPointOnRay( Hit.mFraction );

		JPH::BodyLockRead Lock( Jolt.GetBodyLockInterface(), Hit.mBodyID );
		if ( !Lock.Succeeded() ) return false;

		FVector3 Normal = NPhysics::FromPhys( Lock.GetBody().GetWorldSpaceSurfaceNormal( Hit.mSubShapeID2, Point ) );
		if ( !IsFiniteVector( Normal ) || Normal.LengthSquared() < 0.25f ) Normal = -Direction;  // 面の向きが決まらないとき（中心など）は、レイを押し返す向き

		OutHit.Body     = Body;
		OutHit.UserData = Body->GetUserData();
		OutHit.Position = NPhysics::FromPhys( Point );
		OutHit.Normal   = Normal.Normalized();
		OutHit.Distance = NMath::Clamp( Hit.mFraction * MaxDistance, 0.0f, MaxDistance );
		return true;
	}
}

#pragma region Query

bool CPhysicsSystem::RayCast( const FVector3& Origin, const FVector3& Direction, float32 MaxDistance, FRayHit& OutHit, const FPhysicsQueryFilter& Filter ) const
{
	OutHit = FRayHit();

	FVector3 Unit;
	if ( !bInitialized || !PrepareCast( Origin, Direction, MaxDistance, Unit ) ) return false;

	const JPH::RRayCast Ray( NPhysics::ToPhysR( Origin ), NPhysics::ToPhys( Unit * MaxDistance ) );
	const FQueryBodyFilter BodyFilter( *this, Filter );
	const FQueryLayerFilter LayerFilter( Filter.LayerMask );
	JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> Collector;
	JoltSystem->GetNarrowPhaseQuery().CastRay( Ray, JPH::RayCastSettings(), Collector, JPH::BroadPhaseLayerFilter(), LayerFilter, BodyFilter );
	if ( !Collector.HadHit() ) return false;

	return FillRayHit( *JoltSystem, *this, Ray, MaxDistance, Unit, Collector.mHit, OutHit );
}

int32 CPhysicsSystem::RayCastAll( const FVector3& Origin, const FVector3& Direction, float32 MaxDistance, TArray<FRayHit>& OutHits, const FPhysicsQueryFilter& Filter ) const
{
	OutHits.Reset();

	FVector3 Unit;
	if ( !bInitialized || !PrepareCast( Origin, Direction, MaxDistance, Unit ) ) return 0;

	const JPH::RRayCast Ray( NPhysics::ToPhysR( Origin ), NPhysics::ToPhys( Unit * MaxDistance ) );
	const FQueryBodyFilter BodyFilter( *this, Filter );
	const FQueryLayerFilter LayerFilter( Filter.LayerMask );
	JPH::AllHitCollisionCollector<JPH::CastRayCollector> Collector;
	JoltSystem->GetNarrowPhaseQuery().CastRay( Ray, JPH::RayCastSettings(), Collector, JPH::BroadPhaseLayerFilter(), LayerFilter, BodyFilter );
	Collector.Sort();

	OutHits.Reserve( static_cast<int32>( Collector.mHits.size() ) );
	for ( const JPH::RayCastResult& Hit : Collector.mHits )
	{
		FRayHit Converted;
		if ( FillRayHit( *JoltSystem, *this, Ray, MaxDistance, Unit, Hit, Converted ) ) OutHits.Add( Converted );
	}
	return OutHits.Num();
}

int32 CPhysicsSystem::Overlap( const FQueryShape& Shape, const FVector3& Position, const FQuaternion& Rotation, TArray<FOverlapHit>& OutHits, const FPhysicsQueryFilter& Filter ) const
{
	OutHits.Reset();

	FCollisionShape QueryShape;
	if ( !bInitialized || !IsFiniteVector( Position ) || !IsFiniteRotation( Rotation ) || !MakeCollisionShape( Shape, QueryShape ) ) return 0;

	const JPH::Shape* JoltShape = QueryShape.GetShape().GetPtr();
	const JPH::RMat44 CenterOfMass = MakeWorldTransform( Position, Rotation ).PreTranslated( JoltShape->GetCenterOfMass() );  // Jolt は重心の位置を受け取る

	const FQueryBodyFilter BodyFilter( *this, Filter );
	const FQueryLayerFilter LayerFilter( Filter.LayerMask );
	JPH::CollideShapeSettings CollideSettings;
	CollideSettings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;  // 重なりは幾何学的な判定なので、メッシュの裏側にいても重なる（レイと掃引は裏面を無視する）
	JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> Collector;
	JoltSystem->GetNarrowPhaseQuery().CollideShape( JoltShape, JPH::Vec3::sReplicate( 1.0f ), CenterOfMass, CollideSettings, CenterOfMass.GetTranslation(),
													Collector, JPH::BroadPhaseLayerFilter(), LayerFilter, BodyFilter );

	for ( const JPH::CollideShapeResult& Hit : Collector.mHits )
	{
		FRigidBody* Body = FindBody( Hit.mBodyID2 );
		if ( Body == nullptr ) continue;
		if ( OutHits.ContainsByPredicate( [Body]( const FOverlapHit& Existing ) { return Existing.Body == Body; } ) ) continue;  // 三角形などで 1 つの剛体が何度も出る

		FOverlapHit Converted;
		Converted.Body     = Body;
		Converted.UserData = Body->GetUserData();
		OutHits.Add( Converted );
	}
	return OutHits.Num();
}

bool CPhysicsSystem::Sweep( const FQueryShape& Shape, const FVector3& Start, const FQuaternion& Rotation, const FVector3& Direction, float32 MaxDistance,
							FShapeCastHit& OutHit, const FPhysicsQueryFilter& Filter ) const
{
	OutHit = FShapeCastHit();

	FVector3 Unit;
	FCollisionShape QueryShape;
	if ( !bInitialized || !IsFiniteRotation( Rotation ) || !PrepareCast( Start, Direction, MaxDistance, Unit ) || !MakeCollisionShape( Shape, QueryShape ) ) return false;

	const JPH::RShapeCast Cast = JPH::RShapeCast::sFromWorldTransform( QueryShape.GetShape().GetPtr(), JPH::Vec3::sReplicate( 1.0f ),
																	   MakeWorldTransform( Start, Rotation ), NPhysics::ToPhys( Unit * MaxDistance ) );

	const FQueryBodyFilter BodyFilter( *this, Filter );
	const FQueryLayerFilter LayerFilter( Filter.LayerMask );
	JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> Collector;
	JoltSystem->GetNarrowPhaseQuery().CastShape( Cast, JPH::ShapeCastSettings(), NPhysics::ToPhysR( Start ), Collector,
												 JPH::BroadPhaseLayerFilter(), LayerFilter, BodyFilter );
	if ( !Collector.HadHit() ) return false;

	const JPH::ShapeCastResult& Hit = Collector.mHit;
	FRigidBody* Body = FindBody( Hit.mBodyID2 );
	if ( Body == nullptr ) return false;

	// 当たりの点は、掃引の始点からの相対で返ってくる。法線は「相手を押し出す向き」の逆（相手の面から、動かした形状へ向かう向き）
	FVector3 Normal;
	if ( !NormalizeDirection( -NPhysics::FromPhys( Hit.mPenetrationAxis ), Normal ) ) Normal = -Unit;  // 向きの長さは意味を持たないので、小さくても向きは使える

	OutHit.Body     = Body;
	OutHit.UserData = Body->GetUserData();
	OutHit.Position = Start + NPhysics::FromPhys( Hit.mContactPointOn2 );
	OutHit.Normal   = Normal;
	OutHit.Distance = NMath::Clamp( Hit.mFraction * MaxDistance, 0.0f, MaxDistance );
	return true;
}

#pragma endregion
