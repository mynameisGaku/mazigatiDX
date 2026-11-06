// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"

//=============================================================================
// FProjectPaths
//   プロジェクトの「保存する物」の置き場所を、1 か所で決めるクラス。ログ・スクリーンショット・セーブデータ・プロファイラーの書き出し・
//   コンソールの履歴などが、バラバラに "Saved" を組み立てないように、ここから受け取る。
//   プロジェクトのルートを渡せるので、検査では一時フォルダを基準にして、本物の Saved を触らずに確かめられる。
//
//   FProjectPaths Paths;                                             // 既定: NFileSystem::FindProjectRoot()
//   const FString Logs = Paths.GetLogsDirectory();                   // <ルート>/Saved/Logs
//   FProjectPaths Test( "C:/Temp/Run1" );                           // 検査用に、別のルートを基準にする
//
//   - 返すのはパスの文字列だけ。フォルダは作らない（作るのは使う側。NFileSystem::EnsureDirectory）
//   - 区切りは '/'
//=============================================================================
class FProjectPaths
{
public:
	// プロジェクトのルートは NFileSystem::FindProjectRoot()（Config フォルダがあるフォルダ。見つからなければ作業ディレクトリ）
	FProjectPaths();

	explicit FProjectPaths( const FString& InProjectRoot );

	const FString& GetProjectRoot() const
	{
		return ProjectRoot;
	}

	// <ルート>/Config
	FString GetConfigDirectory() const;
	// <ルート>/Saved（保存する物の親）
	FString GetSavedDirectory() const;
	// <ルート>/Saved/Logs
	FString GetLogsDirectory() const;
	// <ルート>/Saved/Screenshots
	FString GetScreenshotsDirectory() const;
	// <ルート>/Saved/SaveGames（CSaveGameStorage の既定の置き場所）
	FString GetSaveGamesDirectory() const;
	// <ルート>/Saved/Profiler
	FString GetProfilerDirectory() const;
	// <ルート>/Saved/ConsoleHistory.txt
	FString GetConsoleHistoryFile() const;

private:
	FString ProjectRoot;
};
