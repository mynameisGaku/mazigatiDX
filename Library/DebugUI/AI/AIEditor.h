// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/DebugUI/AI/AIParamInspector.h"
#include "Library/DebugUI/AI/NodeGraph.h"
#include "Library/Framework/AI/AIIssue.h"
#include "Library/Framework/AI/Edit/BehaviorTreeEdit.h"
#include "Library/Framework/AI/Edit/BlackboardEdit.h"
#include "Library/Framework/AI/Edit/StateMachineEdit.h"
#include "Library/Json/JsonObject.h"

//=============================================================================
// AI エディタ
//   ブラックボード（.gxblackboard）・ステートマシン（.gxstate）・ビヘイビアツリー（.gxbt）を、画面で作って保存する ImGui のウィンドウ。
//   編集の中身は、画面に依存しない編集関数（NBlackboardEdit / NStateMachineEdit / NBehaviorTreeEdit）。ここは表示と入力の受け取りだけ。
//
//   - タブは 3 つ（Blackboard / State Machine / Behavior Tree）。タブごとに、開いているファイルと、元に戻す・やり直す（Ctrl+Z / Ctrl+Y）の履歴を持つ
//   - Blackboard: キーの一覧（名前・型・既定値・Trigger・Enum の選択肢）の編集と、使われている数。改名は、開いているステートマシン・ツリーの参照も直す
//   - Behavior Tree: 自動で並ぶノードのグラフ（クリックで選択、右クリックで追加・複製・削除など、ドラッグで動かす）と、Inspector
//     （部品のパラメータはスキーマから自動で作る・デコレーター・サービス）。座標は保存しない
//   - State Machine: 自由に置けるステートのグラフ（ドラッグで動かすと、座標が定義の Editor 欄に保存される。遷移はステートからステートへドラッグして作る）と、Inspector
//   - Issues: 部品の名前・ブラックボードのキーなどの問題の一覧（保存する前に直せる）
//   - 保存は CAssetSubsystem 経由。ほかのファイルを開く・新規のときは、未保存の変更を確認する
//   - グラフの描画は、AI デバッガと同じ部品（NodeGraph.h・AIGraphLayout.h）
//
//   ゲーム側は FAIEditorState を 1 つ持ち、開いている間、毎フレーム DrawAIEditor を呼ぶ（FAssetToolsState が持っている）。
//=============================================================================
enum class EAIEditorTab : uint8
{
	Blackboard,
	StateMachine,
	BehaviorTree,
};

// 選んでいる遷移（Machine = -1 は最上位、bAny は Any State の一覧、Index はその一覧の番号）
struct FAIEditorTransitionRef
{
	int32 Machine = -2;
	bool bAny = false;
	int32 Index = -1;

	bool IsValid() const { return Machine >= -1 && Index >= 0; }
	void Clear()
	{
		Machine = -2;
		bAny = false;
		Index = -1;
	}
};

struct FAIEditorState
{
	//-------------------------------------------------------------------------
	// 文書（タブごと）
	//-------------------------------------------------------------------------
	TBlackboardDocument Blackboard;
	FString BlackboardPath;                 // アセットのルートからのパス（空なら、まだ保存していない新規）
	TStateMachineDocument StateMachine;
	FString StateMachinePath;
	TBehaviorTreeDocument BehaviorTree;
	FString BehaviorTreePath;
	EAIEditorTab Tab = EAIEditorTab::BehaviorTree;
	// 起動時などに、タブを切り替えてほしいとき（1 回だけ効く）
	bool bSelectTabRequested = false;

	//-------------------------------------------------------------------------
	// 選択
	//-------------------------------------------------------------------------
	int32 SelectedKey = INDEX_NONE;
	int32 SelectedState = INDEX_NONE;
	FAIEditorTransitionRef SelectedTransition;
	int32 SelectedNode = INDEX_NONE;
	// ビヘイビアツリーのコピー（部分木の JSON）
	NlohmannJson Clipboard;
	// ビヘイビアツリーのノードのドラッグ（動かしているノード・動かし始めたか・押した位置の画面座標）
	int32 DragNode = INDEX_NONE;
	bool bDragging = false;
	ImVec2 DragStart = ImVec2( 0.0f, 0.0f );

