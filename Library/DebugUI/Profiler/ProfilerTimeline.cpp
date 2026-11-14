#include "ProfilerTimeline.h"
#include "ProfilerTimelineInternal.h"


#include "Library/Core/Collection/Map.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Profiler/ProfilerAnalyzer.h"
#include "Library/Math/MathUtils.h"
#include "VendorModules/ImGui/imgui.h"

using namespace NProfilerTimelineUI;

namespace NProfilerTimelineUI
{
	// 文字列 ID ごとに色を決める（同じ Category は同じ色）
	ImU32 IdColor( uint32 Id, float Value, float Alpha )
	{
		const float Hue = NMath::Fmod( static_cast<float>( Id ) * 0.61803398875f, 1.0f );
		ImVec4 Color( 0, 0, 0, Alpha );
		ImGui::ColorConvertHSVtoRGB( Hue, 0.55f, Value, Color.x, Color.y, Color.z );
		return ImGui::ColorConvertFloat4ToU32( Color );
	}

	ImU32 FrameBarColor( float64 Ms, int32 Alpha )
	{
		if ( Ms <= 8.4 ) return IM_COL32( 80, 190, 100, Alpha );
		if ( Ms <= 16.8 ) return IM_COL32( 215, 190, 70, Alpha );
		return IM_COL32( 220, 85, 70, Alpha );
	}

	FString FormatMs( float64 Ms )
	{
		return Ms >= 100.0 ? FString::Printf( "%.1f", Ms ) : ( Ms >= 1.0 ? FString::Printf( "%.2f", Ms ) : FString::Printf( "%.3f", Ms ) );
	}

	// 見やすい目盛り間隔（1 / 2 / 5 × 10^n）
	float64 NiceStep( float64 Raw )
	{
		if ( Raw <= 0.0 ) return 1.0;
		const float64 Magnitude = NMath::Pow( 10.0, NMath::Floor( NMath::Log10( Raw ) ) );
		const float64 Fraction = Raw / Magnitude;
		const float64 Nice = Fraction <= 1.0 ? 1.0 : ( Fraction <= 2.0 ? 2.0 : ( Fraction <= 5.0 ? 5.0 : 10.0 ) );
		return Nice * Magnitude;
	}

	int32 FindFrameByNumber( const IProfiler& Profiler, uint64 FrameNumber )
	{
		const int32 NumFrames = Profiler.GetNumFrames();
		if ( NumFrames <= 0 || FrameNumber == 0 ) return INDEX_NONE;

		// フレーム番号は増えていく順に並んでいる
		int32 Low = 0, High = NumFrames;
		while ( Low < High )
		{
			const int32 Mid = ( Low + High ) / 2;
			if ( Profiler.GetFrame( Mid ).FrameNumber < FrameNumber ) Low = Mid + 1;
			else High = Mid;
		}
		return Low < NumFrames && Profiler.GetFrame( Low ).FrameNumber == FrameNumber ? Low : INDEX_NONE;
	}

	// EndTick が Tick 以上になる最初のフレーム（無ければ GetNumFrames()）。フレームは時間順に並んでいる
	int32 FirstFrameEndingAtOrAfter( const IProfiler& Profiler, int64 Tick )
	{
		int32 Low = 0, High = Profiler.GetNumFrames();
		while ( Low < High )
		{
			const int32 Mid = ( Low + High ) / 2;
			if ( Profiler.GetFrame( Mid ).EndTick < Tick ) Low = Mid + 1;
			else High = Mid;
		}
		return Low;
	}

	// StartTick が Tick 以下になる最後のフレーム（無ければ INDEX_NONE）
	int32 LastFrameStartingAtOrBefore( const IProfiler& Profiler, int64 Tick )
	{
		int32 Low = 0, High = Profiler.GetNumFrames();
		while ( Low < High )
		{
			const int32 Mid = ( Low + High ) / 2;
			if ( Profiler.GetFrame( Mid ).StartTick <= Tick ) Low = Mid + 1;
			else High = Mid;
		}
		return Low - 1;
	}

	// そのフレームでの、あるスレッドの行（ThreadId が 0 ならメインスレッド）
	const TArray<FProfileSample>& SamplesOf( const FProfilerFrame& Frame, uint32 ThreadId )
	{
		if ( ThreadId != 0 )
		{
			for ( const FProfilerThreadTrack& Track : Frame.Threads ) if ( Track.ThreadId == ThreadId ) return Track.Samples;
		}
		return Frame.Samples;
	}

