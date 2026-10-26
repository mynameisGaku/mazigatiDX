#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Physics/PhysicsTypes.h"

#pragma push_macro( "min" )
#pragma push_macro( "max" )
#undef min
#undef max
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#pragma pop_macro( "max" )
#pragma pop_macro( "min" )

//=============================================================================
// FCollisionShape
//   Jolt の Shape への参照（参照カウント）を保持する軽量なデータ。
//   コピーしても Shape 本体は共有される。
//
//   auto Box    = FCollisionShape::Box( { 1.0f, 1.0f, 1.0f } );   // 半径（ハーフサイズ）
//   auto Sphere = FCollisionShape::Sphere( 0.5f );
//   auto Mesh   = FCollisionShape::Mesh( Vertices, Indices );     // TResult
//=============================================================================
class FCollisionShape
{
public:
	FCollisionShape() = default;
	FORCEINLINE explicit FCollisionShape( PhysShapeRef InShape ) : Shape( MoveTemp( InShape ) ) {}

	//-------------------------------------------------------------------------
	// プリミティブ
	//-------------------------------------------------------------------------
	// HalfExtents: 各軸の半分のサイズ
	FORCEINLINE static FCollisionShape Box( const FVector3& HalfExtents )
	{
		const FVector3 Safe = ClampPositive( HalfExtents, "Box half extents" );
		return FCollisionShape( new JPH::BoxShape( NPhysics::ToPhys( Safe ) ) );
	}

	FORCEINLINE static FCollisionShape Sphere( float32 Radius )
	{
		return FCollisionShape( new JPH::SphereShape( ClampPositive( Radius, "Sphere radius" ) ) );
	}

	// Y 軸方向のカプセル。HalfHeight は円柱部分の半分の高さ（半球を含まない）
	FORCEINLINE static FCollisionShape Capsule( float32 HalfHeight, float32 Radius )
	{
		return FCollisionShape( new JPH::CapsuleShape(
			ClampPositive( HalfHeight, "Capsule half height" ),
			ClampPositive( Radius, "Capsule radius" ) ) );
	}

	//-------------------------------------------------------------------------
	// 頂点から生成（失敗し得るので TResult）
	//-------------------------------------------------------------------------
	static TResult<FCollisionShape> ConvexHull( TArrayView<const FVector3> Points );

	// 静的な三角形メッシュ（Static / Kinematic 専用。Dynamic には使えない）
	//   Indices は 3 つで 1 三角形。DxLib と同じく時計回り(左手系)が表面
	static TResult<FCollisionShape> Mesh( TArrayView<const FVector3> Vertices, TArrayView<const uint32> Indices );

	//-------------------------------------------------------------------------
	// 変換
	//-------------------------------------------------------------------------
	// スケールを適用した新しい形状（Sphere / Capsule は一様スケールに補正される）
	FORCEINLINE TResult<FCollisionShape> Scaled( const FVector3& Scale ) const
	{
		if ( !IsValid() ) return TResult<FCollisionShape>::Err( "Scaled: shape is invalid" );
		if ( Scale.Equals( FVector3::One ) ) return TResult<FCollisionShape>::Ok( *this );
		return FromShapeResult( Shape->ScaleShape( NPhysics::ToPhys( Scale ) ), "Scaled" );
	}

	FORCEINLINE bool IsValid() const { return Shape != nullptr; }
	FORCEINLINE const PhysShapeRef& GetShape() const { return Shape; }

private:
	template<typename ResultType>
	FORCEINLINE static TResult<FCollisionShape> FromShapeResult( const ResultType& Result, const char* Context )
	{
		if ( Result.HasError() )
		{
			FString Message = Context;
			Message += ": ";
			Message += Result.GetError().c_str();
			LOG( LogPhysics, Error, "Failed to create a collision shape: %s", *Message );
			return TResult<FCollisionShape>::Err( MoveTemp( Message ) );
		}
		return TResult<FCollisionShape>::Ok( FCollisionShape( Result.Get() ) );
	}

	FORCEINLINE static float32 ClampPositive( float32 Value, const char* Name )
	{
		constexpr float32 MinSize = 1.0e-3f;
		if ( Value >= MinSize ) return Value;
		LOG( LogPhysics, Warning, "%s must be positive (%f), clamped to %f", Name, Value, MinSize );
		return MinSize;
	}

	FORCEINLINE static FVector3 ClampPositive( const FVector3& Value, const char* Name )
	{
		return { ClampPositive( Value.x, Name ), ClampPositive( Value.y, Name ), ClampPositive( Value.z, Name ) };
	}

	PhysShapeRef Shape;
};
