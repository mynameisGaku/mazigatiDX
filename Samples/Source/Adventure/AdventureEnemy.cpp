#include "AdventureEnemy.h"

#include "Library/Framework/AI/Blackboard/Blackboard.h"
#include "Library/Framework/AI/BehaviorTreeComponent.h"
#include "Library/Framework/AI/StateMachineComponent.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"
#include "Source/Adventure/AdventureCollision.h"
#include "Source/Adventure/AdventurePickup.h"
#include "Source/Adventure/AdventurePlayer.h"
#include "Source/Adventure/AdventureState.h"

namespace
{
	// 敵の表（名前, 体力, 速さ, 見える距離, 殴れる距離, 傷, 半径）
	const FEnemyKind Kinds[] =
	{
		{ "Slime", 30, 2.2f, 8.0f, 1.5f, 10, 0.55f },
		{ "Skeleton", 50, 2.9f, 11.0f, 1.9f, 18, 0.45f },
	};

	constexpr float32 StrikeWindup = 0.45f;
	constexpr float32 StrikeTotal = 0.85f;

	float32 NextFloat( uint32& State )
	{
		State = State * 1664525u + 1013904223u;
		return static_cast<float32>( ( State >> 8 ) & 0xFFFFu ) / 65536.0f;
	}
}

CAdventureEnemy::CAdventureEnemy( const TSharedPtr<FAdventureState>& InState, const FString& InKindName, int32 InPlaceholderId )
	: State( InState )
	, PlaceholderId( InPlaceholderId )
{
	Kind = &Kinds[0];
	for ( const FEnemyKind& Candidate : Kinds )
	{
		if ( InKindName == Candidate.Name )
		{
			Kind = &Candidate;
		}
	}
	Hp = Kind->Hp;
	RandomState = static_cast<uint32>( InPlaceholderId ) * 2654435761u + 1u;
}

void CAdventureEnemy::OnSpawn()
{
	// 体の状態（ステートマシン）と、頭脳（ビヘイビアツリー）。同じブラックボードを見る（先に読んだ側が作り、あとの側が共有する）
	Machine = AddComponent<CStateMachineComponent>( "Adventure/Enemy.gxstate" );
	Machine->Load();
	Brain = AddComponent<CBehaviorTreeComponent>( "Adventure/Enemy.gxbt" );
	Brain->Load();
}

void CAdventureEnemy::EnsureHome()
{
	if ( !bHasHome )
	{
		bHasHome = true;
		Position = GetPlaceholderPosition();
		Home = Position;
		PatrolTarget = Position;
		FacingYaw = NextFloat( RandomState ) * NMath::TWO_PI;
	}
}

float32 CAdventureEnemy::GetDistanceToPlayer() const
{
	const float32 DX = State->PlayerPosition.x - Position.x;
	const float32 DZ = State->PlayerPosition.z - Position.z;
	return NMath::Sqrt( DX * DX + DZ * DZ );
}

void CAdventureEnemy::MoveBy( const FVector3& Delta )
{
	Position += Delta;
	NAdventureCollision::Resolve( *State, Position, Kind->Radius );
}

//-----------------------------------------------------------------------------
// センサー: 頭脳と体のステートマシンが見る値を、ブラックボードへ書く
//-----------------------------------------------------------------------------
void CAdventureEnemy::Tick( float32 DeltaTime )
{
	EnsureHome();
	FlashTime = NMath::Max( 0.0f, FlashTime - DeltaTime );
	CBlackboard* Blackboard = Machine != nullptr ? Machine->GetBlackboard().Get() : nullptr;
	if ( Blackboard == nullptr || Machine->GetStateMachine() == nullptr )
	{
		return;
	}
	const bool bAlive = Machine->GetStateMachine()->IsInState( "Alive" );
	const float32 Distance = GetDistanceToPlayer();
	const bool bPlayerLive = !State->bPlayerDead && !State->bWon;
	Blackboard->SetBool( Blackboard->FindKey( "Alive" ), bAlive );
	Blackboard->SetBool( Blackboard->FindKey( "CanSee" ), bPlayerLive && Distance < Kind->SightRange );
	Blackboard->SetBool( Blackboard->FindKey( "InAttackRange" ), bPlayerLive && Distance < Kind->AttackRange );
	if ( bDead )
	{
		DeadTime += DeltaTime;
	}
}

//-----------------------------------------------------------------------------
// 頭脳の Task の動き
//-----------------------------------------------------------------------------
void CAdventureEnemy::StepChase( float32 DeltaTime )
{
	const FVector3 To = State->PlayerPosition - Position;
	const float32 Distance = NMath::Sqrt( To.x * To.x + To.z * To.z );
	if ( Distance < 0.001f )
	{
		return;
	}
	FacingYaw = NMath::Atan2( To.x, To.z );
	if ( Distance > Kind->AttackRange * 0.8f )
	{
		MoveBy( FVector3( To.x / Distance, 0.0f, To.z / Distance ) * ( Kind->Speed * DeltaTime ) );
	}
}

