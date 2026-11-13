#include "AdventurePlayer.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/AI/Blackboard/Blackboard.h"
#include "Library/Framework/AI/StateMachineComponent.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/GameFeel/HitStop.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"
#include "Source/Adventure/AdventureCollision.h"
#include "Source/Adventure/AdventureEnemy.h"
#include "Source/Adventure/AdventureState.h"

namespace
{
	// 手応え: 画面の揺れ（画素）を、ワールドの長さに直す倍率と、やられたときに時間を止める長さ（秒）
	constexpr float32 ShakeWorldScale = 0.012f;
	constexpr float32 DefeatedHitStop = 0.3f;
	constexpr float32 Radius = 0.4f;
	constexpr float32 RunSpeed = 5.5f;
	constexpr float32 RollSpeed = 9.0f;
	constexpr float32 CameraTurnSpeed = 1.8f;

	// 斬る: 届く距離（敵の半径を足す）、前から、この角度の内側（片側。ラジアン）、与える傷、斬ってから当たるまでの秒
	constexpr float32 StrikeReach = 1.9f;
	constexpr float32 StrikeHalfAngle = 1.25f;
	constexpr int32 StrikeDamage = 14;
	constexpr float32 StrikeDelay = 0.12f;

	constexpr float32 HurtInvulnerable = 0.8f;
	constexpr int32 PotionHeal = 40;
	constexpr float32 InteractRange = 1.9f;
}

CAdventurePlayer::CAdventurePlayer( const TSharedPtr<FAdventureState>& InState )
	: State( InState )
{}

void CAdventurePlayer::OnSpawn()
{
	// 状態のステートマシン。すぐ読み込む（ブラックボードを、最初のフレームから使えるように）
	Machine = AddComponent<CStateMachineComponent>( "Adventure/Player.gxstate" );
	Machine->Load();
}

void CAdventurePlayer::Revive()
{
	InvulnerableTime = 1.0f;
	FlashTime = 0.0f;
	if ( Machine != nullptr )
	{
		Machine->SendEvent( "Revived" );
	}
}

void CAdventurePlayer::TeleportTo( const FVector3& InPosition )
{
	Position = InPosition;
	State->PlayerPosition = Position;
	UpdateCameraTarget();
}

//-----------------------------------------------------------------------------
// センサー（Update の段階）: キー入力 → ブラックボード。ステートマシンは、次のフレームの最初に、これを読む
//-----------------------------------------------------------------------------
void CAdventurePlayer::Tick( float32 DeltaTime )
{
	if ( DeltaTime > 1.0f / 30.0f )
	{
		DeltaTime = 1.0f / 30.0f;
	}
	InvulnerableTime = NMath::Max( 0.0f, InvulnerableTime - DeltaTime );
	FlashTime = NMath::Max( 0.0f, FlashTime - DeltaTime );
	State->PlayerPosition = Position;
	State->bPlayerRolling = Machine != nullptr && Machine->GetStateMachine() != nullptr && Machine->GetStateMachine()->IsInState( "Roll" );
	SenseInput( DeltaTime );
	UpdateCameraTarget();
}

