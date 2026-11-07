#include "Platformer2DEnemy.h"

#include "Library/Framework/AI/StateMachineComponent.h"
#include "Source/Platformer2D/Platformer2DState.h"

namespace
{
	constexpr float32 Gravity = 2200.0f;
	constexpr float32 MaxFallSpeed = 900.0f;

	// 画面の左右に、この幅まで広げた範囲に入ったら、動き出す
	constexpr float32 ActiveMargin = 160.0f;
}

CPlatformer2DEnemy::CPlatformer2DEnemy( const TSharedPtr<FPlatformer2DState>& InState, const FVector2& InFeetPosition, float32 InHalfWidth, float32 InHeight, const char* InStatePath )
	: State( InState )
	, StatePath( InStatePath )
	, Position( InFeetPosition )
	, HalfWidth( InHalfWidth )
	, Height( InHeight )
{}

void CPlatformer2DEnemy::OnSpawn()
{
	// 動きのステートマシンを付ける。すぐ読み込む（最初のフレームから、ステートにいられるように）
	Machine = AddComponent<CStateMachineComponent>( StatePath );
	Machine->Load();
}

void CPlatformer2DEnemy::Tick( float32 DeltaTime )
{
	// 穴に落ちたら、消える（動きは、ステートマシンの部品が行う）
	if ( Position.y > static_cast<float32>( ( State->Level.Height + 2 ) * FPlatformer2DLevel::TileSize ) )
	{
		Destroy();
	}
}

bool CPlatformer2DEnemy::IsInMachineState( const char* Path ) const
{
	const CStateMachine* StateMachine = Machine != nullptr ? Machine->GetStateMachine() : nullptr;
	return StateMachine != nullptr && StateMachine->IsInState( Path );
}

float32 CPlatformer2DEnemy::GetMachineStateTime() const
{
	const CStateMachine* StateMachine = Machine != nullptr ? Machine->GetStateMachine() : nullptr;
	return StateMachine != nullptr ? StateMachine->GetLeafStateTime() : 0.0f;
}

bool CPlatformer2DEnemy::IsActive() const
{
	return State->IsNearCamera( Position.x, ActiveMargin );
}

//-----------------------------------------------------------------------------
// 外から触れる窓口: 状態は、すぐ（フラグ）と、ステートマシンの遷移（次のフレーム）の両方に、反映する
//-----------------------------------------------------------------------------
void CPlatformer2DEnemy::OnStomped( float32 PlayerX )
{
	LastPlayerX = PlayerX;
	Machine->SendEvent( "Stomped" );
}

void CPlatformer2DEnemy::OnKicked( float32 PlayerX )
{
	LastPlayerX = PlayerX;
	Machine->SendEvent( "Kicked" );
}

void CPlatformer2DEnemy::KnockOff( float32 InDirection )
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
void CPlatformer2DEnemy::StepWalk( float32 DeltaTime, float32 Speed, bool bAvoidEdges )
{
	if ( !IsActive() )
	{
		return;
	}
	if ( DeltaTime > 1.0f / 30.0f )
	{
		DeltaTime = 1.0f / 30.0f;
	}
	Velocity.x = Direction * Speed;
	Velocity.y = NMath::Min( Velocity.y + Gravity * DeltaTime, MaxFallSpeed );
	const FTile2DMoveResult Result = State->Level.Move( Position, HalfWidth, Height, Velocity, DeltaTime );
	if ( Result.bHitWall )
	{
		Direction = -Direction;
	}
}

void CPlatformer2DEnemy::StepRest( float32 DeltaTime )
{
	StepWalk( DeltaTime, 0.0f, false );
}

void CPlatformer2DEnemy::BeginSquash()
{
	bSquashed = true;
	Velocity = FVector2::Zero;
}

void CPlatformer2DEnemy::BeginFly()
{
	bKnockedOff = true;
	Velocity = FVector2( FlyDirection * 160.0f, -520.0f );
}

void CPlatformer2DEnemy::StepFly( float32 DeltaTime )
{
	if ( DeltaTime > 1.0f / 30.0f )
	{
		DeltaTime = 1.0f / 30.0f;
	}
	// 回りながら、飛んで、落ちていく（タイルには当たらない）
	Velocity.y += Gravity * DeltaTime;
	Position += Velocity * DeltaTime;
}

void CPlatformer2DEnemy::Remove()
{
	Destroy();
}

bool CPlatformer2DEnemy::Overlaps( const FVector2& OtherFeet, float32 OtherHalfWidth, float32 OtherHeight ) const
{
	return NMath::Abs( OtherFeet.x - Position.x ) < OtherHalfWidth + HalfWidth && OtherFeet.y > Position.y - Height && OtherFeet.y - OtherHeight < Position.y;
}