void CAdventureEnemy::BeginPatrol()
{
	// 家から 2〜5 メートルの、ランダムな点
	const float32 Angle = NextFloat( RandomState ) * NMath::TWO_PI;
	const float32 Reach = 2.0f + NextFloat( RandomState ) * 3.0f;
	PatrolTarget = Home + FVector3( NMath::Cos( Angle ) * Reach, 0.0f, NMath::Sin( Angle ) * Reach );
}

bool CAdventureEnemy::StepPatrol( float32 DeltaTime )
{
	const FVector3 To = PatrolTarget - Position;
	const float32 Distance = NMath::Sqrt( To.x * To.x + To.z * To.z );
	if ( Distance < 0.3f )
	{
		return true;
	}
	FacingYaw = NMath::Atan2( To.x, To.z );
	const FVector3 Before = Position;
	MoveBy( FVector3( To.x / Distance, 0.0f, To.z / Distance ) * ( Kind->Speed * 0.5f * DeltaTime ) );
	// 壁に押し戻されて進めないなら、あきらめる（次の目標へ）
	const FVector3 Moved = Position - Before;
	return Moved.x * Moved.x + Moved.z * Moved.z < ( Kind->Speed * 0.5f * DeltaTime ) * ( Kind->Speed * 0.5f * DeltaTime ) * 0.04f;
}

void CAdventureEnemy::BeginStrike()
{
	StrikeTime = 0.0f;
	bStruckPlayer = false;
}

bool CAdventureEnemy::StepStrike( float32 DeltaTime )
{
	StrikeTime += DeltaTime;
	const FVector3 To = State->PlayerPosition - Position;
	if ( StrikeTime < StrikeWindup )
	{
		// 構えの間は、主人公の方を向く
		FacingYaw = NMath::Atan2( To.x, To.z );
	}
	else if ( !bStruckPlayer )
	{
		bStruckPlayer = true;
		if ( GetDistanceToPlayer() < Kind->AttackRange * 1.4f )
		{
			const TSharedPtr<CGameObject> PlayerObject = State->Player.Pin();
			if ( PlayerObject.IsValid() )
			{
				static_cast<CAdventurePlayer*>( PlayerObject.Get() )->ApplyDamage( Kind->Damage, Position );
			}
		}
	}
	return StrikeTime >= StrikeTotal;
}

//-----------------------------------------------------------------------------
// 体のステートの動き
//-----------------------------------------------------------------------------
void CAdventureEnemy::ApplyDamage( int32 Damage, const FVector3& From )
{
	if ( bDead )
	{
		return;
	}
	Hp -= Damage;
	FlashTime = 0.25f;
	// 斬られた側は、ヒットストップの間、大きく震える
	ShiverAmount = 0.16f;
	FVector3 Away( Position.x - From.x, 0.0f, Position.z - From.z );
	KnockDirection = Away.LengthSquared() > 0.0001f ? Away.Normalized() : FVector3( 0.0f, 0.0f, 1.0f );
	if ( Hp <= 0 )
	{
		// すぐ、生きていないことにする（同じフレームに、また斬られない）。倒れる動きは、ステートマシンが遷移する
		bDead = true;
		if ( Machine != nullptr )
		{
			Machine->SendEvent( "Died" );
		}
	}
	else if ( Machine != nullptr )
	{
		Machine->SendEvent( "Damaged" );
	}
}

void CAdventureEnemy::BeginHurt()
{
	FlashTime = 0.3f;
}

void CAdventureEnemy::StepHurt( float32 DeltaTime )
{
	MoveBy( KnockDirection * ( 4.0f * DeltaTime ) );
}

void CAdventureEnemy::BeginDead()
{
	bDead = true;
	DeadTime = 0.0f;
	State->DefeatedEnemies.Add( PlaceholderId );
	State->PlayEffect( "Effects/sparks.gxeffect", Position + FVector3( 0.0f, 0.6f, 0.0f ), 1.4f );
	// ときどき、回復薬を落とす（置き物の番号で決まる。毎回、同じ）
	if ( ( PlaceholderId % 3 ) != 0 )
	{
		GetScene()->SpawnObject<CAdventurePickup>( State, FString( ( PlaceholderId % 3 ) == 1 ? "Potion" : "Gem" ), 0, Position );
	}
}

void CAdventureEnemy::Remove()
{
	Destroy();
}

