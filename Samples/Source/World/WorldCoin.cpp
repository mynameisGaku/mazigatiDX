#include "WorldCoin.h"

#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/World/WorldSampleState.h"

namespace
{
	// 主人公と、この距離（水平）より近づいたら、集める
	constexpr float32 PickupRadius = 0.9f;
}

CWorldCoin::CWorldCoin( const TSharedPtr<FWorldSampleState>& InState, int32 InValue )
	: State( InState )
	, Value( InValue )
{}

FVector3 CWorldCoin::GetPlaceholderPosition() const
{
	const CGameObject* Parent = GetParent();
	const CTransformComponent* Transform = Parent != nullptr ? Parent->GetComponent<CTransformComponent>() : nullptr;
	return Transform != nullptr ? Transform->GetWorldTransform().Position : FVector3::Zero;
}

void CWorldCoin::Tick( float32 DeltaTime )
{
	Age += DeltaTime;
	const FVector3 Delta = State->PlayerPosition + FVector3( 0.0f, 0.6f, 0.0f ) - GetPlaceholderPosition();
	if ( Delta.LengthSquared() < PickupRadius * PickupRadius )
	{
		++State->CoinsCollected;
		State->ScoreCollected += Value;
		Destroy();
	}
}

void CWorldCoin::Record3D( FDrawContext& CommandList )
{
	// 浮いて揺れながら、回る（横幅を変えて、回って見せる）。点数の大きいコインは、大きく、赤みがかった金色
	const bool bBig = Value > 1;
	const FVector3 Center = GetPlaceholderPosition() + FVector3( 0.0f, 0.12f * NMath::Sin( Age * 3.0f ), 0.0f );
	const float32 Spin = NMath::Abs( NMath::Cos( Age * 4.0f ) );
	const float32 Size = bBig ? 0.5f : 0.34f;
	const FVector3 Half( Size * ( 0.12f + 0.88f * Spin ), Size, 0.05f );
	CommandList.Cube3D( Center - Half, Center + Half, bBig ? FColor32( 255, 170, 40 ) : FColor32( 250, 206, 40 ) );
}
