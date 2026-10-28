#include "SampleScene.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"

void CSampleScene::OnTick( float32 DeltaTime )
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input != nullptr && Input->IsKeyPressed( EKey::Escape ) )
	{
		GetSceneManager()->ChangeScene( CSampleCatalog::GetBrowserSceneName() );
		return;
	}

	OnSampleTick( DeltaTime );
}

void CSampleScene::OnDraw()
{
	FImmediateDraw Screen( GetDrawExecutor() );
	OnSampleDraw( Screen );

	// 見出し。シーンの名前（登録した名前）から、一覧に出している名前を引く
	const FSampleInfo* Info = CSampleCatalog::Get().Find( GetName() );
	Screen.TextF( 16, 12, FColor32::White, "Samples > %s", Info != nullptr ? *Info->Title : *GetName() );
	Screen.Text( 16, 32, "[Esc] Back to the list", FColor32::Gray );
}

void CSampleScene::OnSampleTick( float32 DeltaTime )
{
}

void CSampleScene::OnSampleDraw( FImmediateDraw& Screen )
{
}
