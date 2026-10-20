#pragma once
#include "Library/Framework/Scene/Scene.h"
#include "Library/Physics/PhysicsSubsystem.h"

//=============================================================================
// CGameScene
//   IPhysicsScene を継承しているので、このシーンには物理ワールドが作られる。
//   Space でボールを落とす / Esc でタイトルへ
//=============================================================================
class CGameScene : public CScene, public IPhysicsScene
{
protected:
	void OnEnter() override;
	void OnTick( float32 DeltaTime ) override;
	void OnDraw() override;

private:
	void SpawnBall();

	int32 NumSpawned = 0;
};
