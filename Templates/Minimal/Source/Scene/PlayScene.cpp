#include "PlayScene.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Source/Object/Player.h"

void CPlayScene::OnEnter()
{
	// オブジェクトは SpawnObject で置く。シーンが所有して、毎フレーム Tick / Draw する
	SpawnObject<CPlayer>( FVector2( 640.0f, 360.0f ) );
}

void CPlayScene::OnTick( float32 DeltaTime )
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input != nullptr && Input->IsKeyPressed( EKey::Escape ) )
	{
		GetSceneManager()->ChangeScene( "Title" );
	}
}

void CPlayScene::OnDraw()
{
	// オブジェクトの Draw() のあとに呼ばれるので、画面の手前に出る（説明の文字など）
	FImmediateDraw Screen( GetDrawExecutor() );
	Screen.Text( 16, 16, "Arrow keys / WASD: move    Esc: back to the title", FColor32::White );
}
