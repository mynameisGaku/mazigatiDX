#include "ConsoleWindow.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>

#include "Library/DebugUI/EditorCommon.h"
#include "Library/Framework/Console/BuiltinCommands.h"
#include "Library/Framework/Console/ConsoleParse.h"
#include "Library/Platform/FileSystem.h"

namespace
{
	constexpr int32 MaxSuggestionRows = 8;

	FConsoleRegistry& GetRegistry( FConsoleWindowState& S ) { return S.Registry != nullptr ? *S.Registry : FConsoleRegistry::Global(); }
	FConsoleBuffer& GetOutput( FConsoleWindowState& S ) { return S.Output != nullptr ? *S.Output : NConsole::GetGlobalOutput(); }
	FConsoleHistory& GetHistory( FConsoleWindowState& S ) { return S.History != nullptr ? *S.History : NConsole::GetGlobalHistory(); }

	FString GetHistoryPath() { return NFileSystem::FindProjectRoot() / "Saved" / "ConsoleHistory.txt"; }

	ImVec4 LineColor( EConsoleLine Kind )
	{
		switch ( Kind )
		{
		case EConsoleLine::Command: return ImVec4( 0.55f, 0.85f, 1.0f, 1.0f );
		case EConsoleLine::Warning: return ImVec4( 1.0f, 0.85f, 0.4f, 1.0f );
		case EConsoleLine::Error: return ImVec4( 1.0f, 0.45f, 0.4f, 1.0f );
		default: return ImGui::GetStyleColorVec4( ImGuiCol_Text );
		}
	}

	bool HasVisibleSuggestions( const FConsoleWindowState& S )
	{
		return !S.Suggestions.IsEmpty() && !S.bDismissed;
	}

	// 入力欄の中身を、外から入れ替える（次のフレームに反映される。入力欄にはフォーカスを戻す）
	void SetInputText( FConsoleWindowState& S, const FString& Line )
	{
		const FString Utf8 = Line.ToUtf8();
		std::snprintf( S.Input, sizeof( S.Input ), "%s", *Utf8 );
		S.ProgrammaticText = Line;
		S.bFocusInput = true;
		++S.InputGeneration;
	}

	FString ApplySuggestion( const FString& Line, const FConsoleSuggestion& Suggestion )
	{
		FString NewLine = Line.Left( Suggestion.ReplaceFrom ) + Suggestion.Text;
		if ( Suggestion.bAppendSpace ) NewLine += " ";
		return NewLine;
	}

	// 入力が変わったら、候補（予測変換）と、使い方の表示を作り直す
	void UpdateSuggestions( FConsoleWindowState& S )
	{
		const FString Line = FString::FromUtf8( S.Input );
		const FString Key = S.bListAll ? Line + "\x01" : Line;
		if ( Key == S.LastInput ) return;
		S.LastInput = Key;

		S.Suggestions.Empty();
		S.CommandName.Empty();
		S.ActiveArg = INDEX_NONE;
		S.Selected = INDEX_NONE;
		if ( GetHistory( S ).IsNavigating() ) return;   // 履歴をたどっている間は、候補を出さない（矢印で履歴を続けてたどれるように）
		if ( Line.TrimStartAndEnd().IsEmpty() && !S.bListAll ) return;

		const FConsoleCompletion Completion = GetRegistry( S ).Complete( Line );
		if ( Completion.Command != nullptr ) S.CommandName = Completion.Command->Name;
		S.ActiveArg = Completion.ActiveArg;

		for ( int32 i = 0; i < Completion.Candidates.Num(); ++i )
		{
			FConsoleSuggestion Suggestion;
			Suggestion.Text = Completion.Candidates[i];
			Suggestion.Description = Completion.Descriptions.IsValidIndex( i ) ? Completion.Descriptions[i] : FString();
			Suggestion.ReplaceFrom = Completion.ReplaceFrom;
			S.Suggestions.Add( Suggestion );
		}
		// すでに打ち終えている（候補が自分自身だけ）なら、候補は出さない。使い方の表示で足りる
		if ( S.Suggestions.Num() == 1 && Line.Mid( Completion.ReplaceFrom ).Equals( S.Suggestions[0].Text, ESearchCase::IgnoreCase ) ) S.Suggestions.Empty();

		// 同じ書き出しの履歴
		for ( const FString& Match : GetHistory( S ).FindMatches( Line, 4 ) )
		{
			FConsoleSuggestion Suggestion;
			Suggestion.Text = Match;
			Suggestion.Description = "history";
			Suggestion.ReplaceFrom = 0;
			Suggestion.bAppendSpace = false;
			Suggestion.bFromHistory = true;
			S.Suggestions.Add( Suggestion );
		}
	}

