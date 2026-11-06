// SPDX-License-Identifier: MIT
#pragma once

#include "Library/DebugUI/AI/AIEditor.h"

//=============================================================================
// AI エディタの内部（AIEditor*.cpp の間で共有する関数）。ほかからは使わない
//=============================================================================
namespace NAIEditor
{
	constexpr const char* BlackboardExtension = ".gxblackboard";
	constexpr const char* StateMachineExtension = ".gxstate";
	constexpr const char* BehaviorTreeExtension = ".gxbt";

	void SetStatus( FAIEditorState& State, const FString& Text, bool bError = false );

	// 名前（"AI/Enemy.gxblackboard" のようなパス）のブラックボード。開いている文書なら、その編集中の定義。無ければ nullptr
	const FBlackboardDefinition* FindBlackboard( FAIEditorState& State, const FString& Path );

	// Inspector の入力欄に渡す、選択肢（ブラックボードのキー・クリップ名）
	FAIInspectContext MakeInspectContext( FAIEditorState& State, const FString& BlackboardPath );

	// ファイルの一覧を作り直す（Force でなければ、前から 3 秒以内なら何もしない）
	void RescanFiles( FAIEditorState& State, bool bForce );

	// 選んでいるタブの検証の結果（部品の名前・キー・構造）の一覧を出す
	void DrawIssues( FAIEditorState& State );

	// ブラックボードのキーの改名や削除を、開いているステートマシン・ツリーへ反映する（それらが、この定義を指しているとき）
	void PropagateKeyRename( FAIEditorState& State, const FString& OldName, const FString& NewName );
	int32 CountKeyUsage( const FAIEditorState& State, const FString& KeyName );

	// 部品（登録した Task / Condition / Service / ステートの処理）の種類
	enum class EPartKind : uint8
	{
		StateAction,
		Condition,
		Task,
		Service,
	};

	// 部品の呼び出し（名前 + パラメータ）の編集欄。名前を一覧から選び、パラメータはスキーマから自動で作る。
	// 登録されていない名前は赤で出す。変更されたら true（名前を変えると、パラメータは空に戻る）。BlackboardPath は、キーの選択肢に使うブラックボード
	bool DrawCallEditor( FAIEditorState& State, const char* Label, FAICall& Call, EPartKind Kind, const FString& BlackboardPath );

	// 登録した部品を、カテゴリごとのサブメニューにして出す（追加のメニュー用）。選ばれた部品の名前を返す（選ばれなければ空）
	FString DrawPartMenuItems( EPartKind Kind );

	// 各タブの中身
	void DrawBlackboardTab( FAIEditorState& State );
	void DrawBehaviorTreeTab( FAIEditorState& State );
	void DrawStateMachineTab( FAIEditorState& State );
}
