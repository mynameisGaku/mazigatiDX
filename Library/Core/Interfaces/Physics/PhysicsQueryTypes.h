// SPDX-License-Identifier: NOASSERTION
#pragma once
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Interfaces/Physics/PhysicsLayerTypes.h"
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Vector3.h"

class FRigidBody;

//=============================================================================
// FQueryShape
//   空間クエリで当てる形。物理エンジン（Jolt）の型を使わないので、これを呼ぶ側は Jolt に依存しない。
//   Capsule は Y 軸方向。HalfHeight は円柱部分の半分の高さ（半球を含まない）。
//
//   FQueryShape::Sphere( 0.5f );   FQueryShape::Box( { 1, 1, 1 } );   FQueryShape::Capsule( 0.5f, 0.3f );
//=============================================================================
struct FQueryShape
{
	enum class EType : uint8
	{
		Sphere,
		Box,
		Capsule,
	};

	EType    Type        = EType::Sphere;
	float32  Radius      = 0.5f;                     // Sphere / Capsule
	float32  HalfHeight  = 0.5f;                     // Capsule
	FVector3 HalfExtents = FVector3( 0.5f, 0.5f, 0.5f );  // Box（各軸の半分のサイズ）

	static FQueryShape Sphere( float32 InRadius )
	{
		FQueryShape Shape;
		Shape.Type   = EType::Sphere;
		Shape.Radius = InRadius;
		return Shape;
	}

	static FQueryShape Box( const FVector3& InHalfExtents )
	{
		FQueryShape Shape;
		Shape.Type        = EType::Box;
		Shape.HalfExtents = InHalfExtents;
		return Shape;
	}

	static FQueryShape Capsule( float32 InHalfHeight, float32 InRadius )
	{
		FQueryShape Shape;
		Shape.Type       = EType::Capsule;
		Shape.HalfHeight = InHalfHeight;
		Shape.Radius     = InRadius;
		return Shape;
	}
};

//=============================================================================
// FPhysicsQueryFilter
//   空間クエリの対象を絞る。既定は「センサー（トリガー）を除く、すべての剛体」。
//
//   const FRigidBody* Ignore[] = { Player };
//   FPhysicsQueryFilter Filter;
//   Filter.IgnoreBodies = Ignore;           // 自分自身には当たらない
//   Filter.bIncludeDynamic = false;         // 地形だけを調べる
//   Filter.LayerMask = NCollisionLayer::ToMask( Enemy );  // 敵のレイヤーだけを調べる
//=============================================================================
struct FPhysicsQueryFilter
{
	bool bIncludeStatic    = true;
	bool bIncludeKinematic = true;
	bool bIncludeDynamic   = true;
	bool bIncludeSensors   = false;  // bIsSensor の剛体（トリガー）を対象にするか

	// 対象にするレイヤー（NCollisionLayer::ToMask を足し合わせる）。既定はすべて。0 なら何にも当たらない
	uint32 LayerMask = NCollisionLayer::AllMask;

	// 対象から外す剛体。参照先は、クエリを呼ぶ間だけ有効であればよい
	TArrayView<const FRigidBody* const> IgnoreBodies;
};

//=============================================================================
// 結果
//   Body は当たった剛体（FRigidBody の中身を使うには Library/Physics/RigidBody.h が要る）。
//   UserData は、その剛体に SetUserData で付けた値と同じ。ゲームオブジェクトに戻すだけなら、これで足りる。
//=============================================================================
struct FRayHit
{
	FRigidBody* Body     = nullptr;
	void*       UserData = nullptr;
	FVector3    Position = FVector3::Zero;  // 当たった点（ワールド）
	FVector3    Normal   = FVector3::Zero;  // 当たった面の外向きの法線（ワールド、長さ 1）
	float32     Distance = 0.0f;            // 始点から当たった点までの距離。始点が凸形状の中なら 0
};

struct FShapeCastHit
{
	FRigidBody* Body     = nullptr;
	void*       UserData = nullptr;
	FVector3    Position = FVector3::Zero;  // 相手の剛体の表面の当たった点（ワールド）
	FVector3    Normal   = FVector3::Zero;  // 相手の面の外向きの法線（動かした形状を押し返す向き。ワールド、長さ 1）
	float32     Distance = 0.0f;            // 形状の中心が動いた距離。最初から重なっていれば 0
};

struct FOverlapHit
{
	FRigidBody* Body     = nullptr;
	void*       UserData = nullptr;
};
