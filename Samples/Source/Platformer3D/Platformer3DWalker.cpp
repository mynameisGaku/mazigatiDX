#include "Platformer3DWalker.h"

#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Platformer3D/Platformer3DState.h"

namespace
{
	constexpr float32 WalkerHalfWidth = 0.45f;
	constexpr float32 WalkerHeight = 0.9f;
	constexpr float32 ChaseDistance = 10.0f;
	constexpr float32 ChaseSpeed = 3.0f;
	constexpr float32 WanderSpeed = 1.2f;
}

CPlatformer3DWalker::CPlatformer3DWalker( const TSharedPtr<FPlatformer3DState>& InState, const FVector3& InFeetPosition )
	: CPlatformer3DEnemy( InState, InFeetPosition, WalkerHalfWidth, WalkerHeight )
{}

void CPlatformer3DWalker::OnStomped( const FVector3& PlayerPosition )
{
	Squash( 0.5f );
}

void CPlatformer3DWalker::OnTickActive( float32 DeltaTime )
{
	// 主人公が近くにいて、ほぼ同じ高さなら、追いかける。そうでなければ、うろうろ歩く
	FVector3 ToPlayer = State->PlayerPosition - Position;
	const float32 Flat = NMath::Sqrt( ToPlayer.x * ToPlayer.x + ToPlayer.z * ToPlayer.z );
	if ( Flat < ChaseDistance && Flat > 0.05f && NMath::Abs( ToPlayer.y ) < 2.0f && !State->bGoalReached )
	{
		Direction = FVector3( ToPlayer.x / Flat, 0.0f, ToPlayer.z / Flat );
		Walk( DeltaTime, ChaseSpeed, true );
		return;
	}
	Walk( DeltaTime, WanderSpeed, true );
}

void CPlatformer3DWalker::Record3D( FDrawContext& CommandList )
{
	if ( bSquashed )
	{
		CommandList.Cone3D( Position + FVector3( 0.0f, 0.12f, 0.0f ), Position, 0.55f, 14, FColor32( 150, 90, 40 ) );
		return;
	}
	RecordBlobShadow( CommandList, State->Level, Position, 0.5f );
	const FVector3 Right( Direction.z, 0.0f, -Direction.x );
	const bool bStep = ( static_cast<int32>( State->Time * 6.0f ) % 2 ) == 0;
	const float32 Step = bStep ? 0.12f : -0.12f;
	// 足
	CommandList.Sphere3D( Position + Right * 0.22f + Direction * Step + FVector3( 0.0f, 0.1f, 0.0f ), 0.13f, 8, FColor32( 60, 36, 20 ) );
	CommandList.Sphere3D( Position - Right * 0.22f - Direction * Step + FVector3( 0.0f, 0.1f, 0.0f ), 0.13f, 8, FColor32( 60, 36, 20 ) );
	// 体と、顔
	CommandList.Sphere3D( Position + FVector3( 0.0f, 0.5f, 0.0f ), 0.42f, 14, FColor32( 168, 100, 44 ) );
	CommandList.Sphere3D( Position + FVector3( 0.0f, 0.3f, 0.0f ) + Direction * 0.12f, 0.3f, 12, FColor32( 238, 206, 160 ) );
	CommandList.Sphere3D( Position + FVector3( 0.0f, 0.62f, 0.0f ) + Direction * 0.32f + Right * 0.16f, 0.1f, 8, FColor32::White );
	CommandList.Sphere3D( Position + FVector3( 0.0f, 0.62f, 0.0f ) + Direction * 0.32f - Right * 0.16f, 0.1f, 8, FColor32::White );
	CommandList.Sphere3D( Position + FVector3( 0.0f, 0.62f, 0.0f ) + Direction * 0.4f + Right * 0.16f, 0.05f, 6, FColor32::Black );
	CommandList.Sphere3D( Position + FVector3( 0.0f, 0.62f, 0.0f ) + Direction * 0.4f - Right * 0.16f, 0.05f, 6, FColor32::Black );
}
