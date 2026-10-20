#include "GameScene.h"

#include "Library/Core/Interfaces/Input/IGamepadInput.h"
#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Source/Object/OrbitMarker.h"
#include "Source/Object/PhysicsObjects.h"

DEFINE_LOG_CATEGORY_STATIC( LogGameScene, Log )

namespace
{
	constexpr float32 BallLifetime = 15.0f;
	constexpr int32 NumInitialBalls = 5;

	const FColor32 BallColors[] = { FColor32::Red, FColor32::Green, FColor32::Blue, FColor32::Yellow };
}

void CGameScene::OnEnter()
{
	LOG( LogGameScene, Log, "Game scene started (%d initial ball(s))", NumInitialBalls );

	// 画面幅 1280px = 25.6m。下端に床を置く
	SpawnObject<CPhysicsFloor>( FVector3( 12.8f, 0.5f, 0.0f ), FVector3( 12.0f, 0.5f, 2.0f ) )->SetName( "Floor" );

	for ( int32 i = 0; i < NumInitialBalls; ++i ) SpawnBall();

	// 親子関係のサンプル: 親が回ると子・孫が周回する
	TSharedPtr<COrbitMarker> Sun = SpawnObject<COrbitMarker>( FVector3( 20.0f, 9.0f, 0.0f ), 45.0f, 14, FColor32::Yellow );
	Sun->SetName( "Sun" );
	TSharedPtr<COrbitMarker> Planet = SpawnObject<COrbitMarker>( FVector3( 3.0f, 0.0f, 0.0f ), 180.0f, 9, FColor32::Blue );
	Planet->SetName( "Planet" );
	Planet->AttachToParent( Sun.Get() );
	TSharedPtr<COrbitMarker> Moon = SpawnObject<COrbitMarker>( FVector3( 1.0f, 0.0f, 0.0f ), 0.0f, 5, FColor32::White );
	Moon->SetName( "Moon" );
	Moon->AttachToParent( Planet.Get() );
}

void CGameScene::OnTick( float32 DeltaTime )
{
	bool bSpawn = false;
	bool bBackToTitle = false;

	if ( const IInput* Input = GetAppSubsystem<IInput>() )
	{
		bSpawn = Input->IsKeyPressed( EKey::Space );
		bBackToTitle = Input->IsKeyPressed( EKey::Escape );
	}

	if ( const IGamepadInput* Gamepads = GetAppSubsystem<IGamepadInput>() )
	{
		for ( const FGamepadInfo& Device : Gamepads->GetDevices() )
		{
			const FGamepadState* State = Gamepads->GetState( Device.Id );
			if ( State == nullptr ) continue;
			if ( State->IsPressed( EGamepadButton::South ) ) bSpawn = true;
			if ( State->IsPressed( EGamepadButton::Back ) )  bBackToTitle = true;
		}
	}

	if ( bSpawn ) SpawnBall();
	if ( bBackToTitle )
	{
		LOG( LogGameScene, Log, "Back to the title (after %.1f s, %d ball(s) spawned)", GetElapsedTime(), NumSpawned );
		GetSceneManager()->ChangeScene( "Title" );
	}
}

void CGameScene::OnDraw()
{
	DrawFormatString( 16, 40, FColor32::White.ToDxColor(), "GAME   [Space] Spawn ball   [Esc] Title   Objects: %d", GetNumObjects() );
}

void CGameScene::SpawnBall()
{
	// 乱数を使わず生成数から位置と色を決める（毎回同じ結果になる）
	const float32 X = 4.0f + static_cast<float32>( ( NumSpawned * 7 ) % 18 );
	const float32 Radius = 0.3f + 0.1f * static_cast<float32>( NumSpawned % 4 );
	const FColor32& Color = BallColors[NumSpawned % 4];

	SpawnObject<CPhysicsBall>( FVector3( X, 13.0f, 0.0f ), Radius, Color, BallLifetime );
	++NumSpawned;
}
