#include "Platformer3DWalker.h"

#include "Library/Framework/AI/Blackboard/Blackboard.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/Platformer3D/Platformer3DState.h"

namespace
{
	constexpr float32 WalkerHalfWidth = 0.45f;
	constexpr float32 WalkerHeight = 0.9f;
	constexpr float32 ChaseDistance = 10.0f;
}

CPlatformer3DWalker::CPlatformer3DWalker( const TSharedPtr<FPlatformer3DState>& InState, const FVector3& InFeetPosition )
	: CPlatformer3DEnemy( InState, InFeetPosition, WalkerHalfWidth, WalkerHeight, "Platformer/Chaser3D.gxstate" )
{}

void CPlatformer3DWalker::OnStomped( const FVector3& PlayerPosition )
{
	// すぐ、生きていないことにする（同じフレームに、ほかのものが、また踏まない）。つぶれる姿へは、ステートマシンが遷移する
	bSquashed = true;
	CPlatformer3DEnemy::OnStomped( PlayerPosition );
}

void CPlatformer3DWalker::Sense( CBlackboard& Blackboard )
{
	// 主人公が近くにいて、ほぼ同じ高さなら、追いかける（Wander → Chase の条件）
	const FVector3 ToPlayer = State->PlayerPosition - Position;
	const float32 Flat = NMath::Sqrt( ToPlayer.x * ToPlayer.x + ToPlayer.z * ToPlayer.z );
	const bool bNear = Flat < ChaseDistance && Flat > 0.05f && NMath::Abs( ToPlayer.y ) < 2.0f && !State->bGoalReached;
	Blackboard.SetBool( Blackboard.FindKey( "PlayerNear" ), bNear );
	Blackboard.SetBool( Blackboard.FindKey( "Alive" ), IsAlive() );
}

void CPlatformer3DWalker::StepChase( float32 DeltaTime, float32 Speed )
{
	const FVector3 ToPlayer = State->PlayerPosition - Position;
	const float32 Flat = NMath::Sqrt( ToPlayer.x * ToPlayer.x + ToPlayer.z * ToPlayer.z );
	if ( Flat > 0.05f )
	{
		Direction = FVector3( ToPlayer.x / Flat, 0.0f, ToPlayer.z / Flat );
	}
	StepWalk( DeltaTime, Speed, true );
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