	//-------------------------------------------------------------------------
	// 入力欄のコールバック（履歴・補完）
	//-------------------------------------------------------------------------
	void SetCallbackText( ImGuiInputTextCallbackData* Data, FConsoleWindowState& S, const FString& Line )
	{
		const FString Utf8 = Line.ToUtf8();
		Data->DeleteChars( 0, Data->BufTextLen );
		Data->InsertChars( 0, *Utf8 );
		Data->CursorPos = Data->BufTextLen;
		Data->SelectionStart = Data->SelectionEnd = Data->CursorPos;
		S.ProgrammaticText = Line;
	}

	int InputCallback( ImGuiInputTextCallbackData* Data )
	{
		FConsoleWindowState& S = *static_cast<FConsoleWindowState*>( Data->UserData );
		FConsoleHistory& History = GetHistory( S );

		if ( Data->EventFlag == ImGuiInputTextFlags_CallbackEdit )
		{
			// 打ち込んで変わったとき（こちらで入れ替えた直後は除く）
			if ( FString::FromUtf8( Data->Buf ) != S.ProgrammaticText )
			{
				History.ResetNavigation();
				S.Selected = INDEX_NONE;
				S.bDismissed = false;
				S.bListAll = false;
			}
		}
		else if ( Data->EventFlag == ImGuiInputTextFlags_CallbackHistory )
		{
			const bool bUp = Data->EventKey == ImGuiKey_UpArrow;
			if ( HasVisibleSuggestions( S ) && !History.IsNavigating() )
			{
				// 候補を選ぶ（端で折り返す）
				const int32 Num = S.Suggestions.Num();
				if ( S.Selected == INDEX_NONE ) S.Selected = bUp ? Num - 1 : 0;
				else S.Selected = ( S.Selected + ( bUp ? Num - 1 : 1 ) ) % Num;
			}
			else
			{
				const FString Current = FString::FromUtf8( Data->Buf );
				const FString NewLine = bUp ? History.Previous( Current ) : History.Next( Current );
				SetCallbackText( Data, S, NewLine );
				S.Selected = INDEX_NONE;
			}
		}
		else if ( Data->EventFlag == ImGuiInputTextFlags_CallbackCompletion )
		{
			const FString Line = FString::FromUtf8( Data->Buf );
			if ( Line.TrimStartAndEnd().IsEmpty() )
			{
				S.bListAll = true;   // 入力が空なら、コマンドの一覧を出す
				return 0;
			}
			if ( HasVisibleSuggestions( S ) && S.Suggestions.IsValidIndex( S.Selected ) )
			{
				SetCallbackText( Data, S, ApplySuggestion( Line, S.Suggestions[S.Selected] ) );
				S.Selected = INDEX_NONE;
				return 0;
			}
			const FConsoleCompletion Completion = GetRegistry( S ).Complete( Line );
			if ( Completion.Candidates.IsEmpty() ) return 0;
			if ( Completion.Candidates.Num() == 1 )
			{
				SetCallbackText( Data, S, Line.Left( Completion.ReplaceFrom ) + Completion.Candidates[0] + " " );
				return 0;
			}
			if ( Completion.CommonPrefix.Len() > Line.Len() - Completion.ReplaceFrom ) SetCallbackText( Data, S, Line.Left( Completion.ReplaceFrom ) + Completion.CommonPrefix );
			else if ( HasVisibleSuggestions( S ) ) S.Selected = 0;
		}
		return 0;
	}

	//-------------------------------------------------------------------------
	// 画面の部品
	//-------------------------------------------------------------------------
	int32 CountInfoLines( const FConsoleCommandInfo* Info )
	{
		if ( Info == nullptr ) return 0;
		int32 Lines = 1 + ( Info->Description.IsEmpty() ? 0 : 1 );
		for ( const FConsoleArgDesc& Arg : Info->Args )
		{
			if ( !Arg.Description.IsEmpty() ) ++Lines;
		}
		return Lines;
	}

	// コマンドの使い方（いま入力している引数を強調）、コマンドの説明、説明のある引数の説明
	void DrawInfoPanel( const FConsoleCommandInfo& Info, int32 ActiveArg )
	{
		const ImVec4 Accent( 0.55f, 0.85f, 1.0f, 1.0f );
		const ImVec4 Highlight( 1.0f, 0.85f, 0.4f, 1.0f );
		const ImVec4 Normal = ImGui::GetStyleColorVec4( ImGuiCol_Text );
		const ImVec4 Dim = ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled );

