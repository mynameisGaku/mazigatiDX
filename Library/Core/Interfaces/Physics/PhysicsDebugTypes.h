// SPDX-License-Identifier: NOASSERTION
#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Color.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// FPhysicsDebugDrawSettings
//   物理の世界を線で描く（デバッグ描画）ときの、何を・どこまで・どんな色で描くか。物理エンジン（Jolt）の型を使わない。
//
//   FPhysicsDebugDrawSettings Settings;
//   Settings.bDrawStatic = false;                 // 動かない地形は描かない
//   Settings.bUseRegion  = true;                  // カメラの近くだけ描く
//   Settings.RegionCenter = CameraPosition;
//   Settings.RegionRadius = 30.0f;
//=============================================================================
struct FPhysicsDebugDrawSettings
{
	// 描く剛体の種類。センサー（トリガー）は、種類に関わらず bDrawSensors で決まる
	bool bDrawStatic    = true;
	bool bDrawKinematic = true;
	bool bDrawDynamic   = true;
	bool bDrawSensors   = true;

	// 剛体の外接の箱（ワールド軸に平行。AABB）も描くか
	bool bDrawBounds = false;

	// 関節（つないだ 2 点の間の線・十字、蝶番とスライダーの軸）を描くか。外れた関節は描かない
	bool bDrawConstraints = true;

	// キャラクターの足元の面の法線を描くか（キャラクターの剛体の形は、bDrawKinematic に従って描く）
	bool bDrawCharacters = true;

	// 範囲を絞る。true なら、RegionCenter から RegionRadius 以内に触れている剛体・関節・キャラクターだけを描く。
	// 中心や半径が NaN・無限大、半径が 0 以下のときは、何も描かない
	bool     bUseRegion   = false;
	FVector3 RegionCenter = FVector3::Zero;
	float32  RegionRadius = 50.0f;

	// 円や球を何本の線で描くか（8..128 に収める）
	int32 CircleSegments = 24;

	// 三角形メッシュ・凸包など、形が細かいものを 1 つの剛体で描く三角形の上限（0 以下で描かない）
	int32 MaxMeshTriangles = 20000;

	// 色
	FColor32 StaticColor     = FColor32( 140, 140, 140 );  // 動かない剛体
	FColor32 KinematicColor  = FColor32( 80, 160, 255 );   // キネマティック
	FColor32 DynamicColor    = FColor32( 80, 220, 80 );    // 動いている動的な剛体
	FColor32 SleepingColor   = FColor32( 40, 110, 40 );    // 眠っている動的な剛体
	FColor32 SensorColor     = FColor32( 255, 220, 60 );   // センサー（トリガー）
	FColor32 CharacterColor  = FColor32( 255, 80, 200 );   // キャラクターの剛体と、足元の法線
	FColor32 ConstraintColor = FColor32( 255, 130, 40 );   // 効いている関節
	FColor32 DisabledColor   = FColor32( 90, 90, 90 );     // 無効な関節
	FColor32 BoundsColor     = FColor32( 200, 200, 200 );  // 外接の箱
};