void CAdventurePlayer::SenseInput( float32 DeltaTime )
{
	const IInput* Input = GetAppSubsystem<IInput>();
	CBlackboard* Blackboard = Machine != nullptr ? Machine->GetBlackboard().Get() : nullptr;
	MoveDirection = FVector3::Zero;
	if ( Input == nullptr || Blackboard == nullptr || State->bPlayerDead || State->bWon )
	{
		if ( Blackboard != nullptr )
		{
			Blackboard->SetFloat( Blackboard->FindKey( "MoveAmount" ), 0.0f );
		}
		return;
	}
	// カメラ
	const float32 Turn = ( Input->IsKeyDown( EKey::Right ) ? 1.0f : 0.0f ) - ( Input->IsKeyDown( EKey::Left ) ? 1.0f : 0.0f );
	State->CameraYaw += Turn * CameraTurnSpeed * DeltaTime;

	// 動く向き（カメラから見た向きを、世界の向きにする）
	float32 X = ( Input->IsKeyDown( EKey::D ) ? 1.0f : 0.0f ) - ( Input->IsKeyDown( EKey::A ) ? 1.0f : 0.0f );
	float32 Z = ( Input->IsKeyDown( EKey::W ) ? 1.0f : 0.0f ) - ( Input->IsKeyDown( EKey::S ) ? 1.0f : 0.0f );
	const float32 Yaw = State->CameraYaw;
	const FVector3 Forward( NMath::Sin( Yaw ), 0.0f, NMath::Cos( Yaw ) );
	const FVector3 Right( NMath::Cos( Yaw ), 0.0f, -NMath::Sin( Yaw ) );
	FVector3 Wanted = Right * X + Forward * Z;
	if ( Wanted.LengthSquared() > 1.0f )
	{
		Wanted.Normalize();
	}
	MoveDirection = Wanted;
	Blackboard->SetFloat( Blackboard->FindKey( "MoveAmount" ), Wanted.Length() );

	// 斬る・転がる: トリガー（1 フレームだけ真）
	if ( Input->IsKeyPressed( EKey::Space ) || Input->IsKeyPressed( EKey::J ) )
	{
		Blackboard->SetBool( Blackboard->FindKey( "AttackPressed" ), true );
	}
	if ( Input->IsKeyPressed( EKey::LeftShift ) || Input->IsKeyPressed( EKey::K ) )
	{
		Blackboard->SetBool( Blackboard->FindKey( "RollPressed" ), true );
	}
	// 回復薬
	if ( Input->IsKeyPressed( EKey::F ) )
	{
		if ( State->Potions > 0 && State->Hp < FAdventureState::MaxHp )
		{
			--State->Potions;
			State->Hp = NMath::Min( FAdventureState::MaxHp, State->Hp + PotionHeal );
			State->Say( FString::Printf( "Drank a potion. HP %d.", State->Hp ) );
			State->PlaySound( State->PickupSound, 1.3f, 0.3f );
		}
		else if ( State->Potions <= 0 )
		{
			State->Say( "No potions left." );
		}
	}
	UpdateInteraction( Input->IsKeyPressed( EKey::E ) );

	// 出口に着いた
	const float32 DX = Position.x - State->ExitPosition.x;
	const float32 DZ = Position.z - State->ExitPosition.z;
	if ( !State->bWon && DX * DX + DZ * DZ < 1.6f * 1.6f )
	{
		State->bWon = true;
		State->Say( "You escaped the ruins!" );
	}
}

void CAdventurePlayer::UpdateInteraction( bool bInteractPressed )
{
	State->Prompt = FString();
	IAdventureInteractable* Nearest = nullptr;
	float32 NearestDistance = InteractRange * InteractRange;
	for ( const TSharedPtr<CAdventureActor>& Actor : GetScene()->FindObjects<CAdventureActor>() )
	{
		IAdventureInteractable* Interactable = dynamic_cast<IAdventureInteractable*>( Actor.Get() );
		if ( Interactable == nullptr || !Interactable->CanInteract() )
		{
			continue;
		}
		const FVector3 At = Interactable->GetInteractPosition();
		const float32 DX = At.x - Position.x;
		const float32 DZ = At.z - Position.z;
		const float32 Distance = DX * DX + DZ * DZ;
		if ( Distance < NearestDistance )
		{
			NearestDistance = Distance;
			Nearest = Interactable;
		}
	}
	if ( Nearest == nullptr )
	{
		return;
	}
	State->Prompt = FString::Printf( "[E] %s", Nearest->GetPrompt() );
	if ( bInteractPressed )
	{
		Nearest->Interact();
	}
}

