#include "LogSink.h"

#include "Library/Core/Thread/Mutex.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Utility.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "VendorModules/ImGui/imgui.h"

namespace
{
	WORD GetConsoleColor( ELogVerbosity Verbosity )
	{
		switch ( Verbosity )
		{
		case ELogVerbosity::Fatal:       return FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
		case ELogVerbosity::Error:       return FOREGROUND_RED | FOREGROUND_INTENSITY;
		case ELogVerbosity::Warning:     return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
		case ELogVerbosity::Display:     return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
		case ELogVerbosity::Verbose:
		case ELogVerbosity::VeryVerbose: return FOREGROUND_INTENSITY;
		default:                         return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
		}
	}

	ImVec4 GetImGuiColor( ELogVerbosity Verbosity )
	{
		switch ( Verbosity )
		{
		case ELogVerbosity::Fatal:       return ImVec4( 1.00f, 0.30f, 1.00f, 1.0f );
		case ELogVerbosity::Error:       return ImVec4( 1.00f, 0.35f, 0.35f, 1.0f );
		case ELogVerbosity::Warning:     return ImVec4( 1.00f, 0.85f, 0.30f, 1.0f );
		case ELogVerbosity::Display:     return ImVec4( 1.00f, 1.00f, 1.00f, 1.0f );
		case ELogVerbosity::Verbose:
		case ELogVerbosity::VeryVerbose: return ImVec4( 0.60f, 0.60f, 0.60f, 1.0f );
		default:                         return ImVec4( 0.85f, 0.85f, 0.85f, 1.0f );
		}
	}

	// 大文字小文字を区別しない部分一致
	//   FilterText は ImGui の入力バッファ（char 配列）なので、FString にせずバイト単位で比較する
	bool ContainsNoCase( const FString& Text, const char* Pattern )
	{
		const int32 PatternLength = static_cast<int32>( NCString::Length( Pattern ) );
		if ( PatternLength == 0 ) return true;
		if ( PatternLength > Text.Len() ) return false;

		for ( int32 i = 0; i + PatternLength <= Text.Len(); ++i )
		{
			if ( NCString::CompareNIgnoreCase( Text.GetData() + i, Pattern, static_cast<usize>( PatternLength ) ) == 0 ) return true;
		}
		return false;
	}
}

#pragma region CConsoleLogSink

CConsoleLogSink::CConsoleLogSink( bool bAllocConsole )
{
	if ( bAllocConsole && GetConsoleWindow() == nullptr )
	{
		bOwnsConsole = AllocConsole() != FALSE;
	}
}

CConsoleLogSink::~CConsoleLogSink()
{
	if ( bOwnsConsole )
	{
		FreeConsole();
	}
}

void CConsoleLogSink::Write( const FLogMessage& Message )
{
	// 末尾に改行を付ける（終端の直前に挿入）
	TArray<wchar_t> Line = Message.FormattedLine.ToWide();
	Line.Insert( L'\n', Line.Num() - 1 );

	// Visual Studio の出力ウィンドウ
	OutputDebugStringW( Line.GetData() );

	// コンソール
	HANDLE Console = GetStdHandle( STD_OUTPUT_HANDLE );
	if ( Console == nullptr || Console == INVALID_HANDLE_VALUE ) return;

	CONSOLE_SCREEN_BUFFER_INFO Info{};
	const bool bHasInfo = GetConsoleScreenBufferInfo( Console, &Info ) != FALSE;
	if ( !bHasInfo ) return;  // リダイレクト時などコンソールでない

	SetConsoleTextAttribute( Console, GetConsoleColor( Message.Verbosity ) );
	DWORD Written = 0;
	WriteConsoleW( Console, Line.GetData(), static_cast<DWORD>( Line.Num() - 1 ), &Written, nullptr );
	SetConsoleTextAttribute( Console, Info.wAttributes );
}

#pragma endregion

#pragma region CFileLogSink

CFileLogSink::CFileLogSink( const FString& FilePath, bool bAppend, bool bExclusiveWrite )
{
	File.Open( FilePath, bAppend ? FFile::EMode::Append : FFile::EMode::Write, false, bExclusiveWrite ? FFile::EShare::DenyWrite : FFile::EShare::AllowAll );
}

CFileLogSink::~CFileLogSink()
{
	Flush();
}

