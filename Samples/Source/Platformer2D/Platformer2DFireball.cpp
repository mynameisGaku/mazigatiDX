#include "Platformer2DFireball.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"
#include "Source/Platformer2D/Platformer2DEnemy.h"
#include "Source/Platformer2D/Platformer2DState.h"

namespace
{
	constexpr float32 Radius = 7.0f;
	constexpr float32 Speed = 470.0f;
	constexpr float32 Gravity = 1900.0f;
	constexpr float32 Bounce = 360.0f;
	constexpr float32 Lifetime = 3.0f;
}

CPlatformer2DFireball::CPlatformer2DFireball( const TSharedPtr<FPlatformer2DState>& InState, const FVector2& InPosition, float32 InDirection )
	: State( InState )
	, Position( InPosition.x, InPosition.y + Radius )
	, Direction( InDirection )
{
	Velocity = FVector2( Direction * Speed, 0.0f );
}

void CPlatformer2DFireball::Tick( float32 DeltaTime )
{
	if ( DeltaTime > 1.0f / 30.0f )
	{
		DeltaTime = 1.0f / 30.0f;
	}
	Age += DeltaTime;
	Velocity.y += Gravity * DeltaTime;
	Velocity.x = Direction * Speed;
	const FTile2DMoveResult Result = State->Level.Move( Position, Radius, Radius * 2.0f, Velocity, DeltaTime );
	if ( Result.bOnGround )
	{
		Velocity.y = -Bounce;
	}
	if ( Result.bHitWall || Result.bHitCeiling || Age >= Lifetime || !State->IsNearCamera( Position.x, 40.0f ) || Position.y > static_cast<float32>( State->Level.Height * FPlatformer2DLevel::TileSize ) )
	{
		Destroy();
		return;
	}
	for ( const TSharedPtr<CPlatformer2DEnemy>& Enemy : GetScene()->FindObjects<CPlatformer2DEnemy>() )
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

void CPlatformer2DFireball::Draw( FDrawContext& CommandList )
{
	const int32 X = State->ToScreenX( Position.x );
	const int32 Y = static_cast<int32>( Position.y - Radius );
	CommandList.Circle( X, Y, 8, FColor32( 250, 120, 30 ), true );
	CommandList.Circle( X, Y, 5, FColor32( 255, 230, 90 ), true );
}
