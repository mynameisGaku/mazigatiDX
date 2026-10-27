// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Physics/PhysicsLayerTypes.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// ICollisionLayers
//   衝突レイヤー同士がぶつかるかの表。ぶつからない組は、衝突判定も衝突イベントも起きない（すり抜ける）。
//   実装: CPhysicsSystem / CPhysicsSubsystem（Jolt）。ゲームの側はこのインターフェースだけを知ればよい。
//
//   ICollisionLayers* Layers = GetScene()->GetSubsystem<ICollisionLayers>();   // 物理を使うシーンにだけある（無ければ nullptr）
//   Layers->SetLayerCollision( PlayerBullet, Player, false );
//
//   - 表は対称（A と B の順は問わない）。初めはすべての組がぶつかる。物理の Init / Shutdown では変わらない
//   - 静的な剛体同士は、表に関わらず衝突しない
//   - 範囲外のレイヤー（NCollisionLayer::MaxCount 以上）は、設定しても何も変わらず false を返し、Get は false になる
//   - 表を変えると、変わる組に関わるレイヤーの動く剛体を起こす（眠ったまま浮くのを避けるため）
//   - 物理の Update と同時には呼ばない
//=============================================================================
class ICollisionLayers
{
public:
	virtual ~ICollisionLayers() = default;

	// 2 つのレイヤーがぶつかるかを決める（同じレイヤー同士も LayerA == LayerB で指定できる）。範囲外なら何もせず false
	virtual bool SetLayerCollision( uint32 LayerA, uint32 LayerB, bool bCollide ) = 0;
	virtual bool GetLayerCollision( uint32 LayerA, uint32 LayerB ) const = 0;

	// Layer がぶつかる相手を、まとめて決める（相手のレイヤーのビット。NCollisionLayer::ToMask を足し合わせる）。
	// 他のレイヤーの側の表も揃えて変える。範囲外の Layer なら何もせず false
	virtual bool SetLayerCollisionMask( uint32 Layer, uint32 CollidesWithMask ) = 0;
	virtual uint32 GetLayerCollisionMask( uint32 Layer ) const = 0;  // 範囲外は 0

	// すべての組をぶつかる状態に戻す
	virtual void ResetLayerCollision() = 0;
};
