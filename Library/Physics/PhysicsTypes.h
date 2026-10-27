#pragma once
#include "Library/Core/Types/CompilerMacros.h"

// windows.h の min / max マクロが Jolt と衝突するため一時退避
#pragma push_macro( "min" )
#pragma push_macro( "max" )
#undef min
#undef max

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#pragma pop_macro( "max" )
#pragma pop_macro( "min" )

#include "Library/Core/Interfaces/Physics/PhysicsLayerTypes.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Result.h"
#include "Library/Math/Math.h"

DECLARE_LOG_CATEGORY_EXTERN( LogPhysics, Log )

//=============================================================================
// Jolt 型エイリアス（JPH:: -> Phys プレフィックス）
//=============================================================================
using PhysBodyID          = JPH::BodyID;
using PhysVec3            = JPH::Vec3;
using PhysRVec3           = JPH::RVec3;
using PhysQuat            = JPH::Quat;
using PhysShape           = JPH::Shape;
using PhysShapeRef        = JPH::RefConst<JPH::Shape>;
using PhysObjectLayer     = JPH::ObjectLayer;
using PhysBroadPhaseLayer = JPH::BroadPhaseLayer;
using PhysMotionType      = JPH::EMotionType;

//=============================================================================
// EBodyType
//=============================================================================
enum class EBodyType : uint8
{
	Static,     // 動かない（地形など）
	Kinematic,  // プログラムで動かす。他の物体を押すが押されない
	Dynamic,    // 物理シミュレーションで動く
};

//=============================================================================
// レイヤー
//   Jolt のオブジェクトレイヤーは ( 衝突レイヤー << 1 ) | 動くか の形にする。
//   衝突レイヤー（NCollisionLayer, 0..31）は、ユーザーが決める「どれとぶつかるか」の区分。
//   動くか（下位 1 ビット）は、静的な剛体同士は衝突しない決まりと、ブロードフェーズのレイヤー分けに使う。
//=============================================================================
namespace NPhysicsLayers
{
	inline constexpr PhysObjectLayer NonMoving = 0;  // 下位ビットが 0: 静的
	inline constexpr PhysObjectLayer Moving    = 1;  // 下位ビットが 1: キネマティックと動的
	inline constexpr uint32          Count     = NCollisionLayer::MaxCount * 2;
}

namespace NPhysicsBroadPhaseLayers
{
	inline constexpr PhysBroadPhaseLayer NonMoving { 0 };
	inline constexpr PhysBroadPhaseLayer Moving    { 1 };
	inline constexpr uint32              Count = 2;
}

//=============================================================================
// 変換ヘルパー
//=============================================================================
namespace NPhysics
{
	FORCEINLINE PhysVec3 ToPhys( const FVector3& V ) { return PhysVec3( V.x, V.y, V.z ); }
	FORCEINLINE PhysQuat ToPhys( const FQuaternion& Q ) { return PhysQuat( Q.x, Q.y, Q.z, Q.w ); }

	FORCEINLINE FVector3 FromPhys( JPH::Vec3Arg V ) { return FVector3( V.GetX(), V.GetY(), V.GetZ() ); }
	FORCEINLINE FQuaternion FromPhys( JPH::QuatArg Q ) { return FQuaternion( Q.GetX(), Q.GetY(), Q.GetZ(), Q.GetW() ); }
#ifdef JPH_DOUBLE_PRECISION
	FORCEINLINE PhysRVec3 ToPhysR( const FVector3& V ) { return PhysRVec3( V.x, V.y, V.z ); }
	FORCEINLINE FVector3 FromPhys( JPH::DVec3Arg V ) { return FVector3( float32( V.GetX() ), float32( V.GetY() ), float32( V.GetZ() ) ); }
#else
	FORCEINLINE PhysRVec3 ToPhysR( const FVector3& V ) { return ToPhys( V ); }
#endif

	FORCEINLINE PhysMotionType ToPhysMotionType( EBodyType Type )
	{
		switch ( Type )
		{
		case EBodyType::Static:    return PhysMotionType::Static;
		case EBodyType::Kinematic: return PhysMotionType::Kinematic;
		case EBodyType::Dynamic:   return PhysMotionType::Dynamic;
		}
		return PhysMotionType::Static;
	}

	// 衝突レイヤー（0..31）と剛体の種類から、Jolt のオブジェクトレイヤーを作る
	FORCEINLINE PhysObjectLayer ToPhysObjectLayer( EBodyType Type, uint32 CollisionLayer )
	{
		const PhysObjectLayer Moving = Type == EBodyType::Static ? NPhysicsLayers::NonMoving : NPhysicsLayers::Moving;
		return static_cast<PhysObjectLayer>( ( CollisionLayer << 1 ) | Moving );
	}

	FORCEINLINE uint32 ToCollisionLayer( PhysObjectLayer Layer )
	{
		return static_cast<uint32>( Layer ) >> 1;
	}

	FORCEINLINE bool IsMovingObjectLayer( PhysObjectLayer Layer )
	{
		return ( Layer & 1 ) != 0;
	}
}
