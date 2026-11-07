#include "Platformer3DEnemy.h"

#include "Library/Framework/AI/Blackboard/Blackboard.h"
#include "Library/Framework/AI/BehaviorTreeComponent.h"
#include "Library/Framework/AI/StateMachineComponent.h"
#include "Source/Platformer3D/Platformer3DState.h"

namespace
{
	constexpr float32 Gravity = 32.0f;
	constexpr float32 MaxFallSpeed = 30.0f;

	// 主人公から、この距離より近いときだけ、動く
	constexpr float32 ActiveDistance = 45.0f;
}

CPlatformer3DEnemy::CPlatformer3DEnemy( const TSharedPtr<FPlatformer3DState>& InState, const FVector3& InFeetPosition, float32 InHalfWidth, float32 InHeight, const char* InStatePath )
	: State( InState )
	, StatePath( InStatePath )
	, Position( InFeetPosition )
	, HalfWidth( InHalfWidth )
	, Height( InHeight )
{}

void CPlatformer3DEnemy::OnSpawn()
{
	// 動きのステートマシンを付ける。すぐ読み込む（ブラックボードを、最初のフレームから使えるように）
	Machine = AddComponent<CStateMachineComponent>( StatePath );
	Machine->Load();

	// 頭脳（ビヘイビアツリー）。ステートマシンと、同じブラックボードを見る（先に読んだ側が作り、あとの側が共有する）
	if ( const char* BrainPath = GetBrainPath() )
	{
		Brain = AddComponent<CBehaviorTreeComponent>( BrainPath );
		Brain->Load();
	}
}

void CPlatformer3DEnemy::Tick( float32 DeltaTime )
{
	// センサー（ステートマシンの遷移の条件になる値を書く）。動きは、ステートマシンの部品が行う
	if ( CBlackboard* Blackboard = Machine != nullptr ? Machine->GetBlackboard().Get() : nullptr )
	{
		Sense( *Blackboard );
	}
	// 落ちたら、消える
	if ( Position.y < FPlatformer3DLevel::KillHeight )
	{
		Destroy();
	}
}

bool CPlatformer3DEnemy::IsInMachineState( const char* Path ) const
{
	const CStateMachine* StateMachine = Machine != nullptr ? Machine->GetStateMachine() : nullptr;
	return StateMachine != nullptr && StateMachine->IsInState( Path );
}

float32 CPlatformer3DEnemy::GetMachineStateTime() const
{
	const CStateMachine* StateMachine = Machine != nullptr ? Machine->GetStateMachine() : nullptr;
	return StateMachine != nullptr ? StateMachine->GetLeafStateTime() : 0.0f;
}

bool CPlatformer3DEnemy::IsActive() const
{
	const FVector3 ToPlayer = State->PlayerPosition - Position;
	return NMath::Abs( ToPlayer.x ) < ActiveDistance && NMath::Abs( ToPlayer.z ) < ActiveDistance;
}

//-----------------------------------------------------------------------------
// 外から触れる窓口
//-----------------------------------------------------------------------------
void CPlatformer3DEnemy::OnStomped( const FVector3& PlayerPosition )
{
	LastPlayerPosition = PlayerPosition;
	Machine->SendEvent( "Stomped" );
}

void CPlatformer3DEnemy::OnKicked( const FVector3& PlayerPosition )
{
	LastPlayerPosition = PlayerPosition;
	Machine->SendEvent( "Kicked" );
}

void CPlatformer3DEnemy::KnockOff( const FVector3& InDirection )
{
	if ( bKnockedOff )
	{
		return;
	}
	bKnockedOff = true;
	FlyDirection = InDirection;
	Machine->SendEvent( "KnockedOff" );
}

//-----------------------------------------------------------------------------
// IPlatformerEnemyBody
//-----------------------------------------------------------------------------
void CPlatformer3DEnemy::StepWalk( float32 DeltaTime, float32 Speed, bool bAvoidEdges )
{
	if ( !IsActive() )
	{
		return;
	}
	if ( DeltaTime > 1.0f / 30.0f )
	{
		DeltaTime = 1.0f / 30.0f;
	}
	// 崖の前では、向きを反対にする（足元から少し先の、すぐ下に足場があるか）
	float32 UseSpeed = Speed;
	if ( bAvoidEdges && Speed > 0.0f )
	{
		const FVector3 Ahead = Position + Direction * ( HalfWidth + 0.4f );
		if ( !State->Level.HasGroundBelow( Ahead.x, Ahead.z, Position.y, 0.6f ) )
		{
			Direction = FVector3( -Direction.x, 0.0f, -Direction.z );
			UseSpeed = 0.0f;
		}
	}
	Velocity.x = Direction.x * UseSpeed;
	Velocity.z = Direction.z * UseSpeed;
	Velocity.y = NMath::Max( Velocity.y - Gravity * DeltaTime, -MaxFallSpeed );
	const FBlock3DMoveResult Result = State->Level.Move( Position, HalfWidth, Height, Velocity, DeltaTime );
	if ( Result.bHitWall )
	{
		Direction = FVector3( -Direction.x, 0.0f, -Direction.z );
	}
}

void CPlatformer3DEnemy::StepRest( float32 DeltaTime )
{
	StepWalk( DeltaTime, 0.0f, false );
}

void CPlatformer3DEnemy::BeginSquash()
{
	bSquashed = true;
	Velocity = FVector3::Zero;
}

void CPlatformer3DEnemy::BeginFly()
{
	bKnockedOff = true;
	Velocity = FVector3( FlyDirection.x * 4.0f, 8.0f, FlyDirection.z * 4.0f );
}

void CPlatformer3DEnemy::StepFly( float32 DeltaTime )
{
	if ( DeltaTime > 1.0f / 30.0f )
	{
		DeltaTime = 1.0f / 30.0f;
	}
	// 飛んで、落ちていく（箱には当たらない）
	Velocity.y -= Gravity * DeltaTime;
	Position += Velocity * DeltaTime;
}

void CPlatformer3DEnemy::Remove()
{
	Destroy();
}

bool CPlatformer3DEnemy::Overlaps( const FVector3& OtherFeet, float32 OtherHalfWidth, float32 OtherHeight ) const
{
	return NMath::Abs( OtherFeet.x - Position.x ) < OtherHalfWidth + HalfWidth
		&& NMath::Abs( OtherFeet.z - Position.z ) < OtherHalfWidth + HalfWidth
		&& OtherFeet.y < Position.y + Height && OtherFeet.y + OtherHeight > Position.y;
}
