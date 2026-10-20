#pragma once
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Framework/Subsystem/SceneSubsystem.h"
#include "Library/Physics/PhysicsSystem.h"

//=============================================================================
// IPhysicsScene
//   シーンがこれを継承すると、そのシーンにだけ物理ワールド (CPhysicsSubsystem) が作られる。
//   物理を使わないシーンでスレッドプールなどを作らないための opt-in。
//
//   class CGameScene : public CScene, public IPhysicsScene
//   {
//       FPhysicsSettings GetPhysicsSettings() const override { FPhysicsSettings S; S.Gravity = ...; return S; }
//   };
//=============================================================================
class IPhysicsScene
{
public:
	virtual ~IPhysicsScene() = default;

	virtual FPhysicsSettings GetPhysicsSettings() const;
};

//=============================================================================
// CPhysicsSubsystem
//   シーンごとの物理ワールド。オブジェクトの Tick の後 (PostUpdate) に物理を進める。
//
//   CPhysicsSubsystem* Physics = GetScene()->GetSubsystem<CPhysicsSubsystem>();
//   FRigidBody* Body = Physics->CreateBody( FCollisionShape::Box( { 1, 1, 1 } ), EBodyType::Dynamic );
//=============================================================================
class CPhysicsSubsystem : public CSceneSubsystem, public ITickable, public CPhysicsSystem
{
public:
	//-------------------------------------------------------------------------
	// CSceneSubsystem
	//-------------------------------------------------------------------------
	bool ShouldCreateSubsystem( CScene& Scene ) const override;
	void Initialize( FSubsystemCollection& Collection ) override;
	void Deinitialize() override;

	//-------------------------------------------------------------------------
	// ITickable
	//-------------------------------------------------------------------------
	void Tick( float32 DeltaTime ) override;
	bool IsTickable() const override;
	ETickGroup GetTickGroup() const override;
};