	//-------------------------------------------------------------------------
	// グラフの表示
	//-------------------------------------------------------------------------
	FNodeGraphView StateMachineView;
	FNodeGraphView BehaviorTreeView;

	//-------------------------------------------------------------------------
	// ファイルの一覧と、選択肢
	//-------------------------------------------------------------------------
	TArray<FString> BlackboardFiles;
	TArray<FString> StateMachineFiles;
	TArray<FString> BehaviorTreeFiles;
	TArray<FString> ClipNames;              // アセットのルートの下のスプライトシート（.gxsprite）にあるクリップの名前
	double LastScanTime = -100.0;

	//-------------------------------------------------------------------------
	// 参照先のブラックボード（ステートマシン・ツリーが指すもの。開いている文書でなければ、読み込んだ定義を覚える）
	//-------------------------------------------------------------------------
	FBlackboardDefinition CachedBlackboard;
	FString CachedBlackboardPath;
	bool bCachedBlackboardValid = false;

	//-------------------------------------------------------------------------
	// 検証（文書か、参照先のブラックボードが変わったら、計算し直す）
	//-------------------------------------------------------------------------
	TArray<FAIIssue> Issues;
	EAIEditorTab IssuesTab = EAIEditorTab::BehaviorTree;
	uint32 IssuesDocumentRevision = ~0u;
	uint32 IssuesBlackboardRevision = ~0u;
	FString IssuesPath;
	bool bIssuesValid = false;

	//-------------------------------------------------------------------------
	// 部品のカタログ（AI/AIParts.gxparts。ゲームが書き出したもの）を読んだか（起動時に 1 回。File > Reload parts catalog で、読み直す）
	bool bPartsCatalogTried = false;

	// ファイル操作の入力
	//-------------------------------------------------------------------------
	FString OpenSelection;
	FString SaveAsPath;
	FString StatusMessage;
	bool bStatusIsError = false;
	bool bRequestOpenUnsavedPopup = false;
	enum class EPending : uint8 { None, Open, New } Pending = EPending::None;
	FString PendingPath;
	// ブラックボードのキーを消す確認（使われているとき）
	int32 PendingRemoveKey = INDEX_NONE;
	// ステートマシンのステートのドラッグ（動かしているステート・押したときのマウスのワールド座標・押したときの相対座標）
	int32 DragState = INDEX_NONE;
	ImVec2 DragStartWorld = ImVec2( 0.0f, 0.0f );
	ImVec2 DragStartRelative = ImVec2( 0.0f, 0.0f );
	// 右クリックしたワールド座標（「ここにステートを足す」用）
	ImVec2 MenuWorld = ImVec2( 0.0f, 0.0f );
	// Issues からのジャンプ・選んだものを、次に描くグラフの真ん中へ寄せる（1 回だけ）
	bool bFocusRequested = false;
	// 遷移を矢印のドラッグで作っているとき: 元のステート（INDEX_NONE なら作っていない）と、Any State の箱から作るときのマシン（-2 なら、Any State からではない）
	int32 LinkFromState = INDEX_NONE;
	int32 LinkFromAnyMachine = -2;
	// 「キーを足す」の入力
	FString NewKeyName = "Key";
	int32 NewKeyType = 0;
	// 名前を入力中の文字列（確定するまで、文書へ反映しない）
	FString NameBuffer;
	int32 NameBufferKey = INDEX_NONE;
};

namespace NDebugUI
{
	// ウィンドウを 1 つ出す。pOpen が null でなければ、× ボタンで false になる
	void DrawAIEditor( FAIEditorState& State, bool* pOpen = nullptr );

	// ファイルを開く（コマンドラインや他のウィンドウから呼ぶ用）。拡張子（.gxblackboard / .gxstate / .gxbt）でタブが決まる。失敗したら false（State.StatusMessage に理由）
	bool OpenAIEditorFile( FAIEditorState& State, const FString& AssetPath );
}
