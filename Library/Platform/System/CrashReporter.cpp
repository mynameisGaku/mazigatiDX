// SPDX-License-Identifier: MIT
#include "CrashReporter.h"

#include <signal.h>
#include <stdarg.h>

#include "Library/Core/File/File.h"
#include "Library/Core/Log/Assert.h"
#include "Library/Core/Version.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Process.h"
#include "Library/Platform/System/CommandLine.h"
#include "Library/Platform/System/FileSystem.h"
#include "Library/Platform/System/WindowsInclude.h"

#include <dbghelp.h>
#include <tlhelp32.h>

#pragma comment( lib, "dbghelp.lib" )

DEFINE_LOG_CATEGORY_STATIC( LogCrashReporter, Log )

namespace
{
	wchar_t GReportDirectory[512] = {};
	wchar_t GLogFilePath[512] = {};
	wchar_t GLastReportPath[600] = {};
	bool GInstalled = false;
	volatile LONG GCrashHandled = 0;   // 落ち方の受け口からの報告は、1 回だけ
	CRITICAL_SECTION GSymbolLock;
	bool GSymbolLockReady = false;
	bool GSymbolsReady = false;

	const char* ExceptionName( DWORD Code )
	{
		switch ( Code )
		{
		case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
		case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT";
		case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
		case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
		case EXCEPTION_FLT_INVALID_OPERATION: return "EXCEPTION_FLT_INVALID_OPERATION";
		case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
		case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
		case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
		case EXCEPTION_INT_OVERFLOW: return "EXCEPTION_INT_OVERFLOW";
		case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
		case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
		case 0xE06D7363: return "C++ exception";
		default: return "unknown exception";
		}
	}

	void AppendFormat( FString& Out, const char* Format, ... )
	{
		char Buffer[2048];
		va_list Args;
		va_start( Args, Format );
		NCString::FormatV( Buffer, sizeof( Buffer ), Format, Args );
		va_end( Args );
		Out += Buffer;
	}

	// DbgHelp を使う準備（1 回。シンボルは、必要になった段で読み込む）
	void EnsureSymbols()
	{
		if ( GSymbolsReady )
		{
			return;
		}
		SymSetOptions( SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS );
		GSymbolsReady = SymInitialize( GetCurrentProcess(), nullptr, TRUE ) != FALSE;
	}

	// アドレスの、モジュール名と、モジュールの先頭からの位置
	void DescribeModule( HANDLE Process, DWORD64 Address, FString& Out )
	{
		const DWORD64 Base = SymGetModuleBase64( Process, Address );
		char Name[MAX_PATH] = {};
		if ( Base != 0 && GetModuleFileNameA( reinterpret_cast<HMODULE>( Base ), Name, MAX_PATH ) > 0 )
		{
			const char* Slash = Name;
			for ( const char* Cursor = Name; *Cursor != '\0'; ++Cursor )
			{
				if ( *Cursor == '\\' || *Cursor == '/' )
				{
					Slash = Cursor + 1;
				}
			}
			AppendFormat( Out, "[%s+0x%llX]", Slash, static_cast<unsigned long long>( Address - Base ) );
		}
		else
		{
			Out += "[unknown module]";
		}
	}

