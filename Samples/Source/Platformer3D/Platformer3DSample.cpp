#include "Platformer3DSample.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Source/Common/PlatformerOptions.h"
#include "Source/Common/PlatformerParts.h"
#include "Source/Platformer3D/Platformer3DHud.h"
#include "Source/Platformer3D/Platformer3DPlayer.h"
#include "Source/Platformer3D/Platformer3DShelled.h"
#include "Source/Platformer3D/Platformer3DState.h"
#include "Source/Platformer3D/Platformer3DView.h"
#include "Source/Platformer3D/Platformer3DWalker.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<CPlatformer3DSample> Registrar( FSampleInfo { "Platformer3D", "Platformer3D: a 3D action game", "The same rules in 3D: run, jump, stomp enemies, hit blocks. Mushroom = big, flower = fireballs, star = invincible. Reach the flag pole.", 91 } );

	// 描く順（小さいほうが奥）: 3D の面（アクターもここで記録される）→ 表示
	constexpr int32 ViewOrder = 0;
	constexpr int32 HudOrder = 100;
}

void CPlatformer3DSample::OnEnter()
{
	// ステートマシン（Samples/Assets/Platformer/*.gxstate）が呼ぶ、ステートの処理を登録する（2D と 3D で共通。1 回だけ）
	NPlatformerParts::Register();

	State = MakeShared<FPlatformer3DState>();

	// 起動の引数（--autoplay、--form=big / fire、--star）
	const FPlatformerOptions Options = FPlatformerOptions::FromCommandLine();
	State->bAutoPlay = Options.bAutoPlay;
	Options.ApplyTo( State->Status );

	SpawnObject<CPlatformer3DView>( State )->SetDrawOrder( ViewOrder );
	for ( const FBlock3DSpawn& Spawn : State->Level.Spawns )
	{
		if ( Spawn.Kind == 's' )
		{
			SpawnObject<CPlatformer3DShelled>( State, Spawn.Position );
		}
		else
		{
			SpawnObject<CPlatformer3DWalker>( State, Spawn.Position );
		}
	}
	SpawnObject<CPlatformer3DPlayer>( State );
	SpawnObject<CPlatformer3DHud>( State )->SetDrawOrder( HudOrder );
}

void CPlatformer3DSample::OnSampleTick( float32 DeltaTime )
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input != nullptr && ( State->bGoalReached || State->bGameOver ) && Input->IsKeyPressed( EKey::Enter ) )
	{
		// もう一度（同じサンプルに入り直す）
		GetSceneManager()->ChangeScene( "Platformer3D" );
	}
}

void CPlatformer3DSample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 64, "W A S D: move   Left / Right or Q / E: camera   Shift: run   Space: jump   X / J: fireball (with a flower)", FColor32::Gray );
}
