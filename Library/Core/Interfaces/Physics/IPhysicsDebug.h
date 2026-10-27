// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Physics/IPhysicsDebugDrawer.h"
#include "Library/Core/Interfaces/Physics/PhysicsDebugTypes.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// IPhysicsDebug
//   物理の世界（剛体の形・外接の箱・関節・キャラクター）を、線にして IPhysicsDebugDrawer へ渡す窓口。読むだけで、何も変えない。
//   実装: CPhysicsSystem / CPhysicsSubsystem（Jolt）。ゲームの側はこのインターフェースだけを知ればよく、物理エンジンの型を include しない。
//
//   IPhysicsDebug* Debug = GetScene()->GetSubsystem<IPhysicsDebug>();     // 物理を使うシーンにだけある（無ければ nullptr）
//   FPhysicsDebugDrawContext Drawer( Draw );                              // Library/Physics/PhysicsDebugDrawContext.h（FDrawContext へ線を記録する）
//   Debug->DrawDebug( Drawer, FPhysicsDebugDrawSettings() );
//
//   - 剛体の色は種類で変わる（動かない・キネマティック・動いている動的・眠っている動的・センサー・キャラクター）。
//     箱・球・カプセルは輪郭、凸包・三角形メッシュ・そのほかは三角形の辺
//   - 物理の Update と同時には呼ばない（呼ぶのは、同じスレッドの Update の合間）
//   - 世界が初期化されていないときは何も描かず、0 を返す
//=============================================================================
class IPhysicsDebug
{
public:
	virtual ~IPhysicsDebug() = default;

	// 描いた線の本数を返す
	virtual int32 DrawDebug( IPhysicsDebugDrawer& Drawer, const FPhysicsDebugDrawSettings& Settings = FPhysicsDebugDrawSettings() ) const = 0;
};