		ImGui::TextColored( Accent, "%s", UI_TEXT( Info.Name ) );
		for ( int32 i = 0; i < Info.Args.Num(); ++i )
		{
			const FConsoleArgDesc& Arg = Info.Args[i];
			const FString Body = Arg.Name + ":" + Arg.TypeName;
			const FString Text = Arg.bOptional ? "[" + Body + "]" : "<" + Body + ">";
			ImGui::SameLine( 0.0f, 6.0f );
			ImGui::TextColored( i == ActiveArg ? Highlight : Normal, "%s", UI_TEXT( Text ) );
		}
		if ( !Info.Description.IsEmpty() ) ImGui::TextColored( Dim, "%s", UI_TEXT( Info.Description ) );
		for ( int32 i = 0; i < Info.Args.Num(); ++i )
		{
			const FConsoleArgDesc& Arg = Info.Args[i];
			if ( Arg.Description.IsEmpty() ) continue;   // 説明が無い引数は、詳しくは出さない
			const FString Head = Arg.Name + ( Arg.bOptional ? " (optional)" : "" ) + ": ";
			ImGui::TextColored( i == ActiveArg ? Highlight : Dim, "  %s%s", UI_TEXT( Head ), UI_TEXT( Arg.Description ) );
		}
	}
}

//=============================================================================
// 公開
//=============================================================================
void NDebugUI::LoadConsoleHistory( FConsoleWindowState& State )
{
	State.bHistoryLoaded = true;
	FILE* File = nullptr;
	const TArray<wchar_t> Wide = GetHistoryPath().ToWide();
	if ( _wfopen_s( &File, Wide.GetData(), L"rb" ) != 0 || File == nullptr ) return;
	std::string Bytes;
	char Buffer[4096];
	size_t Read = 0;
	while ( ( Read = fread( Buffer, 1, sizeof( Buffer ), File ) ) > 0 ) Bytes.append( Buffer, Read );
	fclose( File );
	if ( Bytes.size() >= 3 && static_cast<unsigned char>( Bytes[0] ) == 0xEF && static_cast<unsigned char>( Bytes[1] ) == 0xBB && static_cast<unsigned char>( Bytes[2] ) == 0xBF ) Bytes.erase( 0, 3 );

	TArray<FString> Lines;
	size_t Start = 0;
	while ( Start <= Bytes.size() )
	{
		size_t End = Bytes.find( '\n', Start );
		if ( End == std::string::npos ) End = Bytes.size();
		std::string Line = Bytes.substr( Start, End - Start );
		if ( !Line.empty() && Line.back() == '\r' ) Line.pop_back();
		if ( !Line.empty() ) Lines.Add( FString::FromUtf8( Line.c_str() ) );
		Start = End + 1;
	}
	GetHistory( State ).SetAll( Lines );
}

void NDebugUI::SaveConsoleHistory( FConsoleWindowState& State )
{
	const FString Path = GetHistoryPath();
	NFileSystem::EnsureDirectory( NFileSystem::FindProjectRoot() / "Saved" );
	FILE* File = nullptr;
	const TArray<wchar_t> Wide = Path.ToWide();
	if ( _wfopen_s( &File, Wide.GetData(), L"wb" ) != 0 || File == nullptr ) return;
	for ( const FString& Line : GetHistory( State ).GetAll() )
	{
		const FString Utf8 = Line.ToUtf8();
		fwrite( *Utf8, 1, static_cast<size_t>( Utf8.Len() ), File );
		fwrite( "\n", 1, 1, File );
	}
	fclose( File );
}

EConsoleResult NDebugUI::ExecuteInConsole( FConsoleWindowState& State, const FString& Line )
{
	FConsoleBuffer& Output = GetOutput( State );
	Output.Write( EConsoleLine::Command, "> " + Line );
	GetHistory( State ).Add( Line );
	const EConsoleResult Result = GetRegistry( State ).Execute( Line, Output );
	SaveConsoleHistory( State );
	State.bScrollToBottom = true;
	return Result;
}

