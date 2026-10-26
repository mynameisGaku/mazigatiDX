// SPDX-License-Identifier: NOASSERTION
#include "StartupHook.h"

#include "Library/Core/Collection/Array.h"

namespace NStartupHook
{
	namespace
	{
		// 追加された関数の一覧。静的変数の初期化順に依らず使えるよう、初めて使うときに作る。
		TArray<FHook>& GetHooks()
		{
			static TArray<FHook> Hooks;
			return Hooks;
		}
	}

	// 起動時に呼ぶ関数を追加する。
	void Add( FHook Hook )
	{
		if ( Hook != nullptr ) GetHooks().Add( Hook );
	}

	// 追加された関数を順に呼ぶ。最初のシーンを選んだものがあれば true。
	bool Run( CSceneManager& Scenes )
	{
		bool bSelectedFirstScene = false;
		for ( const FHook Hook : GetHooks() )
		{
			if ( Hook( Scenes ) ) bSelectedFirstScene = true;
		}
		return bSelectedFirstScene;
	}
}
