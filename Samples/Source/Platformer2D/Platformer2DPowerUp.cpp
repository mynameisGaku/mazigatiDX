#include "Platformer2DPowerUp.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Platformer2D/Platformer2DState.h"

namespace
{
	constexpr float32 Gravity = 2200.0f;
	constexpr float32 MaxFallSpeed = 900.0f;
	constexpr float32 MushroomSpeed = 90.0f;
	constexpr float32 StarSpeed = 150.0f;
	constexpr float32 StarBounce = 520.0f;
}

CPlatformer2DPowerUp::CPlatformer2DPowerUp( const TSharedPtr<FPlatformer2DState>& InState, EPlatformerItem InItem, const FVector2& InBlockTop )
	: State( InState )
	, Item( InItem )
	, Position( InBlockTop.x, InBlockTop.y + static_cast<float32>( FPlatformer2DLevel::TileSize ) )
	, BlockTopY( InBlockTop.y )
{
	Velocity.x = Item == EPlatformerItem::Star ? StarSpeed : MushroomSpeed;
}

void CPlatformer2DPowerUp::Tick( float32 DeltaTime )
{
	if ( DeltaTime > 1.0f / 30.0f )
	{
		DeltaTime = 1.0f / 30.0f;
	}
	if ( IsEmerging() )
	{
		// ブロックの中から、1 タイルぶん せり上がる
		EmergeTime += DeltaTime;
		const float32 T = NMath::Min( EmergeTime / EmergeSeconds, 1.0f );
		Position.y = BlockTopY + static_cast<float32>( FPlatformer2DLevel::TileSize ) * ( 1.0f - T );
		return;
	}
	if ( Item != EPlatformerItem::Flower )
	{
		const float32 Direction = Velocity.x >= 0.0f ? 1.0f : -1.0f;
		const float32 Speed = Item == EPlatformerItem::Star ? StarSpeed : MushroomSpeed;
		Velocity.x = Direction * Speed;
		Velocity.y = NMath::Min( Velocity.y + Gravity * DeltaTime, MaxFallSpeed );
		const FTile2DMoveResult Result = State->Level.Move( Position, HalfWidth, Height, Velocity, DeltaTime );
		if ( Result.bHitWall )
		{
			Velocity.x = -Direction * Speed;
		}
		if ( Result.bOnGround && Item == EPlatformerItem::Star )
		{
			Velocity.y = -StarBounce;
		}
	}
	// 穴に落ちた
	if ( Position.y > static_cast<float32>( ( State->Level.Height + 2 ) * FPlatformer2DLevel::TileSize ) )
	{
		Destroy();
	}
}

void CPlatformer2DPowerUp::Draw( FDrawContext& CommandList )
{
	const int32 X = State->ToScreenX( Position.x );
	const int32 Y = static_cast<int32>( Position.y );
	switch ( Item )
	{
	case EPlatformerItem::Mushroom:
		CommandList.Box( X - 7, Y - 14, X + 7, Y, FColor32( 250, 232, 196 ), true );
		CommandList.Oval( X, Y - 18, 15, 12, FColor32( 224, 64, 56 ), true );
		CommandList.Circle( X - 6, Y - 20, 4, FColor32::White, true );
		CommandList.Circle( X + 6, Y - 20, 4, FColor32::White, true );
		CommandList.Circle( X, Y - 25, 3, FColor32::White, true );
		CommandList.Circle( X - 3, Y - 9, 1, FColor32::Black, true );
		CommandList.Circle( X + 3, Y - 9, 1, FColor32::Black, true );
		break;
	case EPlatformerItem::Flower:
		CommandList.Box( X - 2, Y - 14, X + 2, Y, FColor32( 40, 168, 72 ), true );
		CommandList.Circle( X - 8, Y - 20, 6, FColor32( 250, 120, 40 ), true );
		CommandList.Circle( X + 8, Y - 20, 6, FColor32( 250, 120, 40 ), true );
		CommandList.Circle( X, Y - 28, 6, FColor32( 250, 120, 40 ), true );
		CommandList.Circle( X, Y - 14, 6, FColor32( 250, 120, 40 ), true );
		CommandList.Circle( X, Y - 20, 6, FColor32( 250, 220, 60 ), true );
		break;
	case EPlatformerItem::Star:
		{
			// 6 つの角（三角形 2 つを、逆さに重ねる）。色は、虹のように変わる
			const FColor32 Color = FColor::FromHSV( NMath::Frac( State->Time * 3.0f ), 0.5f, 1.0f ).ToColor32();
			const int32 CenterY = Y - 14;
			CommandList.Triangle( X, CenterY - 15, X - 14, CenterY + 9, X + 14, CenterY + 9, Color, true );
			CommandList.Triangle( X, CenterY + 15, X - 14, CenterY - 9, X + 14, CenterY - 9, Color, true );
			CommandList.Circle( X - 4, CenterY - 2, 2, FColor32::Black, true );
			CommandList.Circle( X + 4, CenterY - 2, 2, FColor32::Black, true );
		}
		break;
	default:
		break;
	}
}
