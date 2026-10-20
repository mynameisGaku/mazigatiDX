#pragma once
#include "Library/Core/Collection/String.h"
#include "Library/Core/Log/LogSink.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// LogArchive
//   ログファイルの世代管理。ログを書き始める前に Prepare を呼ぶ。
//
//   Saved/Logs/project.log                       今回のログ
//   Saved/Logs/project_Backup_2026-10-16_23-19-14.log   前回までのログ（古いものは最大 MaxBackups 個まで）
//
//   Prepare( "Saved/Logs", "project" ) の動作:
//     1. project.log が既にあれば、それを古いログとみなして
//        project_Backup_<そのファイルの最終更新日時>.log に改名する（同じ名前があれば _1, _2 ... を付ける）
//     2. project_Backup_*.log が MaxBackups 個を超えていたら、古い順に削除する
//=============================================================================
namespace NLogArchive
{
	constexpr int32 DefaultMaxBackups = 25;

	struct FPrepareResult
	{
		bool bDirectoryReady = false;   // Directory を使える（作れた）
		FString BackupPath;             // 改名した先（今回は何もしなかった場合は空）
		bool bBackupFailed = false;     // 古いログを改名できなかった（ファイルが使用中など）
		int32 NumDeleted = 0;           // 上限を超えて削除したバックアップの数
		int32 NumBackups = 0;           // 整理後に残っているバックアップの数
	};

	FPrepareResult Prepare( const FString& Directory, const FString& Name, int32 MaxBackups = DefaultMaxBackups );

	//-------------------------------------------------------------------------
	// OpenLogFile
	//   Prepare をして、今回のログファイル <Name>.log を（他のプロセスが書き込めない形で）開く。
	//   アプリを複数同時に起動していて <Name>.log を別のインスタンスが使用中なら、<Name>_2.log、<Name>_3.log ... を順に試す
	//   （それぞれ独自のバックアップを持つ）。ログが混ざらず、先に起動した方のログも壊れない。
	//-------------------------------------------------------------------------
	struct FOpenResult
	{
		TSharedPtr<CFileLogSink> Sink;   // 開けなかったら空（ディレクトリを作れない・MaxInstances 個すべて使用中など）
		FString Path;                    // 開いたファイル
		int32 InstanceIndex = 0;         // 1 = <Name>.log、2 = <Name>_2.log ...
		FPrepareResult Archive;          // 開いたファイルについての世代管理の結果
	};

	constexpr int32 DefaultMaxInstances = 16;
	FOpenResult OpenLogFile( const FString& Directory, const FString& Name, int32 MaxBackups = DefaultMaxBackups, int32 MaxInstances = DefaultMaxInstances );
}