	// Context から、呼び出し履歴をたどる（Thread は、Context のスレッド）。SkipFrames 段を飛ばして、MaxDepth 段まで
	FString WalkStack( HANDLE Process, HANDLE Thread, CONTEXT Context, int32 SkipFrames, int32 MaxDepth )
	{
		FString Out;
		STACKFRAME64 Frame = {};
		Frame.AddrPC.Offset = Context.Rip;
		Frame.AddrPC.Mode = AddrModeFlat;
		Frame.AddrFrame.Offset = Context.Rbp;
		Frame.AddrFrame.Mode = AddrModeFlat;
		Frame.AddrStack.Offset = Context.Rsp;
		Frame.AddrStack.Mode = AddrModeFlat;
		int32 Index = 0;
		int32 Written = 0;
		while ( Written < MaxDepth && StackWalk64( IMAGE_FILE_MACHINE_AMD64, Process, Thread, &Frame, &Context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr ) )
		{
			if ( Frame.AddrPC.Offset == 0 )
			{
				break;
			}
			if ( Index++ < SkipFrames )
			{
				continue;
			}
			alignas( SYMBOL_INFO ) char SymbolBuffer[sizeof( SYMBOL_INFO ) + 256] = {};
			SYMBOL_INFO* Symbol = reinterpret_cast<SYMBOL_INFO*>( SymbolBuffer );
			Symbol->SizeOfStruct = sizeof( SYMBOL_INFO );
			Symbol->MaxNameLen = 255;
			DWORD64 Displacement = 0;
			AppendFormat( Out, "  #%d 0x%016llX ", Written, static_cast<unsigned long long>( Frame.AddrPC.Offset ) );
			if ( SymFromAddr( Process, Frame.AddrPC.Offset, &Displacement, Symbol ) )
			{
				AppendFormat( Out, "%s + 0x%llX", Symbol->Name, static_cast<unsigned long long>( Displacement ) );
				IMAGEHLP_LINE64 Line = {};
				Line.SizeOfStruct = sizeof( Line );
				DWORD LineDisplacement = 0;
				if ( SymGetLineFromAddr64( Process, Frame.AddrPC.Offset, &LineDisplacement, &Line ) )
				{
					AppendFormat( Out, " (%s:%lu)", Line.FileName, static_cast<unsigned long>( Line.LineNumber ) );
				}
				Out += " ";
			}
			DescribeModule( Process, Frame.AddrPC.Offset, Out );
			Out += "\n";
			++Written;
		}
		return Out;
	}

	// ログファイルの、最後の部分（行の途中から始まらないように、最初の改行まで飛ばす）
	FString ReadLogTail()
	{
		if ( GLogFilePath[0] == L'\0' )
		{
			return FString();
		}
		const HANDLE Handle = CreateFileW( GLogFilePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
		if ( Handle == INVALID_HANDLE_VALUE )
		{
			return FString();
		}
		constexpr LONGLONG TailBytes = 16 * 1024;
		LARGE_INTEGER Size = {};
		GetFileSizeEx( Handle, &Size );
		LARGE_INTEGER Start = {};
		Start.QuadPart = Size.QuadPart > TailBytes ? Size.QuadPart - TailBytes : 0;
		SetFilePointerEx( Handle, Start, nullptr, FILE_BEGIN );
		TArray<char> Buffer;
		Buffer.SetNum( static_cast<int32>( TailBytes ) + 1 );
		DWORD Read = 0;
		ReadFile( Handle, Buffer.GetData(), static_cast<DWORD>( TailBytes ), &Read, nullptr );
		CloseHandle( Handle );
		Buffer[static_cast<int32>( Read )] = '\0';
		const char* Text = Buffer.GetData();
		if ( Start.QuadPart > 0 )
		{
			while ( *Text != '\0' && *Text != '\n' )
			{
				++Text;
			}
			if ( *Text == '\n' )
			{
				++Text;
			}
		}
		return FString( Text );
	}

	FString DescribeOs()
	{
		FString Out;
		typedef LONG( WINAPI * FRtlGetVersion )( OSVERSIONINFOW* );
		OSVERSIONINFOW Version = {};
		Version.dwOSVersionInfoSize = sizeof( Version );
		if ( const HMODULE Ntdll = GetModuleHandleW( L"ntdll.dll" ) )
		{
			if ( const FRtlGetVersion Function = reinterpret_cast<FRtlGetVersion>( GetProcAddress( Ntdll, "RtlGetVersion" ) ) )
			{
				Function( &Version );
			}
		}
		MEMORYSTATUSEX Memory = {};
		Memory.dwLength = sizeof( Memory );
		GlobalMemoryStatusEx( &Memory );
		SYSTEM_INFO Info = {};
		GetSystemInfo( &Info );
		AppendFormat( Out, "Windows %lu.%lu build %lu, %lu logical processor(s), physical memory %llu MB (available %llu MB)",
					  Version.dwMajorVersion, Version.dwMinorVersion, Version.dwBuildNumber, Info.dwNumberOfProcessors,
					  static_cast<unsigned long long>( Memory.ullTotalPhys / ( 1024 * 1024 ) ), static_cast<unsigned long long>( Memory.ullAvailPhys / ( 1024 * 1024 ) ) );
		return Out;
	}

	FString ListModules()
	{
		FString Out;
		const HANDLE Snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId() );
		if ( Snapshot == INVALID_HANDLE_VALUE )
		{
			return Out;
		}
		MODULEENTRY32W Module = {};
		Module.dwSize = sizeof( Module );
		if ( Module32FirstW( Snapshot, &Module ) )
		{
			do
			{
				AppendFormat( Out, "  0x%016llX  %8lu KB  %s\n", static_cast<unsigned long long>( reinterpret_cast<uintptr_t>( Module.modBaseAddr ) ), static_cast<unsigned long>( Module.modBaseSize / 1024 ), *FString( Module.szExePath ) );
			} while ( Module32NextW( Snapshot, &Module ) );
		}
		CloseHandle( Snapshot );
		return Out;
	}

