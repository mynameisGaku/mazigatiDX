#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Console/ConsoleHistory.h"
#include "Library/Framework/Console/ConsoleRegistry.h"
#include "Library/Framework/Console/ConsoleTypes.h"

//=============================================================================
// コンソールの窓（ImGui）。実行中にコマンドを入力する。デバッグ限定（Debug ビルド。COG_DEBUG_CONSOLE が 1 のときだけ呼ぶ）。
//
//   入力すると、すぐ下に「予測変換」（コマンド名・引数の候補・同じ書き出しの履歴）が説明つきで出る。
//   コマンド名を打ち終えると、その使い方（いま入力している引数を強調）と、説明のある引数の説明が出る。
//
//   Up / Down   候補が出ているときは、候補を選ぶ。出ていなければ、履歴をたどる
//   Tab         選んだ候補を入れる。選んでいなければ、候補に共通の部分を入れる（無ければ、最初の候補を選ぶ）。入力が空なら、コマンドの一覧
//   Enter       候補を選んでいるときは、その候補を入れる。そうでなければ実行する
//   Esc         候補を閉じる。閉じていれば、入力を消す
//
//   FConsoleWindowState Console;                       // 持っておく
//   NDebugUI::DrawConsoleWindow( Console, &bShow );    // 毎フレーム（ImGui の NewFrame の後）
//
//   Registry / Output / History は、指定しなければアプリ全体のもの（FConsoleRegistry::Global() / NConsole::GetGlobalOutput() / GetGlobalHistory()）。
//=============================================================================
struct FConsoleSuggestion
{
	FString Text;               // 入れる文字列（引用符つきのことがある）
	FString Description;
	int32 ReplaceFrom = 0;      // 入力欄の、この位置から最後までを置き換える
	bool bAppendSpace = true;   // 入れたあとに空白を足す（次の引数を打ち始められるように）
	bool bFromHistory = false;
};

struct FConsoleWindowState
{
	FConsoleRegistry* Registry = nullptr;
	FConsoleBuffer* Output = nullptr;
	FConsoleHistory* History = nullptr;

	char Input[2048] = {};          // UTF-8
	char Filter[128] = {};          // 出力の絞り込み
	bool bFocusInput = true;        // 次のフレームで、入力欄にフォーカスを移す（開いたとき・実行したあと）
	bool bInputWasActive = false;   // 前のフレームで、入力欄が使われていたか（Esc を、こちらで処理するため）
	int32 InputGeneration = 0;      // 入力欄の中身を外から入れ替えるたびに増やす（入力欄の ID を変えて、バッファから読み直させる）
	bool bHistoryLoaded = false;

	// 予測変換
	FString LastInput;                          // 前のフレームの入力（変わったら、候補を作り直す）
	TArray<FConsoleSuggestion> Suggestions;
	int32 Selected = INDEX_NONE;                // 矢印で選んでいる候補
	bool bDismissed = false;                    // Esc で閉じた（次に入力するまで）
	bool bListAll = false;                      // 入力が空のまま Tab を押した（コマンドの一覧を出す）
	FString ProgrammaticText;                   // こちらで入れ替えた入力（入力欄の編集通知と区別する用）
	FString CommandName;                        // いま入力しているコマンド（使い方の表示用。名前を打ち終えたとき）
	int32 ActiveArg = INDEX_NONE;               // いま入力している引数の番号

	// 出力の表示
	uint64 LastOutputRevision = 0;
	bool bScrollToBottom = true;
};

namespace NDebugUI
{
	// 窓を描く（pOpen が false になったら閉じられた）
	void DrawConsoleWindow( FConsoleWindowState& State, bool* pOpen );

	// 1 行を、窓に入力したときと同じように実行する（入力の表示 "> ..."・実行・履歴への追加）
	EConsoleResult ExecuteInConsole( FConsoleWindowState& State, const FString& Line );

	// 履歴を Saved/ConsoleHistory.txt に保存・読み込み（窓が最初に描かれるときに読み込む）
	void LoadConsoleHistory( FConsoleWindowState& State );
	void SaveConsoleHistory( FConsoleWindowState& State );
}
