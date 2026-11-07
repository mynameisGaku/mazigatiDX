#include "Platformer2DShelled.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"
#include "Source/Platformer2D/Platformer2DState.h"

namespace
{
	constexpr float32 ShellHalfWidth = 14.0f;
	constexpr float32 WalkingHeight = 36.0f;
	constexpr float32 ShellHeight = 24.0f;

	// 止まった甲羅は、止まってから、この時間は、蹴れない（踏んだ足が、そのまま蹴ってしまわないように）
	constexpr float32 StillGrace = 0.15f;

	// 蹴り出した直後の、主人公にダメージを与えない時間（蹴った足が、そのまま当たらないように）
	constexpr float32 KickGrace = 0.25f;
}

CPlatformer2DShelled::CPlatformer2DShelled( const TSharedPtr<FPlatformer2DState>& InState, const FVector2& InFeetPosition )
	: CPlatformer2DEnemy( InState, InFeetPosition, ShellHalfWidth, WalkingHeight, "Platformer/Shelled2D.gxstate" )
{}

bool CPlatformer2DShelled::IsDeadly() const
{
	if ( IsInMachineState( "Sliding" ) )
	{
		return GetMachineStateTime() > KickGrace;
	}
	// 歩いているときは、ダメージ。止まった甲羅は、ダメージを与えない
	return IsInMachineState( "Walk" );
}

bool CPlatformer2DShelled::IsStill() const
{
	return IsInMachineState( "Still" ) && GetMachineStateTime() > StillGrace;
}

bool CPlatformer2DShelled::IsDangerousToEnemies() const
{
	return IsInMachineState( "Sliding" );
}

void CPlatformer2DShelled::BeginShell()
{
	// 甲羅にこもって、止まる（背が低くなる）
	Height = ShellHeight;
	Velocity.x = 0.0f;
}

void CPlatformer2DShelled::BeginSlide()
{
	Height = ShellHeight;
	Direction = Position.x >= LastPlayerX ? 1.0f : -1.0f;
}

void CPlatformer2DShelled::StepSlide( float32 DeltaTime, float32 Speed )
{
	StepWalk( DeltaTime, Speed, false );
	// すべっている甲羅は、ぶつかった敵を倒す
	for ( const TSharedPtr<CPlatformer2DEnemy>& Other : GetScene()->FindObjects<CPlatformer2DEnemy>() )
	{
		if ( Other.Get() != this && Other->IsAlive() && Other->Overlaps( Position, HalfWidth, Height ) )
		{
			Other->KnockOff( Direction );
		}
	}
}

void CPlatformer2DShelled::Draw( FDrawContext& CommandList )
{
	const int32 X = State->ToScreenX( Position.x );
	const int32 Y = static_cast<int32>( Position.y );
	const FColor32 ShellColor( 40, 168, 72 );
	if ( IsInMachineState( "Walk" ) )
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
	const int32 Spin = IsInMachineState( "Sliding" ) ? static_cast<int32>( State->Time * 20.0f ) % 3 : 0;
	CommandList.Oval( X, Y - 12, 15, 12, ShellColor, true );
	CommandList.Oval( X, Y - 12, 15, 12, FColor32( 20, 100, 44 ), false, 2 );
	CommandList.Line( X - 10 + Spin * 4, Y - 22, X - 10 + Spin * 4, Y - 2, FColor32( 20, 100, 44 ), 2 );
	CommandList.Oval( X, Y - 3, 12, 3, FColor32( 240, 220, 150 ), true );
}
