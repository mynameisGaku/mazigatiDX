#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Interfaces/Profiler/IProfiler.h"
#include "Library/DebugUI/ProfilerTimeline.h"
#include "Library/Framework/Profiler/ProfilerAnalyzer.h"

//=============================================================================
// FProfilerWindowState
//   プロファイラーウィンドウの表示設定と、集計結果のキャッシュ。ウィンドウを出す側（ゲーム）が 1 つ持ち、毎フレーム渡す。
//
//   表示がちらつかないための決まり:
//     - 追従中（bFollowLatest）の表示は 0.25 秒ごとにしか更新しない（毎フレーム差し替えると読めない）
//     - 呼び出しツリーと集計表は、既定では直近 120 フレームの平均（1 フレームだけの値は揺れが大きい）
//     - フレームグラフは 0.1 秒ごとに更新する（毎フレーム 1 本ずつ流れるのを避ける）。Live でなければ動かさない
//     - タイムラインは Auto Scroll を押さない限り勝手に動かない（Live とは別）
//     - ツリーの開閉は、位置ではなく処理の道筋 (FProfileTreeNode::Id) で覚える
//=============================================================================
struct FProfilerWindowState
{
	//-------------------------------------------------------------------------
	// 表示するフレーム
	//-------------------------------------------------------------------------
	bool bFollowLatest = true;          // 新しいフレームを追いかける（フレームグラフをクリックすると解除される）
	uint64 SelectedFrameNumber = 0;     // bFollowLatest が false のとき見ているフレーム（履歴が流れても同じフレームを指す）

	//-------------------------------------------------------------------------
	// 範囲（Hierarchy / Stats タブ共通）
	//-------------------------------------------------------------------------
	int32 RangeMode = 2;                // 0: 選択中のフレーム / 1: 直近 30 / 2: 直近 120 / 3: 保存している全フレーム

	//-------------------------------------------------------------------------
	// 集計（Stats タブ）
	//-------------------------------------------------------------------------
	uint32 GroupBy = NProfileGroupBy::Name;
	char Filter[128] = {};
	int32 MaxDepth = -1;                // -1 で制限なし
	int32 SortColumn = 3;               // 既定は Self
	bool bSortAscending = false;

	//-------------------------------------------------------------------------
	// 呼び出しツリー（Hierarchy タブ）
	//-------------------------------------------------------------------------
	bool bMergeSiblings = true;         // 同じ親の下の同じ処理を 1 つにまとめる（範囲が選択中の 1 フレームのときだけ選べる）
	bool bMergeInstances = true;        // まとめるとき、同じ型の個体（オブジェクト）の違いを無視する
	// 列見出しのクリックで並べ替える（同じ親の下の兄弟どうし）。-1 は呼ばれた順（既定。安定）
	int32 TreeSortColumn = -1;
	bool bTreeSortAscending = false;
	float HideBelowUs = 0.0f;           // これより短いノードは隠す（マイクロ秒。1 フレームあたり）

	//-------------------------------------------------------------------------
	// タイムライン（Timeline タブ）
	//-------------------------------------------------------------------------
	FProfilerTimelineState Timeline;

	//-------------------------------------------------------------------------
	// カウンター（Counters タブ）
	//-------------------------------------------------------------------------
	char CounterFilter[128] = {};
	bool bCounterGraphs = true;         // 行ごとの推移グラフを出す

	//-------------------------------------------------------------------------
	// 操作
	//-------------------------------------------------------------------------
	int32 CaptureFrames = 120;
	FString StatusMessage;              // 書き出しの結果など（ウィンドウの下に出す）

	//-------------------------------------------------------------------------
	// 内部
	//-------------------------------------------------------------------------
	// 画面に出しているフレーム（追従中は 0.25 秒ごとに最新へ進む）
	uint64 DisplayFrameNumber = 0;
	int64 DisplayTick = 0;

	// フレームグラフ用の時間 (ms) の控え。0.1 秒ごとに作り直す。要素 i はフレーム番号 GraphFirstFrameNumber + i。
	// 履歴は長くなるので、グラフは直近のフレームだけ。Live でないときは右端 (GraphEndFrameNumber) を固定する
	TArray<float32> GraphMs;
	uint64 GraphFirstFrameNumber = 0;
	uint64 GraphEndFrameNumber = 0;
	int64 GraphTick = 0;

	// 集計結果のキャッシュ
	int32 RangeFirst = 0;
	int32 RangeLast = 0;
	FProfileFrameStats RangeStats;
	TArray<FProfileStatRow> Rows;
	TArray<int32> SortedRows;           // Rows の並べ替え結果（インデックス）
	TArray<FProfileTreeNode> TreeNodes;      // 画面に出す順（並べ替え後）
	TArray<FProfileTreeNode> TreeNodesBase;  // 呼ばれた順（BuildCallTree の結果）
	bool bTreeSortDirty = true;
	uint64 SettingsKey = 0;
	uint64 DataKey = 0;
	int64 CacheTick = 0;
	bool bSortDirty = true;
};

namespace NDebugUI
{
	// 記録のオン / オフ、記録の状態、フレームグラフ、各タブ（Hierarchy / Timeline / Stats / Counters）を 1 つのウィンドウに出す。
	// pOpen が null でなければ、ウィンドウの × ボタンで false になる
	void DrawProfilerWindow( IProfiler& Profiler, FProfilerWindowState& State, bool* pOpen = nullptr );
}
