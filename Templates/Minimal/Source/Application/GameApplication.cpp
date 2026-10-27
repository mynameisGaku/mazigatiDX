#include "GameApplication.h"

#include "Library/Core/Log/Log.h"
#include "Library/Framework/Scene/SceneManager.h"
#include "Source/Scene/PlayScene.h"
#include "Source/Scene/TitleScene.h"

DEFINE_LOG_CATEGORY_STATIC( LogGame, Log )

void CGameApplication::OnInit()
{
	// シーンに名前を付けて登録する。ChangeScene( "名前" ) で切り替える
	CSceneManager& Scenes = GetSceneManager();
	Scenes.RegisterScene<CTitleScene>( "Title" );
	Scenes.RegisterScene<CPlayScene>( "Play" );
	Scenes.ChangeScene( "Title" );

	LOG( LogGame, Log, "Game started" );
}