//-----------------------------------------------------------------------------
// 描く
//-----------------------------------------------------------------------------
void CAdventureEnemy::Record3D( FDrawContext& CommandList )
{
	if ( !bHasHome )
	{
		return;
	}
	const FVector3 Facing( NMath::Sin( FacingYaw ), 0.0f, NMath::Cos( FacingYaw ) );
	const FVector3 Right( Facing.z, 0.0f, -Facing.x );
	// ヒットストップの間は、見た目だけ、横に震える（当たり判定の位置は、動かさない）
	const FVector3 Base = Position + State->GetShiverOffset( ShiverAmount );
	const bool bSlime = Kind == &Kinds[0];
	// 倒れたあとは、縮んで消える
	const float32 Shrink = bDead ? NMath::Clamp( 1.0f - DeadTime / 0.9f, 0.05f, 1.0f ) : 1.0f;
	CommandList.Cone3D( Base + FVector3( 0.0f, 0.03f, 0.0f ), Base + FVector3( 0.0f, 0.005f, 0.0f ), Kind->Radius * 1.1f * Shrink, 16, FColor32( 24, 28, 24 ) );
	const bool bFlash = FlashTime > 0.0f;
	// 構えている間は、足元が赤く光る（殴るよ、の合図）
	if ( !bDead && StrikeTime > 0.0f && StrikeTime < StrikeWindup && Machine != nullptr && Machine->GetStateMachine() != nullptr && Machine->GetStateMachine()->IsInState( "Alive" ) )
	{
		CommandList.Cone3D( Base + FVector3( 0.0f, 0.06f, 0.0f ), Base + FVector3( 0.0f, 0.04f, 0.0f ), Kind->AttackRange, 20, FColor32( 230, 60, 50 ) );
	}
	if ( bSlime )
	{
		const float32 Bob = 0.08f * NMath::Sin( State->Time * 6.0f + static_cast<float32>( PlaceholderId ) );
		const FColor32 Body = bFlash ? FColor32( 250, 220, 220 ) : FColor32( 90, 200, 100 );
		CommandList.Sphere3D( Base + FVector3( 0.0f, 0.5f * Shrink + Bob, 0.0f ), 0.55f * Shrink, 14, Body );
		CommandList.Sphere3D( Base + FVector3( 0.0f, 0.62f * Shrink, 0.0f ) + Facing * 0.38f * Shrink + Right * 0.17f, 0.1f * Shrink, 8, FColor32::White );
		CommandList.Sphere3D( Base + FVector3( 0.0f, 0.62f * Shrink, 0.0f ) + Facing * 0.38f * Shrink - Right * 0.17f, 0.1f * Shrink, 8, FColor32::White );
		CommandList.Sphere3D( Base + FVector3( 0.0f, 0.62f * Shrink, 0.0f ) + Facing * 0.46f * Shrink + Right * 0.17f, 0.05f * Shrink, 6, FColor32::Black );
		CommandList.Sphere3D( Base + FVector3( 0.0f, 0.62f * Shrink, 0.0f ) + Facing * 0.46f * Shrink - Right * 0.17f, 0.05f * Shrink, 6, FColor32::Black );
		return;
	}
	// スケルトン: 骨の体・頭蓋骨・剣
	const FColor32 Bone = bFlash ? FColor32( 250, 190, 190 ) : FColor32( 230, 228, 214 );
	const float32 Stride = NMath::Sin( State->Time * 9.0f ) * 0.12f;
	CommandList.Capsule3D( Base + Right * 0.14f + Facing * Stride + FVector3( 0.0f, 0.1f * Shrink, 0.0f ), Base + Right * 0.14f + Facing * Stride + FVector3( 0.0f, 0.5f * Shrink, 0.0f ), 0.07f * Shrink, 6, Bone );
	CommandList.Capsule3D( Base - Right * 0.14f - Facing * Stride + FVector3( 0.0f, 0.1f * Shrink, 0.0f ), Base - Right * 0.14f - Facing * Stride + FVector3( 0.0f, 0.5f * Shrink, 0.0f ), 0.07f * Shrink, 6, Bone );
	CommandList.Capsule3D( Base + FVector3( 0.0f, 0.6f * Shrink, 0.0f ), Base + FVector3( 0.0f, 1.15f * Shrink, 0.0f ), 0.22f * Shrink, 8, Bone );
	const FVector3 Head = Base + FVector3( 0.0f, 1.5f * Shrink, 0.0f );
	CommandList.Sphere3D( Head, 0.26f * Shrink, 10, Bone );
	CommandList.Sphere3D( Head + Facing * 0.2f * Shrink + Right * 0.1f, 0.06f * Shrink, 6, FColor32( 200, 40, 40 ) );
	CommandList.Sphere3D( Head + Facing * 0.2f * Shrink - Right * 0.1f, 0.06f * Shrink, 6, FColor32( 200, 40, 40 ) );
	// 剣: 構えの間は、振りかぶる
	const bool bWinding = StrikeTime > 0.0f && StrikeTime < StrikeWindup;
	const FVector3 Hilt = Base + Right * 0.35f + FVector3( 0.0f, 0.9f * Shrink, 0.0f );
	const FVector3 Tip = bWinding ? Hilt + FVector3( 0.0f, 0.9f, 0.0f ) : Hilt + Facing * 0.8f * Shrink + FVector3( 0.0f, 0.1f, 0.0f );
	CommandList.Capsule3D( Hilt, Tip, 0.06f * Shrink, 6, FColor32( 180, 190, 205 ) );
}