void CAdventurePlayer::UpdateCameraTarget()
{
	// カメラは、主人公の斜め後ろの上から、見下ろす
	const float32 Yaw = State->CameraYaw;
	const FVector3 Forward( NMath::Sin( Yaw ), 0.0f, NMath::Cos( Yaw ) );
	// 画面の揺れ（画素）を、ワールドの長さに直して、カメラの位置と注視点の両方に足す（向きは変えずに、画面ごと揺らす）
	const FVector2 ShakeOffset = State->Shake.GetOffset();
	const FVector3 Right( NMath::Cos( Yaw ), 0.0f, -NMath::Sin( Yaw ) );
	const FVector3 Shake = Right * ( ShakeOffset.x * ShakeWorldScale ) + FVector3( 0.0f, ShakeOffset.y * ShakeWorldScale, 0.0f );
	const FVector3 Look = Position + FVector3( 0.0f, 0.8f, 0.0f ) + Shake;
	FCamera Camera = FCamera::MakePerspective( Look - Forward * 5.0f + FVector3( 0.0f, 11.0f, 0.0f ), Look, NMath::PI / 3.2f );
	Camera.Near = 0.3f;
	Camera.Far = 200.0f;
	State->Camera = Camera;
}

//-----------------------------------------------------------------------------
// ステートの動き
//-----------------------------------------------------------------------------
void CAdventurePlayer::Move( const FVector3& Delta )
{
	Position += Delta;
	NAdventureCollision::Resolve( *State, Position, Radius );
}

void CAdventurePlayer::StepIdle( float32 DeltaTime )
{
}

void CAdventurePlayer::StepRun( float32 DeltaTime )
{
	if ( MoveDirection.LengthSquared() < 0.0001f )
	{
		return;
	}
	FacingYaw = NMath::Atan2( MoveDirection.x, MoveDirection.z );
	Move( MoveDirection * ( RunSpeed * DeltaTime ) );
}

void CAdventurePlayer::BeginAttack()
{
	AttackTime = 0.0f;
	bStruck = false;
	// 近くに敵がいれば、その方へ向いて斬る（狙いの補助）
	float32 Best = 3.0f * 3.0f;
	for ( const TSharedPtr<CAdventureEnemy>& Enemy : GetScene()->FindObjects<CAdventureEnemy>() )
	{
		const FVector3 To = Enemy->GetPosition() - Position;
		const float32 Distance = To.x * To.x + To.z * To.z;
		if ( Enemy->IsAlive() && Distance < Best )
		{
			Best = Distance;
			FacingYaw = NMath::Atan2( To.x, To.z );
		}
	}
	State->PlaySound( State->HitSound, 1.6f, 0.2f );
}

void CAdventurePlayer::StepAttack( float32 DeltaTime )
{
	AttackTime += DeltaTime;
	if ( !bStruck && AttackTime >= StrikeDelay )
	{
		bStruck = true;
		StrikeEnemies();
	}
}

void CAdventurePlayer::StrikeEnemies()
{
	const FVector3 Facing( NMath::Sin( FacingYaw ), 0.0f, NMath::Cos( FacingYaw ) );
	int32 NumHit = 0;
	int32 NumDefeated = 0;
	for ( const TSharedPtr<CAdventureEnemy>& Enemy : GetScene()->FindObjects<CAdventureEnemy>() )
	{
		if ( !Enemy->IsAlive() )
		{
			continue;
		}
		FVector3 To = Enemy->GetPosition() - Position;
		To.y = 0.0f;
		const float32 Distance = To.Length();
		if ( Distance > StrikeReach + Enemy->GetRadius() )
		{
			continue;
		}
		// 前から、この角度の内側にいるか（すぐそばは、向きに関係なく当たる）
		const float32 Cosine = Distance > 0.001f ? FVector3::Dot( To / Distance, Facing ) : 1.0f;
		if ( Distance > 0.8f && Cosine < NMath::Cos( StrikeHalfAngle ) )
		{
			continue;
		}
		Enemy->ApplyDamage( StrikeDamage, Position );
		++NumHit;
		NumDefeated += Enemy->IsDefeated() ? 1 : 0;
		State->PlaySound( State->HurtSound, 1.2f, 0.25f );
		State->PlayEffect( "Effects/sparks.gxeffect", Enemy->GetPosition() + FVector3( 0.0f, 0.8f, 0.0f ), 0.8f );
	}
	if ( NumHit > 0 )
	{
		// 手応え: 当たった瞬間に、時間を一瞬止める（攻撃力から長さを決め、とどめは 2 倍）。画面も揺らす（とどめは、横にも揺れる、中くらいの揺れ）。
		// 斬った側は、ごく小さく震える（斬られた側は、大きく震える）
		GetScene()->StartHitStop( FHitStop::SecondsFromPower( static_cast<float32>( StrikeDamage ), NumDefeated > 0 ? 2.0f : 1.0f ) );
		State->Shake.Start( NumDefeated > 0 ? EScreenShakeStrength::Medium : EScreenShakeStrength::Small );
		ShiverAmount = 0.03f;
	}
}

