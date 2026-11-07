#include "Platformer3DPowerUp.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Platformer3D/Platformer3DState.h"

namespace
{
	constexpr float32 Gravity = 32.0f;
	constexpr float32 MaxFallSpeed = 30.0f;
	constexpr float32 MushroomSpeed = 2.2f;
	constexpr float32 StarSpeed = 3.6f;
	constexpr float32 StarBounce = 11.0f;
}

CPlatformer3DPowerUp::CPlatformer3DPowerUp( const TSharedPtr<FPlatformer3DState>& InState, EPlatformerItem InItem, const FVector3& InBlockTop )
	: State( InState )
	, Item( InItem )
	, Position( InBlockTop.x, InBlockTop.y - 1.0f, InBlockTop.z )
	, BlockTopY( InBlockTop.y )
{
	// 前（+Z）へ進む
	Velocity.z = Item == EPlatformerItem::Star ? StarSpeed : MushroomSpeed;
}

void CPlatformer3DPowerUp::Tick( float32 DeltaTime )
{
	if ( DeltaTime > 1.0f / 30.0f )
	{
		DeltaTime = 1.0f / 30.0f;
	}
	if ( IsEmerging() )
	{
		// ブロックの中から、1 メートルぶん せり上がる
		EmergeTime += DeltaTime;
		const float32 T = NMath::Min( EmergeTime / EmergeSeconds, 1.0f );
		Position.y = BlockTopY - 1.0f * ( 1.0f - T );
		return;
	}
	if ( Item != EPlatformerItem::Flower )
	{
		const float32 Speed = Item == EPlatformerItem::Star ? StarSpeed : MushroomSpeed;
		const float32 DirectionZ = Velocity.z >= 0.0f ? 1.0f : -1.0f;
		Velocity.z = DirectionZ * Speed;
		Velocity.y = NMath::Max( Velocity.y - Gravity * DeltaTime, -MaxFallSpeed );
		const FBlock3DMoveResult Result = State->Level.Move( Position, HalfWidth, Height, Velocity, DeltaTime );
		if ( Result.bHitWall )
		{
			Velocity.z = -DirectionZ * Speed;
		}
		if ( Result.bOnGround && Item == EPlatformerItem::Star )
		{
			Velocity.y = StarBounce;
		}
	}
	if ( Position.y < FPlatformer3DLevel::KillHeight )
	{
		Destroy();
	}
}

void CPlatformer3DPowerUp::Record3D( FDrawContext& CommandList )
{
	if ( !IsEmerging() )
	{
		RecordBlobShadow( CommandList, State->Level, Position, 0.4f );
	}
	switch ( Item )
	{
	case EPlatformerItem::Mushroom:
		CommandList.Capsule3D( Position + FVector3( 0.0f, 0.15f, 0.0f ), Position + FVector3( 0.0f, 0.35f, 0.0f ), 0.22f, 10, FColor32( 250, 232, 196 ) );
		CommandList.Sphere3D( Position + FVector3( 0.0f, 0.5f, 0.0f ), 0.34f, 14, FColor32( 224, 64, 56 ) );
		CommandList.Sphere3D( Position + FVector3( 0.0f, 0.82f, 0.0f ), 0.1f, 8, FColor32::White );
		CommandList.Sphere3D( Position + FVector3( 0.26f, 0.6f, 0.0f ), 0.1f, 8, FColor32::White );
		CommandList.Sphere3D( Position + FVector3( -0.26f, 0.6f, 0.0f ), 0.1f, 8, FColor32::White );
		CommandList.Sphere3D( Position + FVector3( 0.0f, 0.6f, 0.26f ), 0.1f, 8, FColor32::White );
		break;
	case EPlatformerItem::Flower:
		{
			CommandList.Capsule3D( Position + FVector3( 0.0f, 0.05f, 0.0f ), Position + FVector3( 0.0f, 0.4f, 0.0f ), 0.05f, 6, FColor32( 40, 168, 72 ) );
			const float32 Angle = State->Time * 2.0f;
			for ( int32 Petal = 0; Petal < 4; ++Petal )
			{
				const float32 A = Angle + NMath::HALF_PI * static_cast<float32>( Petal );
				CommandList.Sphere3D( Position + FVector3( NMath::Cos( A ) * 0.2f, 0.55f, NMath::Sin( A ) * 0.2f ), 0.14f, 8, FColor32( 250, 120, 40 ) );
			}
			CommandList.Sphere3D( Position + FVector3( 0.0f, 0.55f, 0.0f ), 0.14f, 8, FColor32( 250, 220, 60 ) );
		}
		break;
	case EPlatformerItem::Star:
		{
			// 2 つの円すいを、底どうしで合わせた形。色は虹のように変わる
			const FColor32 Color = FColor::FromHSV( NMath::Frac( State->Time * 3.0f ), 0.5f, 1.0f ).ToColor32();
			const FVector3 Center = Position + FVector3( 0.0f, 0.45f, 0.0f );
			const float32 Angle = State->Time * 4.0f;
			CommandList.Cone3D( Center + FVector3( 0.0f, 0.4f, 0.0f ), Center, 0.32f, 6, Color );
			CommandList.Cone3D( Center - FVector3( 0.0f, 0.4f, 0.0f ), Center, 0.32f, 6, Color );
			CommandList.Sphere3D( Center + FVector3( NMath::Cos( Angle ) * 0.3f, 0.0f, NMath::Sin( Angle ) * 0.3f ), 0.07f, 6, FColor32::Black );
		}
		break;
	default:
		break;
	}
}