void NDebugUI::DrawConsoleWindow( FConsoleWindowState& S, bool* pOpen )
{
	if ( !S.bHistoryLoaded ) LoadConsoleHistory( S );

	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	const float Width = std::min( 860.0f, MainViewport->Size.x - 40.0f );
	const float Height = std::min( 460.0f, MainViewport->Size.y - 60.0f );
	ImGui::SetNextWindowPos( ImVec2( MainViewport->Pos.x + 20.0f, MainViewport->Pos.y + MainViewport->Size.y - Height - 20.0f ), ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( Width, Height ), ImGuiCond_FirstUseEver );
	if ( !ImGui::Begin( "Console###Console", pOpen ) )
	{
		ImGui::End();
		return;
	}

	FConsoleRegistry& Registry = GetRegistry( S );
	FConsoleBuffer& Output = GetOutput( S );

	// ツールバー
	if ( ImGui::Button( "Clear" ) )
	{
		Output.Clear();
		S.bScrollToBottom = true;
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Copy" ) ) ImGui::SetClipboardText( *Output.ToText().ToUtf8() );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( 200.0f );
	ImGui::InputTextWithHint( "##Filter", "filter output", S.Filter, sizeof( S.Filter ) );
	ImGui::SameLine();
	ImGui::TextDisabled( "Tab: complete   Up/Down: pick a suggestion or browse history   Enter: run" );

	UpdateSuggestions( S );

	// 下の部分（使い方・候補・入力欄）の高さ
	const ImGuiStyle& Style = ImGui::GetStyle();
	const float LineHeight = ImGui::GetTextLineHeightWithSpacing();
	const FConsoleCommandInfo* Info = S.CommandName.IsEmpty() ? nullptr : Registry.Find( S.CommandName );
	const int32 InfoLines = CountInfoLines( Info );
	const bool bShowSuggestions = HasVisibleSuggestions( S );
	const int32 NumSuggestions = S.Suggestions.Num();
	const int32 SuggestionRows = bShowSuggestions ? std::min( NumSuggestions, MaxSuggestionRows ) + ( NumSuggestions > MaxSuggestionRows ? 1 : 0 ) : 0;
	float Footer = ImGui::GetFrameHeightWithSpacing() + Style.ItemSpacing.y;
	if ( InfoLines > 0 ) Footer += InfoLines * LineHeight + Style.ItemSpacing.y * 2.0f + 2.0f;
	if ( SuggestionRows > 0 ) Footer += SuggestionRows * LineHeight + Style.ItemSpacing.y * 2.0f + 2.0f;

	//-------------------------------------------------------------------------
	// 出力
	//-------------------------------------------------------------------------
	ImGui::BeginChild( "##ConsoleOutput", ImVec2( 0.0f, -Footer ), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar );
	const bool bAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f;
	{
		TArray<int32> Visible;
		const bool bFilter = S.Filter[0] != '\0';
		const FString FilterText = bFilter ? FString::FromUtf8( S.Filter ) : FString();
		if ( bFilter )
		{
			for ( int32 i = 0; i < Output.Num(); ++i )
			{
				if ( Output.Get( i ).Text.Contains( FilterText, ESearchCase::IgnoreCase ) ) Visible.Add( i );
			}
		}
		const int32 Count = bFilter ? Visible.Num() : Output.Num();

		ImGuiListClipper Clipper;
		Clipper.Begin( Count );
		while ( Clipper.Step() )
		{
			for ( int32 Row = Clipper.DisplayStart; Row < Clipper.DisplayEnd; ++Row )
			{
				const FConsoleLine& Line = Output.Get( bFilter ? Visible[Row] : Row );
				ImGui::PushStyleColor( ImGuiCol_Text, LineColor( Line.Kind ) );
				ImGui::TextUnformatted( UI_TEXT( Line.Text ) );
				ImGui::PopStyleColor();
			}
		}
		Clipper.End();

		if ( ImGui::BeginPopupContextWindow( "##ConsoleOutputMenu" ) )
		{
			if ( ImGui::MenuItem( "Copy all" ) ) ImGui::SetClipboardText( *Output.ToText().ToUtf8() );
			if ( ImGui::MenuItem( "Clear" ) )
			{
				Output.Clear();
				S.bScrollToBottom = true;
			}
			ImGui::EndPopup();
		}
	}
	// 新しい出力は、一番下を見ているときだけ追いかける（上を読んでいるときは動かさない）
	if ( Output.GetRevision() != S.LastOutputRevision )
	{
		if ( bAtBottom || S.bScrollToBottom ) ImGui::SetScrollHereY( 1.0f );
		S.LastOutputRevision = Output.GetRevision();
		S.bScrollToBottom = false;
	}
	ImGui::EndChild();

	//-------------------------------------------------------------------------
	// コマンドの使い方・引数の説明
	//-------------------------------------------------------------------------
	if ( Info != nullptr )
	{
		ImGui::Separator();
		DrawInfoPanel( *Info, S.ActiveArg );
	}

	//-------------------------------------------------------------------------
	// 予測変換（候補）
	//-------------------------------------------------------------------------
	if ( bShowSuggestions )
	{
		ImGui::Separator();
		float NameWidth = 0.0f;
		for ( const FConsoleSuggestion& Suggestion : S.Suggestions ) NameWidth = std::max( NameWidth, ImGui::CalcTextSize( UI_TEXT( Suggestion.Text ) ).x );
		const float DescriptionColumn = std::min( NameWidth + 28.0f, 360.0f );

		// 選んでいる候補が見える範囲を表示する
		int32 First = 0;
		if ( NumSuggestions > MaxSuggestionRows && S.Selected != INDEX_NONE ) First = std::clamp( S.Selected - MaxSuggestionRows + 1, 0, NumSuggestions - MaxSuggestionRows );
		const int32 Last = std::min( First + MaxSuggestionRows, NumSuggestions );
		int32 Clicked = INDEX_NONE;
		for ( int32 i = First; i < Last; ++i )
		{
			const FConsoleSuggestion& Suggestion = S.Suggestions[i];
			ImGui::PushID( i );
			if ( ImGui::Selectable( UI_TEXT( Suggestion.Text ), S.Selected == i, ImGuiSelectableFlags_NoAutoClosePopups, ImVec2( DescriptionColumn - 8.0f, 0.0f ) ) ) Clicked = i;
			if ( !Suggestion.Description.IsEmpty() )
			{
				ImGui::SameLine( DescriptionColumn );
				ImGui::TextDisabled( "%s", UI_TEXT( Suggestion.Description ) );
			}
			ImGui::PopID();
		}
		if ( NumSuggestions > MaxSuggestionRows ) ImGui::TextDisabled( "  %d - %d of %d (Up / Down to see more)", First + 1, Last, NumSuggestions );
		if ( Clicked != INDEX_NONE )
		{
			SetInputText( S, ApplySuggestion( FString::FromUtf8( S.Input ), S.Suggestions[Clicked] ) );
			S.Selected = INDEX_NONE;
		}
	}

	//-------------------------------------------------------------------------
	// 入力欄
	//-------------------------------------------------------------------------
	ImGui::AlignTextToFramePadding();
	ImGui::TextColored( ImVec4( 0.55f, 0.85f, 1.0f, 1.0f ), ">" );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( -FLT_MIN );
	if ( S.bFocusInput )
	{
		ImGui::SetKeyboardFocusHere();
		S.bFocusInput = false;
	}
	const ImGuiInputTextFlags Flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackEdit;
	const FString TextBeforeFrame = FString::FromUtf8( S.Input );
	ImGui::PushID( S.InputGeneration );
	const bool bEnter = ImGui::InputText( "##ConsoleInput", S.Input, sizeof( S.Input ), Flags, InputCallback, &S );
	const bool bInputActive = ImGui::IsItemActive();
	ImGui::PopID();

	// Esc: ImGui の入力欄は「編集をやめる（元に戻して、フォーカスを外す）」に使うので、こちらで受け直す。
	//   候補が出ているときは、候補を閉じるだけ（入力は、そのまま）。出ていなければ、入力を消す。どちらも、フォーカスは入力欄に戻す
	if ( S.bInputWasActive && ImGui::IsKeyPressed( ImGuiKey_Escape, false ) )
	{
		if ( HasVisibleSuggestions( S ) )
		{
			SetInputText( S, TextBeforeFrame );
			S.bDismissed = true;
		}
		else
		{
			SetInputText( S, FString() );
			GetHistory( S ).ResetNavigation();
			S.bDismissed = false;
		}
	}
	S.bInputWasActive = bInputActive;

	if ( bEnter )
	{
		const FString Line = FString::FromUtf8( S.Input );
		if ( HasVisibleSuggestions( S ) && S.Suggestions.IsValidIndex( S.Selected ) )
		{
			// 選んでいる候補を入れる（実行はしない）
			SetInputText( S, ApplySuggestion( Line, S.Suggestions[S.Selected] ) );
			S.Selected = INDEX_NONE;
		}
		else
		{
			S.Input[0] = '\0';
			S.bFocusInput = true;
			GetHistory( S ).ResetNavigation();
			S.bListAll = false;
			if ( !Line.TrimStartAndEnd().IsEmpty() ) ExecuteInConsole( S, Line );
		}
	}

	ImGui::End();
}
