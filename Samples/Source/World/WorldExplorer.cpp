#include "WorldExplorer.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Source/World/WorldSampleState.h"

namespace
{
	constexpr float32 WalkSpeed = 4.5f;
	constexpr float32 RunSpeed = 8.0f;
	constexpr float32 PillarRadius = 0.9f;
	constexpr float32 PlayerRadius = 0.35f;

	// 庭の広さ（中心から、この距離まで）
	constexpr float32 YardHalfSize = 15.5f;

	constexpr float32 CameraDistance = 8.0f;
	constexpr float32 CameraHeight = 4.5f;
	constexpr float32 CameraTurnSpeed = 1.8f;
}

CWorldExplorer::CWorldExplorer( const TSharedPtr<FWorldSampleState>& InState )
	: State( InState )
{}

void CWorldExplorer::OnSpawn()
{
	ResetToStart();
}

void CWorldExplorer::ResetToStart()
{
	// タグ "PlayerStart" の置き物（定義のオブジェクト）の位置から始める。無ければ、原点
	Position = FVector3::Zero;
	const TSharedPtr<CGameObject> Start = State->Stage.FindFirst( "PlayerStart" );
	if ( Start.IsValid() )
	{
		if ( const CTransformComponent* Transform = Start->GetComponent<CTransformComponent>() )
		{
			Position = Transform->GetWorldTransform().Position;
		}
	}
	FacingYaw = 0.0f;
	State->CameraYaw = 0.0f;
	State->PlayerPosition = Position;
	UpdateCamera();
}

void CWorldExplorer::Tick( float32 DeltaTime )
{
	if ( DeltaTime > 1.0f / 30.0f )
	{
		DeltaTime = 1.0f / 30.0f;
	}
	const IInput* Input = GetAppSubsystem<IInput>();
	FVector2 Move;
	float32 Turn = 0.0f;
	bool bRun = false;
	if ( Input != nullptr )
	{
		Move.x = ( Input->IsKeyDown( EKey::D ) ? 1.0f : 0.0f ) - ( Input->IsKeyDown( EKey::A ) ? 1.0f : 0.0f );
		Move.y = ( Input->IsKeyDown( EKey::W ) ? 1.0f : 0.0f ) - ( Input->IsKeyDown( EKey::S ) ? 1.0f : 0.0f );
		Turn = ( ( Input->IsKeyDown( EKey::Right ) || Input->IsKeyDown( EKey::E ) ) ? 1.0f : 0.0f ) - ( ( Input->IsKeyDown( EKey::Left ) || Input->IsKeyDown( EKey::Q ) ) ? 1.0f : 0.0f );
		bRun = Input->IsKeyDown( EKey::LeftShift ) || Input->IsKeyDown( EKey::RightShift );
	}
	State->CameraYaw += Turn * CameraTurnSpeed * DeltaTime;

	// カメラから見た向きを、世界の向きにして、動く
	const float32 Yaw = State->CameraYaw;
	const FVector3 Forward( NMath::Sin( Yaw ), 0.0f, NMath::Cos( Yaw ) );
	const FVector3 Right( NMath::Cos( Yaw ), 0.0f, -NMath::Sin( Yaw ) );
	FVector3 Wanted = Right * Move.x + Forward * Move.y;
	bMoving = Wanted.LengthSquared() > 0.0001f;
	if ( bMoving )
	{
		Wanted.Normalize();
		FacingYaw = NMath::Atan2( Wanted.x, Wanted.z );
		Position += Wanted * ( ( bRun ? RunSpeed : WalkSpeed ) * DeltaTime );
	}

	// 庭の端と、柱（円）は、通れない
	Position.x = NMath::Clamp( Position.x, -YardHalfSize, YardHalfSize );
	Position.z = NMath::Clamp( Position.z, -YardHalfSize, YardHalfSize );
	for ( const FVector3& Pillar : State->PillarPositions )
	{
		FVector3 Away( Position.x - Pillar.x, 0.0f, Position.z - Pillar.z );
		const float32 Distance = Away.Length();
		const float32 Limit = PillarRadius + PlayerRadius;
		if ( Distance < Limit && Distance > 0.0001f )
		{
			Position += Away * ( ( Limit - Distance ) / Distance );
		}
	}
	State->PlayerPosition = Position;
	UpdateCamera();
}

void CWorldExplorer::UpdateCamera()
{
	const float32 Yaw = State->CameraYaw;
	const FVector3 Forward( NMath::Sin( Yaw ), 0.0f, NMath::Cos( Yaw ) );
	const FVector3 Look = Position + FVector3( 0.0f, 1.0f, 0.0f );
	FCamera Camera = FCamera::MakePerspective( Look - Forward * CameraDistance + FVector3( 0.0f, CameraHeight, 0.0f ), Look, NMath::PI / 3.0f );
	Camera.Near = 0.3f;
	Camera.Far = 200.0f;
	State->Camera = Camera;
}

void CWorldExplorer::Record3D( FDrawContext& CommandList )
{
	const FVector3 Facing( NMath::Sin( FacingYaw ), 0.0f, NMath::Cos( FacingYaw ) );
	const FVector3 Right( Facing.z, 0.0f, -Facing.x );
	const float32 Stride = bMoving ? 0.18f * NMath::Sin( State->Time * 14.0f ) : 0.0f;
	// 足元の影
	CommandList.Cone3D( Position + FVector3( 0.0f, 0.03f, 0.0f ), Position + FVector3( 0.0f, 0.005f, 0.0f ), 0.45f, 16, FColor32( 24, 28, 24 ) );
	// 脚・体・頭・ぼうし・鼻（向きが分かるように）
	CommandList.Sphere3D( Position + Right * 0.15f + Facing * Stride + FVector3( 0.0f, 0.12f, 0.0f ), 0.14f, 8, FColor32( 120, 60, 24 ) );
	CommandList.Sphere3D( Position - Right * 0.15f - Facing * Stride + FVector3( 0.0f, 0.12f, 0.0f ), 0.14f, 8, FColor32( 120, 60, 24 ) );
	CommandList.Capsule3D( Position + FVector3( 0.0f, 0.25f, 0.0f ), Position + FVector3( 0.0f, 0.55f, 0.0f ), 0.26f, 8, FColor32( 52, 92, 214 ) );
	CommandList.Capsule3D( Position + FVector3( 0.0f, 0.55f, 0.0f ), Position + FVector3( 0.0f, 0.8f, 0.0f ), 0.26f, 8, FColor32( 214, 52, 44 ) );
	const FVector3 Head = Position + FVector3( 0.0f, 1.05f, 0.0f );
	CommandList.Sphere3D( Head, 0.22f, 12, FColor32( 250, 208, 160 ) );
	CommandList.Cone3D( Head + FVector3( 0.0f, 0.26f, 0.0f ), Head + FVector3( 0.0f, 0.06f, 0.0f ), 0.23f, 12, FColor32( 214, 52, 44 ) );
	CommandList.Sphere3D( Head + Facing * 0.21f, 0.06f, 6, FColor32( 232, 170, 128 ) );
}
