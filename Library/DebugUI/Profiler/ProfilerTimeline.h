#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Profiler/IProfiler.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// FProfilerTimelineState
//   プロファイラーのタイムライン表示の状態（Unreal Insights のタイムラインに近い見方）。
//   記録した履歴はすべて残るので、数十万フレーム以上でも描画の重さが変わらないようにしてある
//   （画面に収まらない分は、フレームを 2 のべき乗個ずつまとめた「最大の時間」の表から描く）。
//
//   Continuous  複数のフレームを 1 本の時間軸に連続して並べる。
//               ホイール: 左右にスクロール / Ctrl+ホイール: カーソル位置を中心にズーム / ドラッグ: 左右に移動
//               クリック: サンプルを選ぶ / ダブルクリック: そのサンプル（フレーム）へズーム / Alt+ホイール: 縦スクロール
//               上のバーは全体の俯瞰（ドラッグで表示位置を動かす）。フレームの境目に線と番号が出る
//   Stacked     1 フレームを 1 段にして縦に積む（新しいものが上）。段の左端がそのフレームの開始。
//               遅いフレームだけ長く伸びるので、フレーム同士の違いを比べやすい
//               ホイール: 縦スクロール / Shift+ホイール: 左右にスクロール / Ctrl+ホイール: ズーム
//
//   Auto Scroll（bAutoScroll）: 既定ではオフ。画面が勝手に動かないようにしてある。
//   オンにすると、右端（Stacked は先頭）が新しいデータに追従する（LiveRefreshHz の頻度で更新する）。
//   横にスクロールするとオフになり、右端まで戻る / Auto Scroll ボタンを押すと、右端（最新）へ移ってオンになる。
//
//   スレッド: ジョブシステムのワーカーなど、メインスレッド以外が記録したサンプルは、Continuous ではメインスレッドの下に
//   スレッドごとの行で出る（Threads ボタンで表示するスレッドを選べる）。
//
//   ズームの限界は、記録されている一番短い命令の長さに合わせる（そこまで細かく見られる。最小はタイマーの 2 カウント）。
//=============================================================================
struct FProfilerTimelineThread
{
	uint32 ThreadId = 0;
	uint32 NameId = 0;              // スレッド名（文字列 ID）
	uint16 ViewMaxDepth = 0;        // 見えているフレームでの一番深いサンプルの深さ（行の高さを決める）
	bool bVisible = true;           // Threads ボタンで選ぶ
	bool bInView = false;           // 見えているフレームに記録がある
};

struct FProfilerTimelineState
{
	//-------------------------------------------------------------------------
	// 設定
	//-------------------------------------------------------------------------
	int32 Mode = 0;                     // 0: Continuous / 1: Stacked
	float32 LiveRefreshHz = 10.0f;      // Auto Scroll 中に表示を新しくする頻度
	int32 RowHeight = 18;               // サンプル 1 段の高さ (px)
	char Highlight[64] = {};            // 名前・分類・持ち主に含まれる文字。一致しないサンプルは暗く表示する
	bool bShowFrameSeparators = true;   // フレームの境目に線を引く
	bool bAutoScroll = false;           // 新しいデータに追従する（既定ではオフ）
	bool bShowThreads = true;           // メインスレッド以外のスレッドの行を出す

	//-------------------------------------------------------------------------
	// 表示範囲（モードごと。Continuous は記録の基準からの時間 (ms)、Stacked はフレーム開始からの時間 (ms)）
	//-------------------------------------------------------------------------
	float64 ViewStartMs[2] = { 0.0, 0.0 };
	float64 ViewSpanMs[2] = { 40.0, 16.0 };

	//-------------------------------------------------------------------------
	// 選択（クリックしたサンプル）
	//-------------------------------------------------------------------------
	uint64 SelectedFrameNumber = 0;
	uint32 SelectedThreadId = 0;        // 0 = メインスレッド
	int32 SelectedSampleIndex = INDEX_NONE;

	//-------------------------------------------------------------------------
	// 内部
	//-------------------------------------------------------------------------
	bool bInitialized = false;
	bool bStackedInitialized = false;
	bool bViewNeedsPlacement = true;    // 最初に見るとき（記録が作り直されたとき）、最新のフレームの位置に合わせる
	int64 BaseTick = 0;                 // 時間 0 に当たる tick（最初に見たフレームの開始）
	uint64 LastNewestFrameNumber = 0;   // 記録が消されて作り直されたことを見つけるための控え

	int64 LiveTick = 0;                 // Auto Scroll 中の表示を最後に新しくした時刻
	float64 LiveEndMs = 0.0;            // その時点の右端（Continuous）
	uint64 LiveFrameNumber = 0;         // その時点の最新フレーム（Stacked の先頭）
	uint64 StackedPinnedFrameNumber = 0;// Auto Scroll をしていないとき、Stacked の先頭に固定するフレーム
	bool bWasFollowing = false;
	float64 PanMinMs = 0.0;             // 今のモードで横に動かせる範囲（Continuous は記録の両端、Stacked は 0〜一番長いフレーム）。毎フレーム設定する
	float64 PanMaxMs = 0.0;
	float ScrollGrabPx = 0.0f;          // 横スクロールバーのつまみを掴んだ位置（つまみの左端からの距離）
	bool bScrollTopPending = false;     // Stacked: Auto Scroll に戻ったとき、先頭（最新）までスクロールし直す

	// フレームの時間 (ms) の最大値の表。Level 0 は 1 フレームごと、Level n は 2^n フレームずつの最大。
	// 添え字はフレーム番号 - PyramidBaseNumber。新しいフレームの分だけ足していくので、履歴が長くなっても描画の重さは変わらない
	TArray<TArray<float32>> PyramidLevels;
	uint64 PyramidBaseNumber = 0;
	uint64 PyramidNextNumber = 0;       // 次に表へ入れるフレーム番号
	float32 MaxFrameMsSeen = 0.0f;      // 記録の中で一番長いフレームの時間
	int64 MinSampleTicks = 0;           // 記録の中で一番短いサンプル（0 カウントを除く）の長さ。0 = まだ分からない
	float64 MinSpanMs = 0.002;          // ズームの限界（これより狭くしない）。MinSampleTicks から決める

	TArray<FProfilerTimelineThread> Threads;   // 見つかったメインスレッド以外のスレッド（見つかった順に固定。行が入れ替わらないように）

	uint64 FocusedFrameNumber = 0;      // 窓全体で選ばれたフレームのうち、最後に画面を合わせたもの
	uint64 PendingScrollFrameNumber = 0;// Stacked で、この段までスクロールしたい
};

namespace NDebugUI
{
	// タイムラインを描く（ImGui のウィンドウ / タブの中で呼ぶ）。
	//   SelectedFrameNumber: 窓全体で選ばれているフレーム（フレームグラフのクリックなど）。変わったらそこへ移動する（Auto Scroll は止まる）。
	//                        同じフレームをもう一度選び直したいときは、呼び出し側で State.FocusedFrameNumber = 0 にする
	void DrawProfilerTimeline( IProfiler& Profiler, FProfilerTimelineState& State, uint64 SelectedFrameNumber );
}
