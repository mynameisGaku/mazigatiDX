// SPDX-License-Identifier: MIT
#pragma once

//=============================================================================
// NEngineCrashHandlers
//   エンジンが、クラッシュのときにする後始末（NCrashReporter::AddEngineCrashHandler で登録する）。NGameMain が、起動のときに Register を呼ぶ。
//   報告（Saved/Crashes）を書いたあと、ユーザーのコールバックの前に、この順で動く:
//     1. "Stop all sounds"        鳴っている音を全部止める（落ちたあと、エラー報告のダイアログが出ている間、音が鳴り続けないように）
//     2. "Save the profiler trace" プロファイラーに記録があれば、直近のフレームを Saved/Profiler/<名前>_<日時>_crash.json に書き出す（chrome://tracing で見られる）
//   マウスの拘束・非表示を戻すのは、Platform（NCrashReporter）が、いつも行う。
//   落ちたスレッドで動く。ゲームスレッド以外で落ちたときは、ゲームスレッドの仕事（音の停止）は、何もしない。
//=============================================================================
namespace NEngineCrashHandlers
{
	// 標準の後始末を登録する（何度呼んでもよい。同じ名前は置き換わる）
	void Register();

	// 名前（検査と、ログのため）
	constexpr const char* StopAllSoundsName = "Stop all sounds";
	constexpr const char* SaveProfilerTraceName = "Save the profiler trace";
}
