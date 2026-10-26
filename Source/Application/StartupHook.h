// SPDX-License-Identifier: NOASSERTION
#pragma once

class CSceneManager;

//=============================================================================
// NStartupHook
//   起動時に、シーンの登録と最初のシーンの選択へ割り込む窓口。
//   別のリポジトリで管理する自己確認（Source/SelfTest）が、静的な初期化で自分を差し込む。
//   GameApplication は差し込むものの有無を知らなくてよく、フォルダーを足し引きしても再コンパイルが要らない。
//
//   // 差し込む側（Source/SelfTest の .cpp）
//   namespace { const bool bAdded = ( NStartupHook::Add( &MyHook ), true ); }
//
//   // 呼ぶ側（GameApplication::OnInit）
//   if ( !NStartupHook::Run( Scenes ) ) Scenes.ChangeScene( "Title" );
//=============================================================================
namespace NStartupHook
{
	// 起動時に呼ぶ関数の型。シーンを登録し、最初のシーンを選んだら true を返す（選ばなければ false）。
	using FHook = bool ( * )( CSceneManager& Scenes );

	// 起動時に呼ぶ関数を追加する。静的変数の初期化から呼んでよい。
	void Add( FHook Hook );

	// 追加された関数を、追加された順に全部呼ぶ。どれかが最初のシーンを選んだら true を返す。
	// 入力: Scenes は登録先のシーン管理。
	bool Run( CSceneManager& Scenes );
}