void CFileLogSink::Write( const FLogMessage& Message )
{
	if ( !File.IsOpen() ) return;

	// 行頭に時刻 [HH:MM:SS.mmm] を付ける
	char Stamp[32];
	NCString::Format( Stamp, sizeof( Stamp ), "[%02d:%02d:%02d.%03d] ", Message.Time.Hour, Message.Time.Minute, Message.Time.Second, Message.Time.Millisecond );

	// 1 行を 1 回の書き込みで出し、すぐファイルへ反映する。
	// 起動中のログを別のツールで読み続けられるうえ、クラッシュしても直前までのログが残る
	const FString& Line = Message.FormattedLine;
	File.WriteText( Stamp );
	File.Write( Line.GetData(), static_cast<usize>( Line.Len() ) );
	File.Write( "\r\n", 2 );
	File.Flush();
}

void CFileLogSink::Flush()
{
	File.Flush();
}

#pragma endregion

#pragma region CImGuiLogSink

CImGuiLogSink::CImGuiLogSink( int32 InMaxLines )
	: MaxLines( InMaxLines > 0 ? InMaxLines : 1 )
{
	Entries.Reserve( MaxLines );
}

void CImGuiLogSink::Write( const FLogMessage& Message )
{
	FScopedLock Lock( Mutex );
	FEntry Entry { Message.Verbosity, Message.FormattedLine };
	if ( Entries.Num() < MaxLines )
	{
		Entries.Add( MoveTemp( Entry ) );
	}
	else
	{
		// 満杯なら最も古い要素を上書き
		Entries[Head] = MoveTemp( Entry );
		Head = ( Head + 1 ) % MaxLines;
	}
}

void CImGuiLogSink::Clear()
{
	FScopedLock Lock( Mutex );
	Entries.Reset();
	Head = 0;
}

void CImGuiLogSink::Draw( const char* Title, bool* pOpen )
{
	if ( !ImGui::Begin( Title, pOpen ) )
	{
		ImGui::End();
		return;
	}

	// ---- ツールバー ----
	if ( ImGui::Button( "Clear" ) ) Clear();
	ImGui::SameLine();
	ImGui::Checkbox( "Auto-scroll", &bAutoScroll );
	ImGui::SameLine();

	static const char* VerbosityItems[] = { "Fatal", "Error", "Warning", "Display", "Log", "Verbose", "VeryVerbose" };
	int32 VerbosityIndex = FilterVerbosity - static_cast<int32>( ELogVerbosity::Fatal );
	ImGui::SetNextItemWidth( 120.0f );
	if ( ImGui::Combo( "##Verbosity", &VerbosityIndex, VerbosityItems, IM_ARRAYSIZE( VerbosityItems ) ) )
	{
		FilterVerbosity = VerbosityIndex + static_cast<int32>( ELogVerbosity::Fatal );
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth( -FLT_MIN );
	ImGui::InputTextWithHint( "##Filter", "Filter", FilterText, IM_ARRAYSIZE( FilterText ) );

	ImGui::Separator();

	// ---- ログ ----
	if ( ImGui::BeginChild( "##LogScroll", ImVec2( 0.0f, 0.0f ), false, ImGuiWindowFlags_HorizontalScrollbar ) )
	{
		FScopedLock Lock( Mutex );

		const bool bFiltered = FilterText[0] != '\0' || FilterVerbosity < static_cast<int32>( ELogVerbosity::All );
		if ( bFiltered )
		{
			for ( int32 i = 0; i < Entries.Num(); ++i )
			{
				const FEntry& Entry = GetEntry( i );
				if ( static_cast<int32>( Entry.Verbosity ) > FilterVerbosity ) continue;
				if ( !ContainsNoCase( Entry.Text, FilterText ) ) continue;

				ImGui::PushStyleColor( ImGuiCol_Text, GetImGuiColor( Entry.Verbosity ) );
				ImGui::TextUnformatted( Entry.Text.GetData(), Entry.Text.GetData() + Entry.Text.Len() );
				ImGui::PopStyleColor();
			}
		}
		else
		{
			ImGuiListClipper Clipper;
			Clipper.Begin( Entries.Num() );
			while ( Clipper.Step() )
			{
				for ( int i = Clipper.DisplayStart; i < Clipper.DisplayEnd; ++i )
				{
					const FEntry& Entry = GetEntry( i );
					ImGui::PushStyleColor( ImGuiCol_Text, GetImGuiColor( Entry.Verbosity ) );
					ImGui::TextUnformatted( Entry.Text.GetData(), Entry.Text.GetData() + Entry.Text.Len() );
					ImGui::PopStyleColor();
				}
			}
			Clipper.End();
		}

		// 最下部にいる間だけ新しいログに追従する
		if ( bAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() )
		{
			ImGui::SetScrollHereY( 1.0f );
		}
	}
	ImGui::EndChild();

	ImGui::End();
}

#pragma endregion

#pragma region VirtualDefaults

// 仮想関数は vtable 経由で呼ばれインライン展開できないので、ヘッダーではなくここで定義する

void ILogSink::Flush()
{
}

#pragma endregion
