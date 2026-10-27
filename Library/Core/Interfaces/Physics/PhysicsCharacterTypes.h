// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Physics/PhysicsLayerTypes.h"
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/MathConstants.h"

//=============================================================================
// ECharacterGroundState
//   キャラクターが何の上にいるか。
//=============================================================================
enum class ECharacterGroundState : uint8
{
	OnGround,       // 地面の上にいて、自由に歩ける
	OnSteepGround,  // 登れない急な坂の上にいる（上へは進めず、滑り落ちる）
	NotSupported,   // 何かに触れているが、支えられていない（落ちる）。壁際など
	InAir,          // 何にも触れていない（空中）
};

//=============================================================================
// FCharacterSettings
//   操作するキャラクター（プレイヤーや敵）の体と動き方。物理エンジン（Jolt）の型を使わない。
//   体は縦向き（Y 軸）のカプセル 1 つで、位置は「足元の中心」。長さは m、角度はラジアン。
//
//   FCharacterSettings Settings;
//   Settings.Height = 1.7f;
//   Settings.CollisionLayer = Player;
//   FPhysicsCharacter* Player = Characters->CreateCharacter( FVector3( 0.0f, 0.0f, 0.0f ), Settings );
//=============================================================================
struct FCharacterSettings
{
	// カプセルの半径
	float32 Radius = 0.3f;

	// 足元から頭の先までの高さ（上下の半球を含む）。Radius の 2 倍より大きいこと
	float32 Height = 1.8f;

	// 登れる坂の最大の角度。これより急な坂には登れず、滑り落ちる。0 より大きく PI/2 より小さい
	float32 MaxSlopeAngle = 50.0f * NMath::DEG2RAD;

	// 登れる段差の高さ。段差にぶつかったとき、この高さまでなら乗り越える。0 で段差を登らない
	float32 StepHeight = 0.4f;

	// 下り坂や段差を降りるとき、足が床から離れずに吸いつく距離。0 で吸いつかない（降りる間は空中になる）
	float32 StickToFloorDistance = 0.5f;

	// 体重（kg）。立っている動く剛体を、重さの分だけ押し下げる。0 以上
	float32 Mass = 70.0f;

	// 動く剛体を押す力の上限（N）。0 で押さない。0 以上
	float32 MaxPushForce = 100.0f;

	// 世界の重力にかける倍率。0 で無重力、負で逆向き
	float32 GravityFactor = 1.0f;

	// 衝突レイヤー（NCollisionLayer, 0..31）。どのレイヤーとぶつかるかは ICollisionLayers で決める。範囲外なら作れない
	uint32 CollisionLayer = NCollisionLayer::Default;

	// 物理のワールドに、キャラクターの形をした剛体（動かさない運動学の剛体）を持たせるか。
	// 持たせると、レイ・重なりのクエリや接触の通知（トリガーなど）、動く剛体との押し合いにキャラクターが現れる
	bool bCreateBody = true;
};
