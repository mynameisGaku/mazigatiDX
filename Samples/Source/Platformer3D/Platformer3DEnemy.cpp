#include "Platformer3DEnemy.h"

#include "Source/Platformer3D/Platformer3DState.h"

namespace
{
	constexpr float32 Gravity = 32.0f;
	constexpr float32 MaxFallSpeed = 30.0f;

	// 主人公から、この距離より近いときだけ、動く
	constexpr float32 ActiveDistance = 45.0f;
}

CPlatformer3DEnemy::CPlatformer3DEnemy( const TSharedPtr<FPlatformer3DState>& InState, const FVector3& InFeetPosition, float32 InHalfWidth, float32 InHeight )
	: State( InState )
	, Position( InFeetPosition )
	, HalfWidth( InHalfWidth )
	, Height( InHeight )
{}

void CPlatformer3DEnemy::Tick( float32 DeltaTime )
{
	if ( DeltaTime > 1.0f / 30.0f )
	{
		DeltaTime = 1.0f / 30.0f;
	}
	if ( bKnockedOff )
	{
		// 飛んで、落ちていく
		Velocity.y -= Gravity * DeltaTime;
		Position += Velocity * DeltaTime;
		if ( Position.y < FPlatformer3DLevel::KillHeight )
		{
			Destroy();
		}
		return;
	}
	if ( bSquashed )
	{
		SquashTime -= DeltaTime;
		if ( SquashTime <= 0.0f )
		{
			Destroy();
		}
		return;
	}
	const FVector3 ToPlayer = State->PlayerPosition - Position;
	if ( NMath::Abs( ToPlayer.x ) < ActiveDistance && NMath::Abs( ToPlayer.z ) < ActiveDistance )
	{
		OnTickActive( DeltaTime );
	}
	if ( Position.y < FPlatformer3DLevel::KillHeight )
	{
		Destroy();
	}
}

void CPlatformer3DEnemy::OnTickActive( float32 DeltaTime )
{
	Walk( DeltaTime, 1.2f, true );
}

void CPlatformer3DEnemy::Walk( float32 DeltaTime, float32 Speed, bool bAvoidEdges )
{
	// 崖の前では、向きを反対にする（足元から 0.3 先の、すぐ下に足場があるか）
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

void CPlatformer3DEnemy::Squash( float32 Seconds )
{
	bSquashed = true;
	SquashTime = Seconds;
	Velocity = FVector3::Zero;
}

bool CPlatformer3DEnemy::Overlaps( const FVector3& OtherFeet, float32 OtherHalfWidth, float32 OtherHeight ) const
{
	return NMath::Abs( OtherFeet.x - Position.x ) < OtherHalfWidth + HalfWidth
		&& NMath::Abs( OtherFeet.z - Position.z ) < OtherHalfWidth + HalfWidth
		&& OtherFeet.y < Position.y + Height && OtherFeet.y + OtherHeight > Position.y;
}

void CPlatformer3DEnemy::KnockOff( const FVector3& InDirection )
{
	if ( bKnockedOff )
	{
		return;
	}
	bKnockedOff = true;
	Velocity = FVector3( InDirection.x * 4.0f, 8.0f, InDirection.z * 4.0f );
}
