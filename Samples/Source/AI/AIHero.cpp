// SPDX-License-Identifier: MIT
#include "AIHero.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/AI/StateMachineComponent.h"
#include "Library/Framework/Engine/Engine.h"

CAIHero::CAIHero( const FVector2& InPosition )
	: CAIActor( "Hero", InPosition, FColor32( 90, 170, 255 ) )
{
	Hp = 10;
}

void CAIHero::OnSpawn()
{
	Machine = AddComponent<CStateMachineComponent>( "AI/Hero.gxstate" );
	Machine->SetAnimationControl( this );
	// すぐ読み込む（ブラックボードを、最初のフレームから使えるように）
	Machine->Load();
}

void CAIHero::Tick( float32 DeltaTime )
{
	// 入力センサー: キーを、ブラックボードへ書くだけ。状態の判断は、ステートマシンが行う
	CBlackboard* Blackboard = Machine != nullptr ? Machine->GetBlackboard().Get() : nullptr;
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Blackboard != nullptr && Input != nullptr )
	{
		int32 Axis = 0;
		Axis += ( Input->IsKeyDown( EKey::D ) || Input->IsKeyDown( EKey::Right ) ) ? 1 : 0;
		Axis -= ( Input->IsKeyDown( EKey::A ) || Input->IsKeyDown( EKey::Left ) ) ? 1 : 0;
		Blackboard->SetInt( Blackboard->FindKey( "MoveAxis" ), Axis );
		if ( Input->IsKeyPressed( EKey::Space ) || Input->IsKeyPressed( EKey::J ) )
		{
			Blackboard->SetBool( Blackboard->FindKey( "AttackPressed" ), true );
		}
	}
	CAIActor::Tick( DeltaTime );
}

void CAIHero::OnDamaged()
{
	if ( Machine != nullptr )
	{
		Machine->SendEvent( "Damaged" );
	}
}
