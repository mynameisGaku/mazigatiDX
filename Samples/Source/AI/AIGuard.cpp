// SPDX-License-Identifier: MIT
#include "AIGuard.h"

#include "Library/Framework/AI/BehaviorTreeComponent.h"

CAIGuard::CAIGuard( const FVector2& InPosition )
	: CAIActor( "Guard", InPosition, FColor32( 235, 120, 90 ) )
{
	Hp = 8;
	Facing = -1.0f;
}

void CAIGuard::OnSpawn()
{
	Brain = AddComponent<CBehaviorTreeComponent>( "AI/Guard.gxbt" );
	Brain->SetAnimationControl( this );
	// すぐ読み込む（ブラックボードを、最初のフレームから使えるように）
	Brain->Load();
}

void CAIGuard::SetTarget( uint64 TargetObjectId )
{
	if ( Brain != nullptr && Brain->GetBlackboard().IsValid() )
	{
		CBlackboard& Blackboard = *Brain->GetBlackboard();
		Blackboard.SetObjectId( Blackboard.FindKey( "Target" ), TargetObjectId );
	}
}

void CAIGuard::OnDamaged()
{
	if ( Brain != nullptr && Brain->GetBlackboard().IsValid() )
	{
		CBlackboard& Blackboard = *Brain->GetBlackboard();
		Blackboard.SetBool( Blackboard.FindKey( "bHurt" ), true );
	}
}
