#include "TitleScene.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"

void CTitleScene::OnTick( float32 DeltaTime )
{
	// 入力はアプリのサブシステム（IInput）から取る
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input != nullptr && Input->IsKeyPressed( EKey::Enter ) )
	{
		GetSceneManager()->ChangeScene( "Play" );
	}
}

void CTitleScene::OnDraw()
{
	// 描いたものは、この関数を抜けるときに画面へ出る
	FImmediateDraw Screen( GetDrawExecutor() );
	Screen.Text( 580, 320, "TITLE", FColor32::White );
	Screen.Text( 540, 360, "Press Enter to start", FColor32::Gray );
}