	FString ThreadLabel( const IProfiler& Profiler, const FProfilerFrame& Frame, uint32 ThreadId )
	{
		if ( ThreadId == 0 ) return FString( "Main thread" );
		for ( const FProfilerThreadTrack& Track : Frame.Threads )
		{
			if ( Track.ThreadId == ThreadId && Track.Name != 0 ) return Profiler.GetString( Track.Name );
		}
		return FString::Printf( "Thread %u", static_cast<unsigned>( ThreadId ) );
	}
}

void NDebugUI::DrawProfilerTimeline( IProfiler& Profiler, FProfilerTimelineState& State, uint64 SelectedFrameNumber )
{
	if ( State.Mode < 0 || State.Mode > 1 ) State.Mode = 0;
	bool& bAutoScroll = State.bAutoScroll;

	EPendingAction Action = EPendingAction::None;
	DrawToolbar( Profiler, State, bAutoScroll, Action, State.ViewSpanMs[State.Mode] );

	const int32 NumFrames = Profiler.GetNumFrames();
	if ( NumFrames <= 0 )
	{
		State.bInitialized = false;
		ImGui::TextDisabled( "No data. Press Record (F2)." );
		return;
	}

	const FProfilerFrame& Oldest = Profiler.GetFrame( 0 );
	const FProfilerFrame& Newest = Profiler.GetFrame( NumFrames - 1 );

	//-------------------------------------------------------------------------
	// 時間の基準と、記録が作り直されたことの検出
	//-------------------------------------------------------------------------
	if ( !State.bInitialized || Oldest.FrameNumber > State.LastNewestFrameNumber || Newest.FrameNumber < State.LastNewestFrameNumber )
	{
		State.bInitialized = true;
		State.BaseTick = Oldest.StartTick;
		State.LiveTick = 0;
		State.SelectedSampleIndex = INDEX_NONE;
		State.SelectedFrameNumber = 0;
		State.SelectedThreadId = 0;
		State.FocusedFrameNumber = 0;
		State.bViewNeedsPlacement = true;
		State.bStackedInitialized = false;
		ResetPyramid( State, Oldest.FrameNumber );
	}
	State.LastNewestFrameNumber = Newest.FrameNumber;
	UpdatePyramid( Profiler, State );

	const float64 DataStartMs = TickToMs( State, Oldest.StartTick );
	const float64 DataEndMs = TickToMs( State, Newest.EndTick );
	const float64 NewestMs = NPlatformTime::TicksToMs( Newest.GetDurationTick() );
	const float64 LongestMs = NMath::Max( static_cast<float64>( State.MaxFrameMsSeen ), 1.0 );

	// 最初は、最新のフレーム数枚分が見えるように合わせる（そのあとは勝手に動かさない）
	if ( State.bViewNeedsPlacement )
	{
		State.bViewNeedsPlacement = false;
		State.ViewSpanMs[ModeContinuous] = NMath::Clamp( NewestMs * 8.0, 10.0, 200.0 );
		State.ViewStartMs[ModeContinuous] = DataEndMs - State.ViewSpanMs[ModeContinuous];
		State.StackedPinnedFrameNumber = Newest.FrameNumber;
		State.LiveFrameNumber = Newest.FrameNumber;
		State.LiveEndMs = DataEndMs;
	}

	if ( !State.bStackedInitialized )
	{
		State.bStackedInitialized = true;
		State.ViewStartMs[ModeStacked] = 0.0;
		State.ViewSpanMs[ModeStacked] = NMath::Clamp( NewestMs * 1.3, 2.0, NMath::Max( LongestMs * 1.05, 2.0 ) );   // std::clamp は、下限が上限より大きいと assert する
	}

	// 横に動かせる範囲（横スクロールバーと、ドラッグなどの制限に使う）
	if ( State.Mode == ModeContinuous )
	{
		State.PanMinMs = DataStartMs;
		State.PanMaxMs = DataEndMs;
	}
	else
	{
		State.PanMinMs = 0.0;
		State.PanMaxMs = LongestMs * 1.05;
	}

	//-------------------------------------------------------------------------
	// ツールバーのボタン
	//-------------------------------------------------------------------------
	const float64 OldCenterMs = State.ViewStartMs[ModeContinuous] + State.ViewSpanMs[ModeContinuous] * 0.5;
	switch ( Action )
	{
	case EPendingAction::FitAll:
		if ( State.Mode == ModeContinuous )
		{
			State.ViewStartMs[ModeContinuous] = DataStartMs;
			State.ViewSpanMs[ModeContinuous] = NMath::Max( DataEndMs - DataStartMs, 1.0 ) * 1.02;
			bAutoScroll = false;
		}
		else
		{
			State.ViewStartMs[ModeStacked] = 0.0;
			State.ViewSpanMs[ModeStacked] = LongestMs * 1.05;
		}
		break;
	case EPendingAction::Span1s:    State.ViewSpanMs[ModeContinuous] = 1000.0; break;
	case EPendingAction::Span100ms: State.ViewSpanMs[ModeContinuous] = 100.0; break;
	case EPendingAction::Span10ms:  State.ViewSpanMs[ModeContinuous] = 10.0; break;
	case EPendingAction::Span1ms:   State.ViewSpanMs[ModeContinuous] = 1.0; break;
	case EPendingAction::Span100us: State.ViewSpanMs[ModeContinuous] = 0.1; break;
	default: break;
	}
	State.ViewSpanMs[ModeContinuous] = NMath::Max( State.ViewSpanMs[ModeContinuous], State.MinSpanMs );
	if ( Action >= EPendingAction::Span1s && !bAutoScroll )
	{
		// Auto Scroll していなければ、今の表示の中央を保ったまま幅を変える
		State.ViewStartMs[ModeContinuous] = OldCenterMs - State.ViewSpanMs[ModeContinuous] * 0.5;
	}

	//-------------------------------------------------------------------------
	// ウィンドウ全体で選ばれたフレーム（フレームグラフのクリックなど）へ画面を合わせる。Auto Scroll は止まる
	//-------------------------------------------------------------------------
	if ( SelectedFrameNumber != 0 && SelectedFrameNumber != State.FocusedFrameNumber )
	{
		const int32 Index = FindFrameByNumber( Profiler, SelectedFrameNumber );
		if ( Index != INDEX_NONE )
		{
			const FProfilerFrame& Frame = Profiler.GetFrame( Index );
			ZoomToRange( State, ModeContinuous, TickToMs( State, Frame.StartTick ), TickToMs( State, Frame.EndTick ) );
			State.StackedPinnedFrameNumber = Profiler.GetFrame( NMath::Min( Index + 5, NumFrames - 1 ) ).FrameNumber;   // 選んだフレームが先頭の少し下に見えるように
			State.PendingScrollFrameNumber = SelectedFrameNumber;
			State.FocusedFrameNumber = SelectedFrameNumber;
			bAutoScroll = false;
			State.bWasFollowing = false;
		}
	}

	//-------------------------------------------------------------------------
	// Auto Scroll: 表示の右端 / 先頭を、設定した頻度で最新に進める
	//-------------------------------------------------------------------------
	const int64 Now = NPlatformTime::GetTicks();
	if ( bAutoScroll )
	{
		const float64 Hz = NMath::Max( static_cast<float64>( State.LiveRefreshHz ), 1.0 );
		const bool bDue = State.LiveTick == 0 || static_cast<float64>( Now - State.LiveTick ) >= static_cast<float64>( NPlatformTime::GetTicksPerSecond() ) / Hz
			|| !Profiler.IsEnabled();   // 止まっているときは動かないので、すぐ合わせる
		if ( bDue )
		{
			State.LiveEndMs = DataEndMs;
			State.LiveFrameNumber = Newest.FrameNumber;
			State.LiveTick = Now;
		}
		State.ViewStartMs[ModeContinuous] = State.LiveEndMs - State.ViewSpanMs[ModeContinuous];
	}
	if ( State.bWasFollowing && !bAutoScroll ) State.StackedPinnedFrameNumber = State.LiveFrameNumber != 0 ? State.LiveFrameNumber : Newest.FrameNumber;
	if ( !State.bWasFollowing && bAutoScroll )
	{
		State.LiveTick = 0;
		State.bScrollTopPending = true;
	}
	State.bWasFollowing = bAutoScroll;

	//-------------------------------------------------------------------------
	// 描画
	//-------------------------------------------------------------------------
	const ImGuiStyle& Style = ImGui::GetStyle();
	const float Width = NMath::Max( 140.0f, ImGui::GetContentRegionAvail().x - Style.ScrollbarSize );

	const float FixedAbove = ( State.Mode == ModeContinuous ? OverviewHeight + RulerHeight + FrameTrackHeight + 3.0f * Style.ItemSpacing.y : RulerHeight + Style.ItemSpacing.y );
	const float BottomReserve = ImGui::GetTextLineHeightWithSpacing() + 6.0f;   // タブの下の状態表示の行
	const float ChildHeight = NMath::Max( 90.0f, ImGui::GetContentRegionAvail().y - FixedAbove - ScrollbarHeight - Style.ItemSpacing.y - DetailHeight - 2.0f * Style.ItemSpacing.y - BottomReserve );

	if ( State.Mode == ModeContinuous ) DrawContinuous( Profiler, State, bAutoScroll, Width, DataStartMs, DataEndMs, ChildHeight );
	else DrawStacked( Profiler, State, bAutoScroll, Width, ChildHeight );

	DrawDetail( Profiler, State );
}
