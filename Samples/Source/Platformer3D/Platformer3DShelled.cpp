#include "Platformer3DShelled.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"
#include "Source/Platformer3D/Platformer3DState.h"

namespace
{
	constexpr float32 ShellHalfWidth = 0.45f;
	constexpr float32 WalkingHeight = 1.1f;
	constexpr float32 ShellHeight = 0.6f;
	constexpr float32 SlideSpeed = 11.0f;
	constexpr float32 WalkSpeed = 1.6f;
}

CPlatformer3DShelled::CPlatformer3DShelled( const TSharedPtr<FPlatformer3DState>& InState, const FVector3& InFeetPosition )
	: CPlatformer3DEnemy( InState, InFeetPosition, ShellHalfWidth, WalkingHeight )
{}

bool CPlatformer3DShelled::IsDeadly() const
{
	if ( Mode == EMode::Still )
	{
		return false;
	}
	return KickGrace <= 0.0f;
}

void CPlatformer3DShelled::OnStomped( const FVector3& PlayerPosition )
{
	if ( Mode == EMode::Walking || Mode == EMode::Sliding )
	{
		// 甲羅にこもって、止まる（背が低くなる）
		Mode = EMode::Still;
		Height = ShellHeight;
		Velocity.x = 0.0f;
		Velocity.z = 0.0f;
		return;
	}
	OnKicked( PlayerPosition );
}

void CPlatformer3DShelled::OnKicked( const FVector3& PlayerPosition )
{
	Mode = EMode::Sliding;
	FVector3 Away( Position.x - PlayerPosition.x, 0.0f, Position.z - PlayerPosition.z );
	if ( Away.LengthSquared() < 0.0001f )
	{
		Away = FVector3( 0.0f, 0.0f, 1.0f );
	}
	Direction = Away.Normalized();
	KickGrace = 0.25f;
}

void CPlatformer3DShelled::OnTickActive( float32 DeltaTime )
{
	if ( KickGrace > 0.0f )
	{
		KickGrace -= DeltaTime;
	}
	switch ( Mode )
	{
	case EMode::Walking:
		Walk( DeltaTime, WalkSpeed, true );
		break;
	case EMode::Still:
		// 止まったまま（重力だけ）
		Walk( DeltaTime, 0.0f, false );
		break;
	case EMode::Sliding:
		Walk( DeltaTime, SlideSpeed, false );
		// すべっている甲羅は、ぶつかった敵を倒す
		for ( const TSharedPtr<CPlatformer3DEnemy>& Other : GetScene()->FindObjects<CPlatformer3DEnemy>() )
		{
			if ( Other.Get() != this && Other->IsAlive() && Other->Overlaps( Position, HalfWidth, Height ) )
			{
				Other->KnockOff( Direction );
			}
		}
		break;
	}
}

void CPlatformer3DShelled::Record3D( FDrawContext& CommandList )
{
	RecordBlobShadow( CommandList, State->Level, Position, 0.5f );
	const FColor32 ShellColor( 40, 168, 72 );
	if ( Mode == EMode::Walking )
	{
		const FVector3 Right( Direction.z, 0.0f, -Direction.x );
		const bool bStep = ( static_cast<int32>( State->Time * 5.0f ) % 2 ) == 0;
		const float32 Step = bStep ? 0.1f : -0.1f;
		CommandList.Sphere3D( Position + Right * 0.2f + Direction * Step + FVector3( 0.0f, 0.1f, 0.0f ), 0.12f, 8, FColor32( 240, 200, 90 ) );
		CommandList.Sphere3D( Position - Right * 0.2f - Direction * Step + FVector3( 0.0f, 0.1f, 0.0f ), 0.12f, 8, FColor32( 240, 200, 90 ) );
		// 頭（進む向きに出る）と、甲羅（背中）
		CommandList.Sphere3D( Position + FVector3( 0.0f, 0.85f, 0.0f ) + Direction * 0.35f, 0.2f, 10, FColor32( 240, 200, 90 ) );
		CommandList.Sphere3D( Position + FVector3( 0.0f, 0.9f, 0.0f ) + Direction * 0.5f + Right * 0.08f, 0.04f, 6, FColor32::Black );
		CommandList.Sphere3D( Position + FVector3( 0.0f, 0.9f, 0.0f ) + Direction * 0.5f - Right * 0.08f, 0.04f, 6, FColor32::Black );
		CommandList.Sphere3D( Position + FVector3( 0.0f, 0.45f, 0.0f ) - Direction * 0.05f, 0.4f, 14, ShellColor );
		return;
	}
	// こもった甲羅（すべっているときは、縁の模様が回る）
	CommandList.Sphere3D( Position + FVector3( 0.0f, 0.3f, 0.0f ), 0.4f, 14, ShellColor );
	CommandList.Cone3D( Position + FVector3( 0.0f, 0.12f, 0.0f ), Position + FVector3( 0.0f, 0.0f, 0.0f ), 0.46f, 14, FColor32( 240, 220, 150 ) );
	if ( Mode == EMode::Sliding )
	{
		const float32 Angle = State->Time * 18.0f;
		CommandList.Sphere3D( Position + FVector3( NMath::Cos( Angle ) * 0.34f, 0.5f, NMath::Sin( Angle ) * 0.34f ), 0.07f, 6, FColor32( 20, 100, 44 ) );
	}
}
