#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

#include "LeakReport.h"

#include "Library/Core/Types/CString.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

#ifdef _DEBUG
#include <dbghelp.h>
#pragma comment( lib, "dbghelp.lib" )

namespace
{
	long LeakBreakRequest = 0;
	HANDLE LeakReportHandle = INVALID_HANDLE_VALUE;

	void WriteLeakText( const char* Text )
	{
		DWORD Written = 0;
		if ( LeakReportHandle != INVALID_HANDLE_VALUE ) WriteFile( LeakReportHandle, Text, static_cast<DWORD>( NCString::Length( Text ) ), &Written, nullptr );
	}

	int LeakAllocHook( int AllocType, void*, size_t Size, int, long Request, const unsigned char*, int )
	{
		static bool bBusy = false;
		if ( AllocType != _HOOK_ALLOC || Request != LeakBreakRequest || bBusy ) return TRUE;
		bBusy = true;

		char Line[512];
		NCString::Format( Line, sizeof( Line ), "allocation #%ld (%zu bytes) was made here:\r\n", Request, Size );
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
			NCString::Format( Line, sizeof( Line ), "  %2u  %s  %s:%lu\r\n", i, bFound ? Symbol->Name : "?", bHasLine ? LineInfo.FileName : "", bHasLine ? LineInfo.LineNumber : 0UL );
			WriteLeakText( Line );
		}
		bBusy = false;
		return TRUE;
	}
}
#endif

void NLeakReport::Install()
{
#ifdef _DEBUG
	// 終了時（静的オブジェクトの破棄後）にメモリリークを出力ウィンドウへ表示する
	_CrtSetDbgFlag( _CrtSetDbgFlag( _CRTDBG_REPORT_FLAG ) | _CRTDBG_LEAK_CHECK_DF );

	// 環境変数 COG_LEAK_REPORT にパスがあれば、メモリリークの報告を、そのファイルへも書く。
	// ファイルは、終了時の報告のあとまで開けたままにする（閉じない）
	wchar_t LeakReportPath[MAX_PATH] = {};
	if ( GetEnvironmentVariableW( L"COG_LEAK_REPORT", LeakReportPath, MAX_PATH ) > 0 )
	{
		const HANDLE LeakReportFile = CreateFileW( LeakReportPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
		if ( LeakReportFile != INVALID_HANDLE_VALUE )
		{
			_CrtSetReportMode( _CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG );
			_CrtSetReportFile( _CRT_WARN, LeakReportFile );
			LeakReportHandle = LeakReportFile;
		}
	}
	wchar_t LeakBreakText[32] = {};
	if ( GetEnvironmentVariableW( L"COG_LEAK_BREAK", LeakBreakText, static_cast<DWORD>( sizeof( LeakBreakText ) / sizeof( LeakBreakText[0] ) ) ) > 0 )
	{
		LeakBreakRequest = _wtol( LeakBreakText );
		_CrtSetAllocHook( LeakAllocHook );
	}
#endif
}
