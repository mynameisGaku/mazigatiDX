#include "Platformer2DEnemy.h"

#include "Source/Platformer2D/Platformer2DState.h"

namespace
{
	constexpr float32 Gravity = 2200.0f;
	constexpr float32 MaxFallSpeed = 900.0f;

	// 画面の左右に、この幅まで広げた範囲に入ったら、動き出す
	constexpr float32 ActiveMargin = 160.0f;
}

CPlatformer2DEnemy::CPlatformer2DEnemy( const TSharedPtr<FPlatformer2DState>& InState, const FVector2& InFeetPosition, float32 InHalfWidth, float32 InHeight )
	: State( InState )
	, Position( InFeetPosition )
	, HalfWidth( InHalfWidth )
	, Height( InHeight )
{}

void CPlatformer2DEnemy::Tick( float32 DeltaTime )
{
	if ( DeltaTime > 1.0f / 30.0f )
	{
		DeltaTime = 1.0f / 30.0f;
	}
	if ( bKnockedOff )
	{
		// 回りながら、飛んで、落ちていく
		Velocity.y += Gravity * DeltaTime;
		Position += Velocity * DeltaTime;
		if ( Position.y > static_cast<float32>( ( State->Level.Height + 4 ) * FPlatformer2DLevel::TileSize ) )
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
	if ( State->IsNearCamera( Position.x, ActiveMargin ) )
	{
		OnTickActive( DeltaTime );
	}
	// 穴に落ちた
	if ( Position.y > static_cast<float32>( ( State->Level.Height + 2 ) * FPlatformer2DLevel::TileSize ) )
	{
		Destroy();
	}
}

void CPlatformer2DEnemy::OnTickActive( float32 DeltaTime )
{
	Walk( DeltaTime, 60.0f );
}

void CPlatformer2DEnemy::Walk( float32 DeltaTime, float32 Speed )
{
	Velocity.x = Direction * Speed;
	Velocity.y = NMath::Min( Velocity.y + Gravity * DeltaTime, MaxFallSpeed );
	const FTile2DMoveResult Result = State->Level.Move( Position, HalfWidth, Height, Velocity, DeltaTime );
	if ( Result.bHitWall )
	{
		Direction = -Direction;
	}
}

void CPlatformer2DEnemy::Squash( float32 Seconds )
{
	bSquashed = true;
	SquashTime = Seconds;
	Velocity = FVector2::Zero;
}

bool CPlatformer2DEnemy::Overlaps( const FVector2& OtherFeet, float32 OtherHalfWidth, float32 OtherHeight ) const
{
	return NMath::Abs( OtherFeet.x - Position.x ) < OtherHalfWidth + HalfWidth && OtherFeet.y > Position.y - Height && OtherFeet.y - OtherHeight < Position.y;
}

void CPlatformer2DEnemy::KnockOff( float32 InDirection )
{
	if ( bKnockedOff )
	{
		return;
	}
	bKnockedOff = true;
	Velocity = FVector2( InDirection * 160.0f, -520.0f );
}