void CAdventurePlayer::BeginRoll()
{
	RollDirection = MoveDirection.LengthSquared() > 0.01f ? MoveDirection.Normalized() : FVector3( NMath::Sin( FacingYaw ), 0.0f, NMath::Cos( FacingYaw ) );
	FacingYaw = NMath::Atan2( RollDirection.x, RollDirection.z );
	State->bPlayerRolling = true;
}

void CAdventurePlayer::StepRoll( float32 DeltaTime )
{
	Move( RollDirection * ( RollSpeed * DeltaTime ) );
}

void CAdventurePlayer::EndRoll()
{
	State->bPlayerRolling = false;
}

void CAdventurePlayer::BeginHurt()
{
	FlashTime = 0.4f;
}

void CAdventurePlayer::StepHurt( float32 DeltaTime )
{
	// のけぞる: 攻撃された向きと反対へ、だんだん遅く滑る
	Move( KnockDirection * ( 3.5f * DeltaTime ) );
}

void CAdventurePlayer::BeginDead()
{
	State->bPlayerDead = true;
	State->Say( "You died. Press R to start again, or F9 to load." );
}

void CAdventurePlayer::ApplyDamage( int32 Damage, const FVector3& From )
{
	if ( State->bPlayerDead || State->bPlayerRolling || InvulnerableTime > 0.0f || State->bWon )
	{
		return;
	}
	State->Hp = NMath::Max( 0, State->Hp - Damage );
	InvulnerableTime = HurtInvulnerable;
	FVector3 Away( Position.x - From.x, 0.0f, Position.z - From.z );
	KnockDirection = Away.LengthSquared() > 0.0001f ? Away.Normalized() : FVector3( 0.0f, 0.0f, -1.0f );
	State->PlaySound( State->HurtSound, 0.7f, 0.4f );
	State->PlayEffect( "Effects/sparks.gxeffect", Position + FVector3( 0.0f, 0.9f, 0.0f ), 0.7f );
	// 手応え: 受けたときも、時間を止めて、画面を揺らす（やられたときは、長く止めて、大きく揺らす）
	const bool bDefeated = State->Hp <= 0;
	GetScene()->StartHitStop( bDefeated ? DefeatedHitStop : FHitStop::SecondsFromPower( static_cast<float32>( Damage ) ) );
	State->Shake.Start( bDefeated ? EScreenShakeStrength::Large : EScreenShakeStrength::Medium );
	ShiverAmount = 0.1f;
	if ( Machine != nullptr )
	{
		Machine->SendEvent( State->Hp <= 0 ? "Died" : "Damaged" );
	}
}

