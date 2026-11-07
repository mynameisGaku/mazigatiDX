#include "GameMain.h"

#include "Library/Core/Thread/GameThread.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Version.h"
#include "Library/Framework/Engine/EngineCrashHandlers.h"
#include "Library/Framework/Engine/ScreenConfig.h"
#include "Library/Platform/System/CommandLine.h"
#include "Library/Platform/System/CrashReporter.h"
#include "Library/Platform/System/FileSystem.h"
#include "Library/Platform/System/ProjectPaths.h"
#include "Library/Platform/System/LeakReport.h"
#include "Library/Platform/System/LogArchive.h"

DEFINE_LOG_CATEGORY_STATIC( LogMain, Log )

int32 NGameMain::Run( const FOptions& Options, const TCallable<int32( const FWindowSettings& )>& RunEngine )
{
	// 起動したスレッドが、ゲームスレッド（描画・アセットなど DxLib を触る仕事をするスレッド）
	NGameThread::Bind();
	NLeakReport::Install();
	// クラッシュ報告: 落ちたときに、原因・呼び出し履歴・ダンプ・直前のログを、Saved/Crashes に書く（Release でも有効）。起動の最初に入れる
	NCrashReporter::Install( FProjectPaths().GetCrashesDirectory() );
	NEngineCrashHandlers::Register();   // エンジン自身の、落ちたときの後始末（音を止める・プロファイラーの記録を書き出す）

	// 起動前（設定ファイルの読み込みやエンジンの初期化）のログも出せるよう、最初に登録する
	const TSharedPtr<ILogSink> ConsoleSink = MakeShared<CConsoleLogSink>();
	CLogManager::Get().AddSink( ConsoleSink );

	// ログはファイルにも残す: <プロジェクトのフォルダ>/Saved/Logs/<プロジェクト名>.log
	//   同じ名前のログが既にあれば古いログとして <プロジェクト名>_Backup_<日時>.log に改名し、古いものは最大 25 個まで残す。
	//   書き込みは起動時から行う（終了時にまとめて書くと、クラッシュしたときにログが残らないため）
	//   アプリを複数同時に起動しているときは、使用中のファイルを避けて <プロジェクト名>_2.log ... に書く（ログが混ざらない）
	const FString ProjectName = NFileSystem::GetExecutableName();
	const FString LogDirectory = FProjectPaths().GetLogsDirectory();
	const NLogArchive::FOpenResult LogFile = NLogArchive::OpenLogFile( LogDirectory, ProjectName, NLogArchive::DefaultMaxBackups );
	const TSharedPtr<CFileLogSink> FileSink = LogFile.Sink;
	if ( FileSink )
	{
		CLogManager::Get().AddSink( FileSink );
		NCrashReporter::SetLogFilePath( LogFile.Path );   // 報告に、ログの末尾を載せる
	}
	else LOG( LogMain, Warning, "Could not open a log file in %s. Logs are not saved.", *LogDirectory );
	if ( LogFile.Archive.bBackupFailed ) LOG( LogMain, Warning, "Could not rename the previous log file (is it open elsewhere?)" );
	if ( !LogFile.Archive.BackupPath.IsEmpty() ) LOG( LogMain, Verbose, "Previous log saved as %s", *LogFile.Archive.BackupPath );
	if ( LogFile.Archive.NumDeleted > 0 )
	{
		LOG( LogMain, Verbose, "Deleted %d old log file(s) (keeping the latest %d)", LogFile.Archive.NumDeleted, NLogArchive::DefaultMaxBackups );
	}

#ifdef _DEBUG
	const char* BuildConfiguration = "Debug";
#else
	const char* BuildConfiguration = "Release";
#endif
	LOG( LogMain, Log, "%s %s starting (%s, %s)", NVersion::ProductName, NVersion::String, BuildConfiguration, sizeof( void* ) == 8 ? "x64" : "x86" );
	LOG( LogMain, Log, "Executable: %s", *NFileSystem::GetExecutableDirectory() );
	LOG( LogMain, Log, "Working directory: %s", *NFileSystem::GetWorkingDirectory() );
	if ( FileSink )
	{
		LOG( LogMain, Log, "Log file: %s (backups: %d)", *LogFile.Path, LogFile.Archive.NumBackups );
		if ( LogFile.InstanceIndex > 1 ) LOG( LogMain, Log, "Another instance is already running, so this instance (#%d) writes to its own log file.", LogFile.InstanceIndex );
	}
	const FString CommandLine = NCommandLine::Get();
	const int32 NumLogOverrides = CLogManager::Get().ApplyCommandLine( *CommandLine );
	if ( NumLogOverrides > 0 ) LOG( LogMain, Log, "Log verbosity changed by the command line (%d category change(s))", NumLogOverrides );
	LOG( LogMain, Verbose, "Command line: %s", *CommandLine );
	// --crash-test=access / divide / stackoverflow / abort / check / ensure: わざと落とす（クラッシュ報告の確認用。ensure は、落ちずに続く）
	if ( const TOptional<FString> CrashTest = NCommandLine::GetValue( "crash-test" ); CrashTest.IsSet() )
	{
		LOG( LogMain, Warning, "--crash-test=%s: crashing on purpose", *CrashTest.Get( FString() ) );
		if ( !NCrashReporter::TriggerTestCrash( CrashTest.Get( FString() ) ) )
		{
			LOG( LogMain, Warning, "--crash-test: unknown kind '%s' (access / divide / stackoverflow / abort / check / ensure)", *CrashTest.Get( FString() ) );
		}
	}

	// 作業ディレクトリ -> exe のフォルダ -> その親 の順に探す（VS 実行・exe 直接起動・配布のどれでも見つかる）
	FWindowSettings Settings = Options.DefaultWindow;
	const TOptional<FString> ScreenConfigPath = NFileSystem::FindFile( Options.ScreenConfigFile );
	if ( ScreenConfigPath.IsSet() )
	{
		LOG( LogMain, Log, "Screen config: %s", *ScreenConfigPath.GetValue() );
		Settings = FScreenConfig( ScreenConfigPath.GetValue() ).ToWindowSettings( Options.DefaultWindow );
	}
	else
	{
		LOG( LogMain, Warning, "%s not found. Using default window settings.", *Options.ScreenConfigFile );
	}

	const int32 ExitCode = RunEngine( Settings );
	LOG( LogMain, Log, "Exited with code %d", ExitCode );

	CLogManager::Get().Flush();
	CLogManager::Get().RemoveSink( FileSink );
	CLogManager::Get().RemoveSink( ConsoleSink );
	return ExitCode;
}
