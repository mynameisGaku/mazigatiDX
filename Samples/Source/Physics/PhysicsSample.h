// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Physics/PhysicsSubsystem.h"
#include "Source/Catalog/SampleScene.h"

class CSensorZone;

//=============================================================================
// CPhysicsSample
//   物理（Jolt）の見本。横から見た 2D の画面で、次のことを見せる。
//     - 剛体: 球と箱が落ちて、積もる（CPhysicsBody）。動かない床・壁・足場（CStaticBlock）
//     - 関節: ロープでぶら下がった物体が、振り子のように揺れる（CRopeJoint）
//     - センサー: 緑の枠の中に入った / 出た物を、通知で数える（CSensorZone）
//     - 空間クエリ: マウスが指している物を、レイキャスト（IPhysicsQuery::RayCast）で見つけて、白く縁取る。左クリックで、その物を蹴る
//   Space（または右クリック）で、マウスの位置に物を足す。F4 で、物理の形の線（デバッグビュー）を出せる。
//   物理を使うシーンは、IPhysicsScene も継承する（それだけで、シーンに物理のワールドができる）。
//=============================================================================
class CPhysicsSample : public CSampleScene, public IPhysicsScene
{
protected:
	void OnEnter() override;
	void OnExit() override;
	void OnSampleTick( float32 DeltaTime ) override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;

private:
	// マウスの位置に、物を 1 つ置く（球と箱を、交互に）
	void SpawnAt( const FVector3& Position );

	// マウスが指している物を調べて、白く縁取る。左クリックされたら、蹴る
	void PickWithMouse();

	TSharedPtr<CSensorZone> Zone;
	int32 NumSpawned = 0;
	bool bHasPicked = false;
};
