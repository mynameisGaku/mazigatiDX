#include "Platformer3DFireball.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"
#include "Source/Platformer3D/Platformer3DEnemy.h"
#include "Source/Platformer3D/Platformer3DState.h"

namespace
{
	constexpr float32 Radius = 0.2f;
	constexpr float32 Speed = 13.0f;
	constexpr float32 Gravity = 26.0f;
	constexpr float32 Bounce = 7.5f;
	constexpr float32 Lifetime = 3.0f;
}

CPlatformer3DFireball::CPlatformer3DFireball( const TSharedPtr<FPlatformer3DState>& InState, const FVector3& InCenter, const FVector3& InDirection )
	: State( InState )
	, Position( InCenter.x, InCenter.y - Radius, InCenter.z )
	, Direction( InDirection )
{
	Velocity = FVector3( Direction.x * Speed, 0.0f, Direction.z * Speed );
}

void CPlatformer3DFireball::Tick( float32 DeltaTime )
{
	if ( DeltaTime > 1.0f / 30.0f )
	{
		DeltaTime = 1.0f / 30.0f;
	}
	Age += DeltaTime;
	Velocity.y -= Gravity * DeltaTime;
	Velocity.x = Direction.x * Speed;
	Velocity.z = Direction.z * Speed;
	const FBlock3DMoveResult Result = State->Level.Move( Position, Radius, Radius * 2.0f, Velocity, DeltaTime );
	if ( Result.bOnGround )
	{
		Velocity.y = Bounce;
	}
	if ( Result.bHitWall || Result.bHitCeiling || Age >= Lifetime || Position.y < FPlatformer3DLevel::KillHeight )
	{
		Destroy();
		return;
	}
	for ( const TSharedPtr<CPlatformer3DEnemy>& Enemy : GetScene()->FindObjects<CPlatformer3DEnemy>() )
	{
		if ( Enemy->IsAlive() && Enemy->Overlaps( Position, Radius, Radius * 2.0f ) )
		{
			Enemy->KnockOff( Direction );
			State->Status.AddScore( 100 );
			Destroy();
			return;
		}
	}
}

void CPlatformer3DFireball::Record3D( FDrawContext& CommandList )
{
	const FVector3 Center = Position + FVector3( 0.0f, Radius, 0.0f );
	CommandList.Sphere3D( Center, Radius, 10, FColor32( 250, 120, 30 ) );
	CommandList.Sphere3D( Center, Radius * 0.6f, 8, FColor32( 255, 230, 90 ) );
}
