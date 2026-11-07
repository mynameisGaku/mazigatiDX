#include "PhysicsSubsystem.h"

#include "Library/Framework/Scene/Scene.h"
#include "Library/Physics/DebugDraw/PhysicsDebugView.h"

REGISTER_SUBSYSTEM( CPhysicsSubsystem )

bool CPhysicsSubsystem::ShouldCreateSubsystem( CScene& Scene ) const
{
	return dynamic_cast<const IPhysicsScene*>( &Scene ) != nullptr;
}

void CPhysicsSubsystem::Initialize( FSubsystemCollection& Collection )
{
	const IPhysicsScene* PhysicsScene = dynamic_cast<const IPhysicsScene*>( GetScene() );
	const TResult<void> InitResult = Init( PhysicsScene->GetPhysicsSettings() );
	if ( InitResult.IsErr() )
	{
		LOG( LogPhysics, Error, "%s", *InitResult.UnwrapErr() );
	}
	// デバッグビュー（F4 / view Physics on）: 登録して、このシーンの描画に、物理の形の線を重ねる（オフのときは、何も描かない）
	NPhysicsDebugView::Register();
	GetScene()->AddSceneDraw( NPhysicsDebugView::SceneDrawName, [this]( FDrawContext& Draw ) { NPhysicsDebugView::Draw( *this, Draw ); } );
}

void CPhysicsSubsystem::Deinitialize()
{
	if ( GetScene() != nullptr )
	{
		GetScene()->RemoveSceneDraw( NPhysicsDebugView::SceneDrawName );
	}
	Shutdown();
}

#pragma region VirtualDefaults

// 仮想関数は vtable 経由で呼ばれインライン展開できないので、ヘッダーではなくここで定義する

FPhysicsSettings IPhysicsScene::GetPhysicsSettings() const
{
	return FPhysicsSettings();
}

void CPhysicsSubsystem::Tick( float32 DeltaTime )
{
	Update( DeltaTime );
}

bool CPhysicsSubsystem::IsTickable() const
{
	return IsInitialized();
}

ETickGroup CPhysicsSubsystem::GetTickGroup() const
{
	return ETickGroup::PostUpdate;
}

#pragma endregion
