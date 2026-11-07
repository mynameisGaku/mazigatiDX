#include "Platformer2DShelled.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"
#include "Source/Platformer2D/Platformer2DState.h"

namespace
{
	constexpr float32 ShellHalfWidth = 14.0f;
	constexpr float32 WalkingHeight = 36.0f;
	constexpr float32 ShellHeight = 24.0f;
	constexpr float32 SlideSpeed = 460.0f;
	constexpr float32 WalkSpeed = 50.0f;
}

CPlatformer2DShelled::CPlatformer2DShelled( const TSharedPtr<FPlatformer2DState>& InState, const FVector2& InFeetPosition )
	: CPlatformer2DEnemy( InState, InFeetPosition, ShellHalfWidth, WalkingHeight )
{}

bool CPlatformer2DShelled::IsDeadly() const
{
	if ( Mode == EMode::Still )
	{
		return false;
	}
	return KickGrace <= 0.0f;
}

void CPlatformer2DShelled::OnStomped( float32 PlayerX )
{
	if ( Mode == EMode::Walking || Mode == EMode::Sliding )
	{
		// 甲羅にこもって、止まる（背が低くなる）
		Mode = EMode::Still;
		Height = ShellHeight;
		Velocity.x = 0.0f;
		return;
	}
	OnKicked( PlayerX );
}

void CPlatformer2DShelled::OnKicked( float32 PlayerX )
{
	Mode = EMode::Sliding;
	Direction = Position.x >= PlayerX ? 1.0f : -1.0f;
	KickGrace = 0.25f;
}

void CPlatformer2DShelled::OnTickActive( float32 DeltaTime )
{
	if ( KickGrace > 0.0f )
	{
		KickGrace -= DeltaTime;
	}
	switch ( Mode )
	{
	case EMode::Walking:
		Walk( DeltaTime, WalkSpeed );
		break;
	case EMode::Still:
		// 止まったまま（重力だけ）
		Walk( DeltaTime, 0.0f );
		break;
	case EMode::Sliding:
		Walk( DeltaTime, SlideSpeed );
		// すべっている甲羅は、ぶつかった敵を倒す
		for ( const TSharedPtr<CPlatformer2DEnemy>& Other : GetScene()->FindObjects<CPlatformer2DEnemy>() )
		{
			if ( Other.Get() != this && Other->IsAlive() && Other->Overlaps( Position, HalfWidth, Height ) )
			{
				Other->KnockOff( Direction );
			}
		}
		break;
	}
}

void CPlatformer2DShelled::Draw( FDrawContext& CommandList )
{
	const int32 X = State->ToScreenX( Position.x );
	const int32 Y = static_cast<int32>( Position.y );
	const FColor32 ShellColor( 40, 168, 72 );
	if ( Mode == EMode::Walking )
	{
		const bool bStep = ( static_cast<int32>( State->Time * 5.0f ) % 2 ) == 0;
		// 足と、頭（進む向きに出る）
		CommandList.Oval( X - 6 + ( bStep ? 2 : -2 ), Y - 3, 6, 4, FColor32( 240, 200, 90 ), true );
		CommandList.Oval( X + 6 + ( bStep ? -2 : 2 ), Y - 3, 6, 4, FColor32( 240, 200, 90 ), true );
		const int32 HeadX = X + static_cast<int32>( Direction * 9.0f );
		CommandList.Circle( HeadX, Y - 30, 8, FColor32( 240, 200, 90 ), true );
		CommandList.Circle( HeadX + static_cast<int32>( Direction * 3.0f ), Y - 32, 2, FColor32::Black, true );
		// 甲羅（背中）
		CommandList.Oval( X - static_cast<int32>( Direction * 2.0f ), Y - 14, 14, 12, ShellColor, true );
		CommandList.Oval( X - static_cast<int32>( Direction * 2.0f ), Y - 14, 14, 12, FColor32( 20, 100, 44 ), false, 2 );
		return;
	}
	// こもった甲羅（すべっているときは、模様が回る）
	const int32 Spin = Mode == EMode::Sliding ? static_cast<int32>( State->Time * 20.0f ) % 3 : 0;
	CommandList.Oval( X, Y - 12, 15, 12, ShellColor, true );
	CommandList.Oval( X, Y - 12, 15, 12, FColor32( 20, 100, 44 ), false, 2 );
	CommandList.Line( X - 10 + Spin * 4, Y - 22, X - 10 + Spin * 4, Y - 2, FColor32( 20, 100, 44 ), 2 );
	CommandList.Oval( X, Y - 3, 12, 3, FColor32( 240, 220, 150 ), true );
}
