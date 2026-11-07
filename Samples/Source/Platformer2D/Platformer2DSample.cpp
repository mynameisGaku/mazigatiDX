#include "Platformer2DSample.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Common/PlatformerOptions.h"
#include "Source/Platformer2D/Platformer2DHud.h"
#include "Source/Platformer2D/Platformer2DLevelView.h"
#include "Source/Platformer2D/Platformer2DPlayer.h"
#include "Source/Platformer2D/Platformer2DShelled.h"
#include "Source/Platformer2D/Platformer2DState.h"
#include "Source/Platformer2D/Platformer2DWalker.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CPlatformer2DSample> Registrar( FSampleInfo { "Platformer2D", "Platformer2D: a side-scrolling action game", "Run, jump, stomp enemies and hit blocks. Mushroom = big, flower = fireballs, star = invincible. Reach the flag pole.", 90 } );

	// 描く順（小さいほうが奥）: 面 → アイテム → コイン → 敵 → 火の玉 → 主人公 → 表示
	constexpr int32 LevelOrder = 0;
	constexpr int32 EnemyOrder = 20;
	constexpr int32 PlayerOrder = 30;
	constexpr int32 HudOrder = 100;
}

void CPlatformer2DSample::OnEnter()
{
	State = MakeShared<FPlatformer2DState>();
	// 起動の引数（--autoplay、--form=big / fire、--star）
	const FPlatformerOptions Options = FPlatformerOptions::FromCommandLine();
	State->bAutoPlay = Options.bAutoPlay;
	Options.ApplyTo( State->Status );

	SpawnObject<CPlatformer2DLevelView>( State )->SetDrawOrder( LevelOrder );
	for ( const FTile2DSpawn& Spawn : State->Level.Spawns )
	{
		if ( Spawn.Kind == 's' )
		{
			SpawnObject<CPlatformer2DShelled>( State, Spawn.Position )->SetDrawOrder( EnemyOrder );
		}
		else
		{
			SpawnObject<CPlatformer2DWalker>( State, Spawn.Position )->SetDrawOrder( EnemyOrder );
		}
	}
	SpawnObject<CPlatformer2DPlayer>( State )->SetDrawOrder( PlayerOrder );
	SpawnObject<CPlatformer2DHud>( State )->SetDrawOrder( HudOrder );
}

void CPlatformer2DSample::OnSampleTick( float32 DeltaTime )
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input != nullptr && ( State->bGoalReached || State->bGameOver ) && Input->IsKeyPressed( EKey::Enter ) )
	{
		// もう一度（同じサンプルに入り直す）
		GetSceneManager()->ChangeScene( "Platformer2D" );
	}
}

void CPlatformer2DSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 64, "A / D or arrows: move   Shift: run   Space / Z: jump   X / J: fireball (with a flower)", FColor32::Gray );
}