	struct FCrashJob
	{
		const char* Reason = "";
		int32 SkipFrames = 0;                      // 呼んだ所の文脈のとき、報告を書く仕組み自身の段数（呼び出し履歴の先頭から飛ばす）
		EXCEPTION_POINTERS* Pointers = nullptr;   // 無ければ、Context（呼んだ所）を使う
		CONTEXT Context = {};
		EXCEPTION_RECORD Record = {};
		DWORD CrashThreadId = 0;
		wchar_t ReportPath[600] = {};
		wchar_t DumpPath[600] = {};
		bool bOk = false;
		bool bDumpOk = false;
	};

	// 報告を書く本体（別のスレッドで動く）
	DWORD WINAPI CrashThreadMain( LPVOID Parameter )
	{
		FCrashJob& Job = *static_cast<FCrashJob*>( Parameter );
		EnsureSymbols();
		const HANDLE Process = GetCurrentProcess();
		const HANDLE CrashThread = OpenThread( THREAD_ALL_ACCESS, FALSE, Job.CrashThreadId );

		SYSTEMTIME Time;
		GetLocalTime( &Time );
		wchar_t Base[600];
		swprintf_s( Base, L"%s/Crash_%04d-%02d-%02d_%02d-%02d-%02d_%lu", GReportDirectory, Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond, GetCurrentProcessId() );
		wchar_t TextPath[620];
		wchar_t DumpPath[620];
		// 同じ秒に、2 つ目の報告を書くとき（検査・ログの多重）は、_2, _3 ... を付けて、別のファイルにする
		for ( int32 Number = 1; Number < 1000; ++Number )
		{
			if ( Number == 1 )
			{
				swprintf_s( TextPath, L"%s.txt", Base );
				swprintf_s( DumpPath, L"%s.dmp", Base );
			}
			else
			{
				swprintf_s( TextPath, L"%s_%d.txt", Base, Number );
				swprintf_s( DumpPath, L"%s_%d.dmp", Base, Number );
			}
			if ( GetFileAttributesW( TextPath ) == INVALID_FILE_ATTRIBUTES )
			{
				break;
			}
		}

		const CONTEXT* Context = Job.Pointers != nullptr ? Job.Pointers->ContextRecord : &Job.Context;
		const EXCEPTION_RECORD* Record = Job.Pointers != nullptr ? Job.Pointers->ExceptionRecord : nullptr;

		FString Text;
		AppendFormat( Text, "%s crash report\nVersion: %s\n", NVersion::ProductName, NVersion::String );
		AppendFormat( Text, "Time: %04d-%02d-%02d %02d:%02d:%02d\n", Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond );
		AppendFormat( Text, "Reason: %s\n", Job.Reason );
		if ( Record != nullptr )
		{
			AppendFormat( Text, "Exception: 0x%08lX (%s) at 0x%016llX ", Record->ExceptionCode, ExceptionName( Record->ExceptionCode ), static_cast<unsigned long long>( reinterpret_cast<uintptr_t>( Record->ExceptionAddress ) ) );
			DescribeModule( Process, reinterpret_cast<DWORD64>( Record->ExceptionAddress ), Text );
			Text += "\n";
			if ( Record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && Record->NumberParameters >= 2 )
			{
				const char* Kind = Record->ExceptionInformation[0] == 0 ? "reading" : ( Record->ExceptionInformation[0] == 1 ? "writing" : "executing" );
				AppendFormat( Text, "Access violation %s address 0x%016llX\n", Kind, static_cast<unsigned long long>( Record->ExceptionInformation[1] ) );
			}
		}
		AppendFormat( Text, "Process: %lu, crashing thread: %lu\n", GetCurrentProcessId(), Job.CrashThreadId );
		AppendFormat( Text, "Executable: %s\n", *NFileSystem::GetExecutableDirectory() );
		AppendFormat( Text, "Working directory: %s\n", *NFileSystem::GetWorkingDirectory() );
		AppendFormat( Text, "Command line: %s\n", *NCommandLine::Get() );
#ifdef _DEBUG
		AppendFormat( Text, "Build: Debug x64\n" );
#else
		AppendFormat( Text, "Build: Release x64\n" );
#endif
		AppendFormat( Text, "System: %s\n", *DescribeOs() );
		AppendFormat( Text, "\nCall stack (crashing thread):\n" );
		// 例外の文脈なら、先頭から。呼んだ所の文脈（abort・CHECK など）なら、報告を書く仕組み自身の段（Job.SkipFrames）は飛ばす
		Text += WalkStack( Process, CrashThread != nullptr ? CrashThread : GetCurrentThread(), *Context, Job.Pointers != nullptr ? 0 : Job.SkipFrames, 64 );

		const FString LogTail = ReadLogTail();
		if ( !LogTail.IsEmpty() )
		{
			AppendFormat( Text, "\nLog (the end of the log file; lines still in the write buffer are not included):\n%s\n", *LogTail );
		}
		AppendFormat( Text, "\nModules:\n%s", *ListModules() );

		// ダンプ
		bool bDump = false;
		{
			const HANDLE DumpFile = CreateFileW( DumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
			if ( DumpFile != INVALID_HANDLE_VALUE )
			{
				EXCEPTION_POINTERS Fallback = {};
				EXCEPTION_RECORD FallbackRecord = {};
				CONTEXT FallbackContext = Job.Context;
				MINIDUMP_EXCEPTION_INFORMATION Exception = {};
				Exception.ThreadId = Job.CrashThreadId;
				Exception.ClientPointers = FALSE;
				if ( Job.Pointers != nullptr )
				{
					Exception.ExceptionPointers = Job.Pointers;
				}
				else
				{
					FallbackRecord.ExceptionCode = 0xE0000001;
					Fallback.ExceptionRecord = &FallbackRecord;
					Fallback.ContextRecord = &FallbackContext;
					Exception.ExceptionPointers = &Fallback;
				}
				bDump = MiniDumpWriteDump( Process, GetCurrentProcessId(), DumpFile,
										   static_cast<MINIDUMP_TYPE>( MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules ), &Exception, nullptr, nullptr ) != FALSE;
				CloseHandle( DumpFile );
			}
		}
		Job.bDumpOk = bDump;
		if ( bDump )
		{
			wcscpy_s( Job.DumpPath, DumpPath );
			AppendFormat( Text, "\nDump: %s\n", *FString( DumpPath ) );
		}
		else
		{
			AppendFormat( Text, "\nDump: could not be written (error %lu)\n", GetLastError() );
		}

		{
			const HANDLE TextFile = CreateFileW( TextPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
			if ( TextFile != INVALID_HANDLE_VALUE )
			{
				DWORD Written = 0;
				Job.bOk = WriteFile( TextFile, *Text, static_cast<DWORD>( Text.Len() ), &Written, nullptr ) != FALSE;
				CloseHandle( TextFile );
			}
		}
		if ( Job.bOk )
		{
			wcscpy_s( Job.ReportPath, TextPath );
		}
		if ( CrashThread != nullptr )
		{
			CloseHandle( CrashThread );
		}
		return 0;
	}

	// 落ちる前に、報告を書く。別のスレッドで書き、ここでは、終わるまで待つ（スタックを使い切っていても、書けるように）
	// SkipFrames: 呼んだ所の文脈を使うとき、呼び出し履歴の先頭から飛ばす段数（RunReport 自身を含む、報告を書く仕組みの段）。OutInfo には、書いたファイルを入れる
	FString RunReport( const char* Reason, EXCEPTION_POINTERS* Pointers, int32 SkipFrames, FCrashInfo* OutInfo )
	{
		if ( !GInstalled )
		{
			return FString();
		}
		// フォルダーが無ければ作る
		const FString Directory = FString( GReportDirectory );
		NFileSystem::EnsureDirectory( Directory );

		FCrashJob Job;
		Job.Reason = Reason;
		Job.SkipFrames = SkipFrames;
		Job.Pointers = Pointers;
		Job.CrashThreadId = GetCurrentThreadId();
		if ( Pointers == nullptr )
		{
			RtlCaptureContext( &Job.Context );
		}
		const HANDLE Thread = CreateThread( nullptr, 4 * 1024 * 1024, CrashThreadMain, &Job, 0, nullptr );
		if ( Thread == nullptr )
		{
			return FString();
		}
		WaitForSingleObject( Thread, 60000 );
		CloseHandle( Thread );
		if ( !Job.bOk )
		{
			return FString();
		}
		wcscpy_s( GLastReportPath, Job.ReportPath );
		if ( OutInfo != nullptr )
		{
			OutInfo->ReportPath = FString( Job.ReportPath );
			OutInfo->DumpPath = Job.bDumpOk ? FString( Job.DumpPath ) : FString();
		}
		return FString( Job.ReportPath );
	}

	// 設定されたコールバック（プロセスの終わりまで、壊さない: 終了中の落ち方でも、呼べるように）
	FCrashCallback& CrashCallback()
	{
		static FCrashCallback* Callback = new FCrashCallback();
		return *Callback;
	}

	//-------------------------------------------------------------------------
	// エンジンの後始末（AddEngineCrashHandler）
	//-------------------------------------------------------------------------
	struct FEngineHandlerEntry
	{
		const char* Name = nullptr;
		FEngineCrashHandler Handler = nullptr;
	};

	constexpr int32 MaxEngineHandlers = 16;
	FEngineHandlerEntry GEngineHandlers[MaxEngineHandlers];
	int32 GNumEngineHandlers = 0;

	int32 FindEngineHandler( const char* Name )
	{
		for ( int32 Index = 0; Index < GNumEngineHandlers; ++Index )
		{
			if ( NCString::Compare( GEngineHandlers[Index].Name, Name ) == 0 )
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	// 例外で落ちても、先へ進めるように、1 つずつ守って呼ぶ（この関数には、デストラクターを持つ物を置かない）
	bool CallEngineHandlerProtected( FEngineCrashHandler Handler, const FCrashInfo& Info )
	{
		__try
		{
			Handler( Info );
			return true;
		}
		__except ( EXCEPTION_EXECUTE_HANDLER )
		{
			return false;
		}
	}

	bool CallUserCallbackProtected( const FCrashInfo& Info )
	{
		__try
		{
			CrashCallback()( Info );
			return true;
		}
		__except ( EXCEPTION_EXECUTE_HANDLER )
		{
			return false;
		}
	}

	// マウスの拘束・非表示を戻す（落ちたあと、エラー報告のダイアログを操作できるように）
	void ReleaseInput()
	{
		ClipCursor( nullptr );
		ReleaseCapture();
		for ( int32 Guard = 0; Guard < 16 && ShowCursor( TRUE ) < 0; ++Guard )
		{
		}
	}

	// 報告を書き、入力を解放し、ログに残し、エンジンの後始末とコールバックを呼ぶ。SkipFrames は RunReport に渡す（この関数の中の段を含める）
	FCrashInfo HandleCrashInternal( const char* Reason, EXCEPTION_POINTERS* Pointers, int32 SkipFrames )
	{
		FCrashInfo Info;
		Info.Reason = Reason;
		if ( Pointers != nullptr && Pointers->ExceptionRecord != nullptr )
		{
			Info.bHasException = true;
			Info.ExceptionCode = static_cast<uint32>( Pointers->ExceptionRecord->ExceptionCode );
		}
		const FString Path = RunReport( Reason, Pointers, SkipFrames, &Info );
		if ( !Path.IsEmpty() )
		{
			// ログにも残す（ファイルへも出る）
			LOG( LogCrashReporter, Error, "Crash report written: %s", *Path );
			CLogManager::Get().Flush();
		}
		ReleaseInput();
		// エンジンの後始末（登録された順）。1 つが落ちても、次へ進む
		for ( int32 Index = 0; Index < GNumEngineHandlers; ++Index )
		{
			if ( !CallEngineHandlerProtected( GEngineHandlers[Index].Handler, Info ) )
			{
				LOG( LogCrashReporter, Error, "The crash handler '%s' crashed too (skipped)", GEngineHandlers[Index].Name );
			}
		}
		// ユーザーのコールバック（設定されていれば）
		if ( CrashCallback() && !CallUserCallbackProtected( Info ) )
		{
			LOG( LogCrashReporter, Error, "The crash callback crashed too (ignored)" );
		}
		CLogManager::Get().Flush();
		return Info;
	}

	// 落ち方の受け口からの報告（1 回だけ）
	void HandleCrashOnce( const char* Reason, EXCEPTION_POINTERS* Pointers )
	{
		if ( InterlockedCompareExchange( &GCrashHandled, 1, 0 ) != 0 )
		{
			return;
		}
		// 飛ばす段: RunReport・HandleCrashInternal・HandleCrashOnce・（abort のフック / シグナルの受け口）
		HandleCrashInternal( Reason, Pointers, 4 );
	}

	LONG WINAPI TopLevelExceptionFilter( EXCEPTION_POINTERS* Pointers )
	{
		HandleCrashOnce( "Unhandled exception", Pointers );
		return EXCEPTION_CONTINUE_SEARCH;
	}

	void AbortSignalHandler( int )
	{
		HandleCrashOnce( "abort()", nullptr );
	}

	void AbortHook( const char* Reason )
	{
		HandleCrashOnce( Reason, nullptr );
	}

	void InvalidParameterHandler( const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t )
	{
		HandleCrashOnce( "Invalid CRT parameter", nullptr );
		NProcess::Abort();
	}

	// 無限の再帰（スタックオーバーフロー）。わざとなので、コンパイラーの警告（C4717）は止める
#pragma warning( push )
#pragma warning( disable : 4717 )
	__declspec( noinline ) int RecurseForever( int Depth )
	{
		volatile char Padding[256];
		Padding[0] = static_cast<char>( Depth );
		return RecurseForever( Depth + 1 ) + Padding[0];
	}
#pragma warning( pop )
}

void NCrashReporter::Install( const FString& ReportDirectory )
{
	if ( !GSymbolLockReady )
	{
		InitializeCriticalSection( &GSymbolLock );
		GSymbolLockReady = true;
	}
	const TArray<wchar_t> Wide = ReportDirectory.Replace( "\\", "/" ).ToWide();
	wcsncpy_s( GReportDirectory, Wide.GetData(), _TRUNCATE );
	if ( GInstalled )
	{
		return;
	}
	GInstalled = true;
	// スタックオーバーフローの落ち方でも、受け口が動けるように、このスレッドのスタックの最後に、動ける分の余白を残してもらう
	ULONG StackGuarantee = 128 * 1024;
	SetThreadStackGuarantee( &StackGuarantee );
	SetUnhandledExceptionFilter( TopLevelExceptionFilter );
	signal( SIGABRT, AbortSignalHandler );
	_set_invalid_parameter_handler( InvalidParameterHandler );
	NProcess::SetAbortHook( AbortHook );
	NAssert::SetCallStackProvider( &NCrashReporter::CaptureCallStack );
}

void NCrashReporter::SetLogFilePath( const FString& LogFilePath )
{
	const TArray<wchar_t> Wide = LogFilePath.ToWide();
	wcsncpy_s( GLogFilePath, Wide.GetData(), _TRUNCATE );
}

bool NCrashReporter::IsInstalled()
{
	return GInstalled;
}

FString NCrashReporter::GetReportDirectory()
{
	return FString( GReportDirectory );
}

FString NCrashReporter::WriteReport( const char* Reason, void* ExceptionPointers )
{
	return RunReport( Reason, static_cast<EXCEPTION_POINTERS*>( ExceptionPointers ), 2, nullptr );
}

bool NCrashReporter::AddEngineCrashHandler( const char* Name, FEngineCrashHandler Handler )
{
	if ( Name == nullptr || Handler == nullptr )
	{
		return false;
	}
	const int32 Existing = FindEngineHandler( Name );
	if ( Existing != INDEX_NONE )
	{
		GEngineHandlers[Existing].Handler = Handler;
		return true;
	}
	if ( GNumEngineHandlers >= MaxEngineHandlers )
	{
		return false;
	}
	GEngineHandlers[GNumEngineHandlers].Name = Name;
	GEngineHandlers[GNumEngineHandlers].Handler = Handler;
	++GNumEngineHandlers;
	return true;
}

bool NCrashReporter::RemoveEngineCrashHandler( const char* Name )
{
	const int32 Existing = Name != nullptr ? FindEngineHandler( Name ) : INDEX_NONE;
	if ( Existing == INDEX_NONE )
	{
		return false;
	}
	for ( int32 Index = Existing; Index + 1 < GNumEngineHandlers; ++Index )
	{
		GEngineHandlers[Index] = GEngineHandlers[Index + 1];
	}
	--GNumEngineHandlers;
	GEngineHandlers[GNumEngineHandlers] = FEngineHandlerEntry();
	return true;
}

bool NCrashReporter::HasEngineCrashHandler( const char* Name )
{
	return Name != nullptr && FindEngineHandler( Name ) != INDEX_NONE;
}

int32 NCrashReporter::GetNumEngineCrashHandlers()
{
	return GNumEngineHandlers;
}

void NCrashReporter::SetCrashCallback( const FCrashCallback& Callback )
{
	CrashCallback() = Callback;
}

FCrashInfo NCrashReporter::HandleCrash( const char* Reason, void* ExceptionPointers )
{
	// 飛ばす段: RunReport・HandleCrashInternal・この関数
	return HandleCrashInternal( Reason, static_cast<EXCEPTION_POINTERS*>( ExceptionPointers ), 3 );
}

FString NCrashReporter::CaptureCallStack( int32 SkipFrames, int32 MaxDepth )
{
	if ( !GSymbolLockReady )
	{
		InitializeCriticalSection( &GSymbolLock );
		GSymbolLockReady = true;
	}
	EnterCriticalSection( &GSymbolLock );
	EnsureSymbols();
	CONTEXT Context = {};
	RtlCaptureContext( &Context );
	// 先頭の 1 段は、この関数自身
	const FString Stack = WalkStack( GetCurrentProcess(), GetCurrentThread(), Context, SkipFrames + 1, MaxDepth );
	LeaveCriticalSection( &GSymbolLock );
	return Stack;
}

FString NCrashReporter::GetLastReportPath()
{
	return GLastReportPath[0] != L'\0' ? FString( GLastReportPath ) : FString();
}

bool NCrashReporter::TriggerTestCrash( const FString& Kind )
{
	if ( Kind == "access" )
	{
		volatile int* Null = nullptr;
		*Null = 42;
		return true;
	}
	if ( Kind == "divide" )
	{
		volatile int Zero = 0;
		volatile int Result = 1 / Zero;
		return Result != 0;
	}
	if ( Kind == "stackoverflow" )
	{
		return RecurseForever( 0 ) != 0;
	}
	if ( Kind == "abort" )
	{
		NProcess::Abort();
	}
	if ( Kind == "check" )
	{
		CHECKF( Kind == "never", "crash test (--crash-test=check)" );
		return true;
	}
	if ( Kind == "ensure" )
	{
		ENSUREF( Kind == "never", "crash test (--crash-test=ensure): the program goes on" );
		return true;
	}
	return false;
}
