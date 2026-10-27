// SPDX-License-Identifier: NOASSERTION
#pragma once
#include "Library/Core/Interfaces/Physics/PhysicsLayerTypes.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Vector3.h"

class FRigidBody;

//=============================================================================
// FContactInfo
//   接触 1 つぶんの情報。物理エンジン（Jolt）の型を使わないので、これを受け取る側は Jolt に依存しない。
//=============================================================================
struct FContactInfo
{
	FVector3 Point;                     // 接触点（ワールド座標、A 側の表面）
	FVector3 Normal;                    // 接触法線（A から B へ向かう方向、長さ 1）
	float32  PenetrationDepth = 0.0f;   // めり込み量

	// 接触した瞬間の、法線方向の近づく速さ（m/s）。A が B へ近づいているとき正、離れているとき負。
	// 衝撃音の大きさやダメージを決めるのに使う。接触の通知が来る時点では、まだぶつかった反動は反映されていない（ぶつかる前の速さ）
	float32  ClosingSpeed = 0.0f;
};

//=============================================================================
// FPhysicsContactFilter
//   接触の通知を受け取る相手を絞る。既定は「すべての剛体の、すべての通知」。
//
//   FPhysicsContactFilter Filter;
//   Filter.Body = Player;                                  // この剛体が絡む通知だけ
//   Filter.LayerMask = NCollisionLayer::ToMask( Enemy );   // どちらかの剛体が敵のレイヤーにいる通知だけ
//   Filter.bPersisted = false;                             // 毎ステップ来る「接触が続いている」通知は要らない
//=============================================================================
struct FPhysicsContactFilter
{
	// nullptr 以外なら、この剛体が絡む通知だけを受け取る。このワールドで作った剛体でなければ登録に失敗する
	FRigidBody* Body = nullptr;

	// 2 つの剛体のうち、どちらかの衝突レイヤーが含まれる通知だけを受け取る（NCollisionLayer::ToMask を足し合わせる）。0 なら何も届かない
	uint32 LayerMask = NCollisionLayer::AllMask;

	// 「接触が続いている」通知（OnContactPersisted。接触している間は毎ステップ来る）を受け取るか。
	// 接触の始まりと終わりだけで足りるなら false にすると、呼び出しが減る
	bool bPersisted = true;
};
