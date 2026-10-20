#pragma once
#include "Library/Framework/Console/ConsoleHistory.h"
#include "Library/Framework/Console/ConsoleRegistry.h"
#include "Library/Framework/Console/ConsoleTypes.h"

//=============================================================================
// 組み込みのコマンド
//   Registry に、実行中のエンジンを調べる・操作するコマンドを登録する（すべて Keep される）。
//   コマンドは実行のときに GetAppSubsystem<T>() などで相手を探すので、登録は、エンジンを起動する前でも後でもよい。
//   エンジンが動いていなければ（確認のとき）、各コマンドは「使えない」と出力するだけで、落ちない。
//
//   すべてのコマンドに説明（Description）と分類（Category）が付いている（help に出る）。引数の説明は、必要なものだけ付いている。
//
//   分類: Console / Variables / Engine / Window / Scene / Objects / ECS / Assets / Audio / Profiler / Log / Info
//
//   一覧は、コンソールで help と入力するか、BuiltinCommands.cpp を見る。
//=============================================================================
namespace NConsole
{
	// 組み込みのコマンドと変数を登録する
	void RegisterBuiltinCommands( FConsoleRegistry& Registry );

	// 画面の窓・組み込みのコマンド（history）・起動時のスクリプトが共有する、アプリ全体の履歴と出力
	FConsoleHistory& GetGlobalHistory();
	FConsoleBuffer& GetGlobalOutput();

	// テキストファイル（1 行に 1 つのコマンド。# と // の行は注釈）のコマンドを順に実行する。
	// 読めなければ false（OutError に理由）。実行したコマンドの数は OutNumCommands（注釈・空行を除く）。
	// コマンドが失敗しても、残りは実行する（失敗した数は OutNumFailed）。exec の中の exec は 8 段まで
	bool ExecuteScriptFile( FConsoleRegistry& Registry, const FString& Path, IConsoleOutput& Output, FString& OutError, int32* OutNumCommands = nullptr, int32* OutNumFailed = nullptr );

	// スクリプトのファイルの探し方: そのまま（作業フォルダから）→ 実行ファイルの近く（親も）→ プロジェクトの Config フォルダ。見つかったパス（無ければ空）
	FString FindScriptFile( const FString& Path );
}
