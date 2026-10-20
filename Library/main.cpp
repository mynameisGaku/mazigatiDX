/// <summary>
/// CoGフレームワーク
/// WinMain()から始まります
/// </summary>
/// <author>N.Hanai</author>
///
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

#include "Library/Core/Log/Log.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/FileSystem.h"
#include "Library/Platform/LogArchive.h"
#include "Source/Application/GameApplication.h"
#include "Source/Config/ScreenConfig.h"

#define CoGVersion (4.1)

DEFINE_LOG_CATEGORY_STATIC( LogMain, Log )


#ifdef _DEBUG
//=============================================================================
// メモリリークの調べ方（Debug だけ）
//   終了時の「Detected memory leaks!」の { } の中の番号（割り当ての通し番号）を、環境変数 COG_LEAK_BREAK に入れて、同じ操作で起動し直すと、
//   その番号の割り当てが起きた時点の呼び出し（関数名・行）を、COG_LEAK_REPORT のファイルに書く。番号は、起動のたびに同じとは限らない（スレッドや操作の順で変わる）。
//=============================================================================
#include <dbghelp.h>
#pragma comment( lib, "dbghelp.lib" )

namespace
{
	long LeakBreakRequest = 0;
	HANDLE LeakReportHandle = INVALID_HANDLE_VALUE;

	void WriteLeakText( const char* Text )
	{
		DWORD Written = 0;
		if ( LeakReportHandle != INVALID_HANDLE_VALUE ) WriteFile( LeakReportHandle, Text, static_cast<DWORD>( strlen( Text ) ), &Written, nullptr );
	}

	int LeakAllocHook( int AllocType, void*, size_t Size, int, long Request, const unsigned char*, int )
	{
		static bool bBusy = false;
		if ( AllocType != _HOOK_ALLOC || Request != LeakBreakRequest || bBusy ) return TRUE;
		bBusy = true;

		char Line[512];
		snprintf( Line, sizeof( Line ), "allocation #%ld (%zu bytes) was made here:\r\n", Request, Size );
		WriteLeakText( Line );
		void* Frames[48] = {};
		const USHORT NumFrames = CaptureStackBackTrace( 0, 48, Frames, nullptr );
		const HANDLE Process = GetCurrentProcess();
		SymSetOptions( SYMOPT_LOAD_LINES | SYMOPT_UNDNAME );
		SymInitialize( Process, nullptr, TRUE );
		alignas( SYMBOL_INFO ) char SymbolBuffer[sizeof( SYMBOL_INFO ) + 256] = {};
		SYMBOL_INFO* Symbol = reinterpret_cast<SYMBOL_INFO*>( SymbolBuffer );
		Symbol->SizeOfStruct = sizeof( SYMBOL_INFO );
		Symbol->MaxNameLen = 255;
		for ( USHORT i = 0; i < NumFrames; ++i )
		{
			DWORD64 Displacement = 0;
			const DWORD64 Address = reinterpret_cast<DWORD64>( Frames[i] );
			const bool bFound = SymFromAddr( Process, Address, &Displacement, Symbol ) != FALSE;
			IMAGEHLP_LINE64 LineInfo = {};
			LineInfo.SizeOfStruct = sizeof( LineInfo );
			DWORD LineDisplacement = 0;
			const bool bHasLine = SymGetLineFromAddr64( Process, Address, &LineDisplacement, &LineInfo ) != FALSE;
			snprintf( Line, sizeof( Line ), "  %2u  %s  %s:%lu\r\n", i, bFound ? Symbol->Name : "?", bHasLine ? LineInfo.FileName : "", bHasLine ? LineInfo.LineNumber : 0UL );
			WriteLeakText( Line );
		}
		bBusy = false;
		return TRUE;
	}
}
#endif