//-----------------------------------------------------------------------------
// 描く
//-----------------------------------------------------------------------------
void CAdventurePlayer::Record3D( FDrawContext& CommandList )
{
	const CStateMachine* StateMachine = Machine != nullptr ? Machine->GetStateMachine() : nullptr;
	const bool bRolling = StateMachine != nullptr && StateMachine->IsInState( "Roll" );
	const bool bAttacking = StateMachine != nullptr && StateMachine->IsInState( "Attack" );
	const bool bDead = StateMachine != nullptr && StateMachine->IsInState( "Dead" );
	const FVector3 Facing( NMath::Sin( FacingYaw ), 0.0f, NMath::Cos( FacingYaw ) );
	const FVector3 Right( Facing.z, 0.0f, -Facing.x );
	// ヒットストップの間は、見た目だけ、横に震える（当たり判定の位置は、動かさない）
	const FVector3 Base = Position + State->GetShiverOffset( ShiverAmount );
	// 傷を受けた直後は、無敵の間、点滅する
	if ( InvulnerableTime > 0.0f && !bDead && ( static_cast<int32>( InvulnerableTime * 16.0f ) % 2 ) == 0 )
	{
		return;
	}
	CommandList.Cone3D( Base + FVector3( 0.0f, 0.03f, 0.0f ), Base + FVector3( 0.0f, 0.005f, 0.0f ), 0.5f, 16, FColor32( 24, 28, 24 ) );
	const FColor32 Tunic = FlashTime > 0.0f ? FColor32( 240, 90, 80 ) : FColor32( 60, 110, 210 );
	if ( bDead )
	{
		// 倒れている（横になる）
		CommandList.Capsule3D( Base + FVector3( -0.5f, 0.3f, 0.0f ), Base + FVector3( 0.5f, 0.3f, 0.0f ), 0.3f, 8, Tunic );
		CommandList.Sphere3D( Base + FVector3( 0.75f, 0.3f, 0.0f ), 0.22f, 10, FColor32( 250, 208, 160 ) );
		return;
	}
	if ( bRolling )
	{
		// 丸くなって、転がる
		const float32 Spin = State->Time * 18.0f;
		CommandList.Sphere3D( Base + FVector3( 0.0f, 0.45f, 0.0f ), 0.45f, 12, Tunic );
		CommandList.Sphere3D( Base + FVector3( NMath::Cos( Spin ) * 0.3f, 0.45f + NMath::Sin( Spin ) * 0.3f, 0.0f ) + Facing * 0.2f, 0.12f, 6, FColor32( 250, 208, 160 ) );
		return;
	}
	const bool bWalking = MoveDirection.LengthSquared() > 0.01f;
	const float32 Stride = bWalking ? 0.2f * NMath::Sin( State->Time * 14.0f ) : 0.0f;
	CommandList.Sphere3D( Base + Right * 0.16f + Facing * Stride + FVector3( 0.0f, 0.14f, 0.0f ), 0.15f, 8, FColor32( 90, 60, 30 ) );
	CommandList.Sphere3D( Base - Right * 0.16f - Facing * Stride + FVector3( 0.0f, 0.14f, 0.0f ), 0.15f, 8, FColor32( 90, 60, 30 ) );
	CommandList.Capsule3D( Base + FVector3( 0.0f, 0.4f, 0.0f ), Base + FVector3( 0.0f, 0.95f, 0.0f ), 0.3f, 8, Tunic );
	const FVector3 Head = Base + FVector3( 0.0f, 1.45f, 0.0f );
	CommandList.Sphere3D( Head, 0.26f, 12, FColor32( 250, 208, 160 ) );
	CommandList.Cone3D( Head + FVector3( 0.0f, 0.38f, 0.0f ), Head + FVector3( 0.0f, 0.1f, 0.0f ), 0.28f, 10, FColor32( 120, 70, 40 ) );
	CommandList.Sphere3D( Head + Facing * 0.24f + Right * 0.1f, 0.04f, 6, FColor32::Black );
	CommandList.Sphere3D( Head + Facing * 0.24f - Right * 0.1f, 0.04f, 6, FColor32::Black );
	// 剣: ふだんは、体の横。斬るときは、前を大きく振る
	FVector3 Hilt = Base + Right * 0.4f + FVector3( 0.0f, 0.8f, 0.0f );
	FVector3 Tip = Hilt + Facing * 0.1f + FVector3( 0.0f, 0.6f, 0.0f );
	if ( bAttacking )
	{
		const float32 T = NMath::Clamp( AttackTime / 0.3f, 0.0f, 1.0f );
		const float32 Angle = NMath::Lerp( -1.2f, 1.2f, T );
		const FVector3 Swing = Right * NMath::Sin( Angle ) + Facing * NMath::Cos( Angle );
		Tip = Base + FVector3( 0.0f, 0.95f, 0.0f ) + Swing * 1.5f;
		Hilt = Base + FVector3( 0.0f, 0.95f, 0.0f ) + Swing * 0.4f;
	}
	CommandList.Capsule3D( Hilt, Tip, 0.07f, 6, FColor32( 210, 220, 235 ) );
}
