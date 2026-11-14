#include "ProfilerTimelineInternal.h"

namespace NProfilerTimelineUI
{
	//-------------------------------------------------------------------------
	// Stacked: 1 フレームを 1 段にして縦に積む（新しいものが上）
	//-------------------------------------------------------------------------
	void DrawStacked( IProfiler& Profiler, FProfilerTimelineState& State, bool& bFollow, float Width, float ChildHeight )
	{
		ImGuiIO& IO = ImGui::GetIO();
		const int32 Mode = ModeStacked;
		const int32 NumFrames = Profiler.GetNumFrames();
		const float64 MaxSpanMs = NMath::Max( static_cast<float64>( State.MaxFrameMsSeen ) * 2.0, 4.0 );

		// 先頭に出すフレーム（Auto Scroll 中は一定の頻度で新しくなる。オフなら固定）
		const uint64 TopNumber = bFollow ? State.LiveFrameNumber : State.StackedPinnedFrameNumber;
		int32 TopIndex = FindFrameByNumber( Profiler, TopNumber );
		if ( TopIndex == INDEX_NONE ) TopIndex = NumFrames - 1;

		FHighlight Highlight( Profiler, State.Highlight );

		// 目盛り
		const ImVec2 HeaderOrigin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton( "##TimelineStackedHeader", ImVec2( Width, RulerHeight ) );
		HandleTimeInput( State, Mode, bFollow, HeaderOrigin.x, Width, false, MaxSpanMs, false );
		FArea Header;
		Header.DrawList = ImGui::GetWindowDrawList();
		Header.Origin = HeaderOrigin;
		Header.Width = Width;
		Header.StartMs = State.ViewStartMs[Mode];
		Header.SpanMs = State.ViewSpanMs[Mode];
		DrawRuler( Header, HeaderOrigin.y, true );

		// 各フレームの段は同じ高さ（見出し + 深さの最大 + すきま）。深さは先頭付近のフレームから決める（履歴が長くても全フレームは調べない）
		const float RowHeight = static_cast<float>( State.RowHeight );
		uint16 MaxDepth = 0;
		for ( int32 i = NMath::Max( 0, TopIndex - 255 ); i <= TopIndex; ++i ) MaxDepth = NMath::Max( MaxDepth, Profiler.GetFrame( i ).MaxDepth );
		const float Stride = StackedHeaderHeight + RowHeight * static_cast<float>( MaxDepth + 1 ) + StackedGap;
		const float TotalHeight = Stride * static_cast<float>( TopIndex + 1 );

		ImGui::BeginChild( "##TimelineStackedSamples", ImVec2( 0.0f, ChildHeight ), ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar );
		const ImVec2 SamplesOrigin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton( "##TimelineStackedHit", ImVec2( Width, NMath::Max( TotalHeight, 1.0f ) ) );
		const bool bHovered = ImGui::IsItemHovered();
		const bool bClicked = ImGui::IsItemDeactivated() && IO.MouseDragMaxDistanceSqr[0] < 16.0f && ImGui::IsMouseReleased( 0 );
		const bool bDoubleClicked = bHovered && ImGui::IsMouseDoubleClicked( 0 );
		HandleTimeInput( State, Mode, bFollow, SamplesOrigin.x, Width, false, MaxSpanMs, false );

		// 指定されたフレームの段までスクロールする
		if ( State.PendingScrollFrameNumber != 0 )
		{
			const int32 Index = FindFrameByNumber( Profiler, State.PendingScrollFrameNumber );
			if ( Index != INDEX_NONE && Index <= TopIndex ) ImGui::SetScrollY( Stride * static_cast<float>( TopIndex - Index ) );
			State.PendingScrollFrameNumber = 0;
		}

		// Auto Scroll 中に下へスクロールしたら、そこで止める（新しいフレームが来ても見ている段が動かないように）
		if ( State.bScrollTopPending )
		{
			ImGui::SetScrollY( 0.0f );
			State.bScrollTopPending = false;
		}
		else if ( bFollow && ImGui::GetScrollY() > 1.0f )
		{
			bFollow = false;
		}

		FArea Area = Header;
		Area.DrawList = ImGui::GetWindowDrawList();
		Area.Origin = SamplesOrigin;
		Area.StartMs = State.ViewStartMs[Mode];

		const float ScrollY = ImGui::GetScrollY();
		FHit Hit;
		const int32 FirstGroup = NMath::Max( 0, static_cast<int32>( NMath::Floor( ScrollY / Stride ) ) );
		const int32 LastGroup = NMath::Min( TopIndex, static_cast<int32>( NMath::Floor( ( ScrollY + ChildHeight ) / Stride ) ) );
		for ( int32 Group = FirstGroup; Group <= LastGroup; ++Group )   // 見えない段は描かない
		{
			const int32 FrameIndex = TopIndex - Group;
			const float Top = Stride * static_cast<float>( Group );
			const float Bottom = Top + Stride;

			const FProfilerFrame& Frame = Profiler.GetFrame( FrameIndex );
			const float64 FrameMs = NPlatformTime::TicksToMs( Frame.GetDurationTick() );
			const float Y = SamplesOrigin.y + Top;

			if ( ( Frame.FrameNumber & 1 ) == 0 ) Area.DrawList->AddRectFilled( ImVec2( SamplesOrigin.x, Y ), ImVec2( SamplesOrigin.x + Width, Y + Bottom - Top - StackedGap ), IM_COL32( 255, 255, 255, 10 ) );

			// 見出し（番号と時間。フレームの終わりの位置に縦線）
			char Label[64];
			NCString::Format( Label, sizeof( Label ), "#%llu   %.2f ms", static_cast<unsigned long long>( Frame.FrameNumber ), FrameMs );
			Area.DrawList->AddRectFilled( ImVec2( SamplesOrigin.x, Y ), ImVec2( SamplesOrigin.x + Width, Y + StackedHeaderHeight - 1.0f ), IM_COL32( 40, 40, 40, 255 ) );
			Area.DrawList->AddText( ImVec2( SamplesOrigin.x + 4.0f, Y + 1.0f ), IM_COL32( 235, 235, 235, 255 ), Label );
			const float EndX = Area.ToX( FrameMs );
			if ( EndX >= SamplesOrigin.x && EndX <= SamplesOrigin.x + Width )
			{
				Area.DrawList->AddLine( ImVec2( EndX, Y ), ImVec2( EndX, Y + Bottom - Top - StackedGap ), FrameBarColor( FrameMs, 230 ), 2.0f );
			}

			// 深いサンプルが次のフレームの段にはみ出さないように切る
			Area.DrawList->PushClipRect( ImVec2( SamplesOrigin.x, Y + StackedHeaderHeight ), ImVec2( SamplesOrigin.x + Width, Y + Bottom - Top - StackedGap ), true );
			DrawFrameSamples( Profiler, State, Area, Frame, FrameIndex, Frame.Samples, 0, 0.0, Y + StackedHeaderHeight, RowHeight, IO.MousePos, bHovered, Highlight, Hit );
			Area.DrawList->PopClipRect();
		}

		if ( Hit.FrameIndex != INDEX_NONE )
		{
			const FProfilerFrame& Frame = Profiler.GetFrame( Hit.FrameIndex );
			const FProfileSample& Sample = Frame.Samples[Hit.SampleIndex];
			DrawSampleTooltip( Profiler, Frame, Sample );
			if ( bClicked )
			{
				State.SelectedFrameNumber = Frame.FrameNumber;
				State.SelectedThreadId = 0;
				State.SelectedSampleIndex = Hit.SampleIndex;
			}
			if ( bDoubleClicked )
			{
				const float64 Start = NPlatformTime::TicksToMs( Sample.StartTick - Frame.StartTick );
				ZoomToRange( State, Mode, Start, Start + NPlatformTime::TicksToMs( Sample.DurationTick ) );
			}
		}
		ImGui::EndChild();

		ClampView( State, Mode );
		DrawHScrollbar( State, Mode, bFollow, Width, false );
	}
}