// プログラムは WinMain から始まります
int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow )
{
#ifdef _DEBUG
	// 終了時（静的オブジェクトの破棄後）にメモリリークを出力ウィンドウへ表示する
	_CrtSetDbgFlag( _CrtSetDbgFlag( _CRTDBG_REPORT_FLAG ) | _CRTDBG_LEAK_CHECK_DF );

	// 環境変数 COG_LEAK_REPORT にパスがあれば、メモリリークの報告を、そのファイルへも書く（デバッガーなしの自動実行でも、リークを調べられる）。
	// ファイルは、終了時の報告のあとまで開けたままにする（閉じない）
	char LeakReportPath[MAX_PATH] = {};
	if ( GetEnvironmentVariableA( "COG_LEAK_REPORT", LeakReportPath, MAX_PATH ) > 0 )
	{
		const HANDLE LeakReportFile = CreateFileA( LeakReportPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
		if ( LeakReportFile != INVALID_HANDLE_VALUE )
		{
			_CrtSetReportMode( _CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG );
			_CrtSetReportFile( _CRT_WARN, LeakReportFile );
			LeakReportHandle = LeakReportFile;
		}
	}
	char LeakBreakText[32] = {};
	if ( GetEnvironmentVariableA( "COG_LEAK_BREAK", LeakBreakText, sizeof( LeakBreakText ) ) > 0 )
	{
		LeakBreakRequest = atol( LeakBreakText );
		_CrtSetAllocHook( LeakAllocHook );
	}
#endif

	// 起動前（設定ファイルの読み込みやエンジンの初期化）のログも出せるよう、最初に登録する
	const TSharedPtr<ILogSink> ConsoleSink = MakeShared<CConsoleLogSink>();
	CLogManager::Get().AddSink( ConsoleSink );

	// ログはファイルにも残す: <プロジェクトのフォルダ>/Saved/Logs/<プロジェクト名>.log
	//   同じ名前のログが既にあれば古いログとして <プロジェクト名>_Backup_<日時>.log に改名し、古いものは最大 25 個まで残す。
	//   書き込みは起動時から行う（終了時にまとめて書くと、クラッシュしたときにログが残らないため）
	//   アプリを複数同時に起動しているときは、使用中のファイルを避けて <プロジェクト名>_2.log ... に書く（ログが混ざらない）
	const FString ProjectName = NFileSystem::GetExecutableName();
	const FString LogDirectory = NFileSystem::FindProjectRoot() / "Saved" / "Logs";
	const NLogArchive::FOpenResult LogFile = NLogArchive::OpenLogFile( LogDirectory, ProjectName, NLogArchive::DefaultMaxBackups );
	const TSharedPtr<CFileLogSink> FileSink = LogFile.Sink;
	if ( FileSink ) CLogManager::Get().AddSink( FileSink );
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
	LOG( LogMain, Log, "CoG Framework %.1f starting (%s, %s)", CoGVersion, BuildConfiguration, sizeof( void* ) == 8 ? "x64" : "x86" );
	LOG( LogMain, Log, "Executable: %s", *NFileSystem::GetExecutableDirectory() );
	LOG( LogMain, Log, "Working directory: %s", *NFileSystem::GetWorkingDirectory() );
	if ( FileSink )
	{
		LOG( LogMain, Log, "Log file: %s (backups: %d)", *LogFile.Path, LogFile.Archive.NumBackups );
		if ( LogFile.InstanceIndex > 1 ) LOG( LogMain, Log, "Another instance is already running, so this instance (#%d) writes to its own log file.", LogFile.InstanceIndex );
	}
	const int32 NumLogOverrides = CLogManager::Get().ApplyCommandLine( lpCmdLine );
	if ( NumLogOverrides > 0 ) LOG( LogMain, Log, "Log verbosity changed by the command line (%d category change(s))", NumLogOverrides );
	LOG( LogMain, Verbose, "Command line: %s", lpCmdLine );


	// 設定ファイルに無い項目・読み込みに失敗した場合はこの値を使う
	FWindowSettings DefaultSettings;
	DefaultSettings.Title      = "CoG Framework";
	DefaultSettings.Width      = 1280;
	DefaultSettings.Height     = 720;
	DefaultSettings.WindowMode = EWindowMode::Windowed;

	// 作業ディレクトリ -> exe のフォルダ -> その親 の順に探す（VS 実行・exe 直接起動・配布のどれでも見つかる）
	FWindowSettings Settings = DefaultSettings;
	const TOptional<FString> ScreenConfigPath = NFileSystem::FindFile( "Config/ScreenConfig.json" );
	if ( ScreenConfigPath.IsSet() )
	{
		LOG( LogMain, Log, "Screen config: %s", *ScreenConfigPath.GetValue() );
		Settings = FScreenConfig( ScreenConfigPath.GetValue() ).ToWindowSettings( DefaultSettings );
	}
	else
	{
		LOG( LogMain, Warning, "Config/ScreenConfig.json not found. Using default window settings." );
	}

	CEngine Engine;
	const int32 ExitCode = Engine.Run<CGameApplication>( Settings );
	LOG( LogMain, Log, "Exited with code %d", ExitCode );

	CLogManager::Get().Flush();
	CLogManager::Get().RemoveSink( FileSink );
	CLogManager::Get().RemoveSink( ConsoleSink );
	return ExitCode;
}
