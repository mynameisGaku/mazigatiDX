// SPDX-License-Identifier: NOASSERTION
#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// NCollisionLayer
//   衝突レイヤー。剛体を 0..31 のどれか 1 つに入れ、どのレイヤー同士がぶつかるかを ICollisionLayers で決める。
//   何も設定しなければ、すべての剛体が Default に入り、すべてのレイヤーがぶつかる（これまでの動き）。
//   物理エンジン（Jolt）の型を使わないので、これを使う側は Jolt に依存しない。
//
//   constexpr uint32 Player = 1, Enemy = 2, PlayerBullet = 3;
//   Layers->SetLayerCollision( PlayerBullet, Player, false );   // 自分の弾は自分に当たらない
//   Settings.CollisionLayer = Player;                           // FRigidBodySettings
//   Filter.LayerMask = NCollisionLayer::ToMask( Enemy );        // 空間クエリで敵だけを調べる
//=============================================================================
namespace NCollisionLayer
{
	inline constexpr uint32 MaxCount = 32;           // レイヤーの数。使える番号は 0..MaxCount-1
	inline constexpr uint32 Default  = 0;            // 何も指定しなかった剛体が入るレイヤー
	inline constexpr uint32 AllMask  = 0xFFFFFFFFu;  // すべてのレイヤーのビット

	FORCEINLINE constexpr bool IsValid( uint32 Layer )
	{
		return Layer < MaxCount;
	}

	// レイヤー 1 つのビット（空間クエリの LayerMask に使う）。範囲外は 0
	FORCEINLINE constexpr uint32 ToMask( uint32 Layer )
	{
		return Layer < MaxCount ? ( 1u << Layer ) : 0u;
	}
}
