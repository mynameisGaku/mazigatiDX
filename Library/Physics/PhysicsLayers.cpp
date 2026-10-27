#include "PhysicsSystem.h"

#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/PhysicsSystem.h>

#pragma region CollisionLayers

bool CPhysicsSystem::SetLayerCollision( uint32 LayerA, uint32 LayerB, bool bCollide )
{
	if ( !NCollisionLayer::IsValid( LayerA ) || !NCollisionLayer::IsValid( LayerB ) ) return false;

	const uint32 Row = bCollide ? ( LayerCollisionMasks[LayerA] | NCollisionLayer::ToMask( LayerB ) ) : ( LayerCollisionMasks[LayerA] & ~NCollisionLayer::ToMask( LayerB ) );
	return SetLayerCollisionMask( LayerA, Row );
}

bool CPhysicsSystem::GetLayerCollision( uint32 LayerA, uint32 LayerB ) const
{
	if ( !NCollisionLayer::IsValid( LayerA ) || !NCollisionLayer::IsValid( LayerB ) ) return false;
	return ( LayerCollisionMasks[LayerA] & NCollisionLayer::ToMask( LayerB ) ) != 0;
}

bool CPhysicsSystem::SetLayerCollisionMask( uint32 Layer, uint32 CollidesWithMask )
{
	if ( !NCollisionLayer::IsValid( Layer ) ) return false;

	// 表は対称なので、Layer の行と、他のレイヤーの行にある Layer のビットを揃えて変える
	const uint32 Changed = LayerCollisionMasks[Layer] ^ CollidesWithMask;
	LayerCollisionMasks[Layer] = CollidesWithMask;
	for ( uint32 Other = 0; Other < NCollisionLayer::MaxCount; ++Other )
	{
		if ( Other == Layer ) continue;

		if ( ( CollidesWithMask & NCollisionLayer::ToMask( Other ) ) != 0 ) LayerCollisionMasks[Other] |= NCollisionLayer::ToMask( Layer );
		else LayerCollisionMasks[Other] &= ~NCollisionLayer::ToMask( Layer );
	}

	if ( Changed != 0 ) WakeLayerBodies( Changed | NCollisionLayer::ToMask( Layer ) );
	return true;
}

uint32 CPhysicsSystem::GetLayerCollisionMask( uint32 Layer ) const
{
	return NCollisionLayer::IsValid( Layer ) ? LayerCollisionMasks[Layer] : 0u;
}

void CPhysicsSystem::ResetLayerCollision()
{
	uint32 Changed = 0;
	for ( uint32 Layer = 0; Layer < NCollisionLayer::MaxCount; ++Layer )
	{
		if ( LayerCollisionMasks[Layer] == NCollisionLayer::AllMask ) continue;

		Changed |= NCollisionLayer::ToMask( Layer );  // 表は対称なので、ぶつからない組の両側がここに入る
		LayerCollisionMasks[Layer] = NCollisionLayer::AllMask;
	}

	if ( Changed != 0 ) WakeLayerBodies( Changed );
}

void CPhysicsSystem::WakeLayerBodies( uint32 LayerMask )
{
	if ( !bInitialized ) return;

	// 眠っている剛体は衝突を調べ直さない。表が変わったことで、床をすり抜けるのに眠ったまま浮く、などが起きないよう起こす
	JPH::BodyInterface& BodyInterface = GetBodyInterface();
	for ( auto& [Key, Body] : Bodies )
	{
		if ( Body->GetBodyType() == EBodyType::Static ) continue;
		if ( ( LayerMask & NCollisionLayer::ToMask( Body->GetCollisionLayer() ) ) != 0 ) BodyInterface.ActivateBody( Body->GetBodyID() );
	}
}

#pragma endregion
