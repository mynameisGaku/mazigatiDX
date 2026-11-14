// SPDX-License-Identifier: MIT
#include "ImGuiViewportScheduler.h"
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Set.h"
#include "Library/Core/Profiler/ProfilerScope.h"
#include "Library/Core/Time/PlatformTime.h"
#include "VendorModules/ImGui/imgui.h"

void CImGuiViewportScheduler::Reset()
{
	LastPresentTicks.Reset();
}

void CImGuiViewportScheduler::Render( int32 MaxFPS, int32 MaxPresents )
{
	if ( !ImGui::GetCurrentContext() || !( ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable ) )
	{
		Reset();
		return;
	}
	ImGuiPlatformIO& Platform = ImGui::GetPlatformIO(); // 現在の文脈が持つ窓と公式描画の窓口。
	const int64 Now = NPlatformTime::GetTicks(); // 更新順を揃えるため、一呼出内では同じ時刻を使う。
	const float64 MinInterval = MaxFPS > 0 ? 1000.0 / MaxFPS : 0; // 各窓の表示間隔のミリ秒。
	// 表示時刻の古い窓から選ぶための候補。
	struct FCandidate
	{
		ImGuiViewport* Viewport = nullptr; // 公式実装が所有する表示先。
		int64 LastTicks = 0; // 未表示なら0で最優先にする。
	};
	TArray<FCandidate> Candidates; // この呼出で表示できる窓。
	TSet<uint32> Alive; // 閉じた窓の時刻を取り除くための番号。
	for ( int32 Index = 1; Index < Platform.Viewports.Size; ++Index )
	{
		ImGuiViewport* Viewport = Platform.Viewports[Index]; // 主画面はアプリがすでに描画済み。
		Alive.Add( Viewport->ID );
		if ( Viewport->Flags & ImGuiViewportFlags_IsMinimized )
		{
			continue;
		}
		const int64* Last = LastPresentTicks.Find( Viewport->ID ); // 最後の表示からの経過時間を調べる。
		if ( !Last || NPlatformTime::TicksToMs( Now - *Last ) >= MinInterval )
		{
			Candidates.Add( { Viewport, Last ? *Last : 0 } );
		}
	}
	Candidates.StableSort( []( const FCandidate& A, const FCandidate& B ) { return A.LastTicks < B.LastTicks; } );
	if ( MaxPresents > 0 && Candidates.Num() > MaxPresents )
	{
		Candidates.RemoveAt( MaxPresents, Candidates.Num() - MaxPresents );
	}
	for ( const FCandidate& Candidate : Candidates )
	{
		FProfileScope Scope( NProfileCategory::Platform, "ImGui", "Viewport.Render", Candidate.Viewport->ID ); // GPU実装ごとの描画時間。
		if ( Platform.Platform_RenderWindow )
		{
			Platform.Platform_RenderWindow( Candidate.Viewport, nullptr );
		}
		if ( Platform.Renderer_RenderWindow )
		{
			Platform.Renderer_RenderWindow( Candidate.Viewport, nullptr );
		}
	}
	for ( const FCandidate& Candidate : Candidates )
	{
		FProfileScope Scope( NProfileCategory::Platform, "ImGui", "Viewport.Present", Candidate.Viewport->ID ); // 表示待ちを描画時間から分ける。
		if ( Platform.Platform_SwapBuffers )
		{
			Platform.Platform_SwapBuffers( Candidate.Viewport, nullptr );
		}
		if ( Platform.Renderer_SwapBuffers )
		{
			Platform.Renderer_SwapBuffers( Candidate.Viewport, nullptr );
		}
		LastPresentTicks.Add( Candidate.Viewport->ID, Now );
	}
	LastPresentTicks.RemoveAll( [&Alive]( uint32 Id, int64 ) { return !Alive.Contains( Id ); } );
}
