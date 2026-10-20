#pragma once
#include "Library/Core/Collection/String.h"
#include "Library/Core/Interfaces/Profiler/IProfiler.h"

//=============================================================================
// ProfilerExport
//   記録したフレームをファイルへ書き出す。
//
//   Chrome トレース形式 (JSON): chrome://tracing や https://ui.perfetto.dev にドラッグして開くと、
//   タイムライン・検索・集計がブラウザで使える。サンプルの Category / Owner / Instance も付く。
//
//   const FString Path = NProfilerExport::MakeDefaultTracePath();
//   NProfilerExport::WriteChromeTrace( Profiler, 0, -1, Path );
//=============================================================================
namespace NProfilerExport
{
	// <プロジェクトのフォルダ>/Saved/Profiler/<プロジェクト名>_<日時>.json
	FString MakeDefaultTracePath();

	// FirstFrame..LastFrame（IProfiler::GetFrame のインデックス。LastFrame < 0 で最後まで）を書き出す。成功したら true
	bool WriteChromeTrace( const IProfiler& Profiler, int32 FirstFrame, int32 LastFrame, const FString& Path );
}
