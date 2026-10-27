#include "TitleScene.h"

#include "Library/Core/Interfaces/Input/IGamepadInput.h"
#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/ImmediateDraw.h"
#include "Library/Math/Math.h"

DEFINE_LOG_CATEGORY_STATIC( LogTitleScene, Log )

void CTitleScene::OnTick( float32 DeltaTime )
{
	bool bStartPressed = false;
	if ( const IInput* Input = GetAppSubsystem<IInput>() )
	{
		bStartPressed = Input->IsKeyPressed( EKey::Enter );
	}
	if ( !bStartPressed )
	{
		if ( const IGamepadInput* Gamepads = GetAppSubsystem<IGamepadInput>() )
		{
			for ( const FGamepadInfo& Device : Gamepads->GetDevices() )
			{
				const FGamepadState* State = Gamepads->GetState( Device.Id );
				if ( State != nullptr && ( State->IsPressed( EGamepadButton::South ) || State->IsPressed( EGamepadButton::Start ) ) )
				{
					bStartPressed = true;
					break;
				}
			}
		}
	}

	if ( bStartPressed )
	{
		LOG( LogTitleScene, Log, "Start pressed after %.1f s. Starting the game.", GetElapsedTime() );
		GetSceneManager()->ChangeScene( "Game" );
	}
}

void CTitleScene::OnDraw()
{
	// 描いたものは、この関数を抜けるときに実行される（DxLib を直接呼ばない）
	FImmediateDraw Draw( GetDrawExecutor() );
	Draw.Text( 560, 320, "TITLE", FColor32::White );
	Draw.Text( 500, 360, "Press Enter or Gamepad South/Start", FColor32::Gray );
}
