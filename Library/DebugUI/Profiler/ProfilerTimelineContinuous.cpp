#include "ProfilerTimelineInternal.h"

namespace NProfilerTimelineUI
{
	//-------------------------------------------------------------------------
	// Continuous: 複数のフレームを 1 本の時間軸に並べる
	//-------------------------------------------------------------------------
	void DrawContinuous( IProfiler& Profiler, FProfilerTimelineState& State, bool& bFollow, float Width, float64 DataStartMs, float64 DataEndMs, float ChildHeight )
	{
		ImGuiIO& IO = ImGui::GetIO();
		const int32 Mode = ModeContinuous;
		const float64 DataSpan = NMath::Max( DataEndMs - DataStartMs, 1.0 );
		const float64 MaxSpanMs = NMath::Max( DataSpan * 1.1, 10.0 );

		DrawOverview( Profiler, State, bFollow, Width, DataStartMs, DataEndMs );

		// ---- 時間の目盛りとフレームの帯
		FHighlight Highlight( Profiler, State.Highlight );
		const ImVec2 HeaderOrigin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton( "##TimelineHeader", ImVec2( Width, RulerHeight + FrameTrackHeight ) );
		const bool bHeaderHovered = ImGui::IsItemHovered();
		const bool bHeaderDoubleClicked = bHeaderHovered && ImGui::IsMouseDoubleClicked( 0 );
		HandleTimeInput( State, Mode, bFollow, HeaderOrigin.x, Width, true, MaxSpanMs, true );

		FArea Header;
		Header.DrawList = ImGui::GetWindowDrawList();
		Header.Origin = HeaderOrigin;
		Header.Width = Width;
		Header.StartMs = State.ViewStartMs[Mode];
		Header.SpanMs = State.ViewSpanMs[Mode];

		const int64 TicksPerSecond = NPlatformTime::GetTicksPerSecond();
		const int64 ViewStartTick = State.BaseTick + static_cast<int64>( Header.StartMs * 0.001 * static_cast<float64>( TicksPerSecond ) );
		const int64 ViewEndTick = State.BaseTick + static_cast<int64>( ( Header.StartMs + Header.SpanMs ) * 0.001 * static_cast<float64>( TicksPerSecond ) );
		const int32 FirstFrame = FirstFrameEndingAtOrAfter( Profiler, ViewStartTick );
		const int32 LastFrame = LastFrameStartingAtOrBefore( Profiler, ViewEndTick );
		const int32 NumVisible = NMath::Max( 0, LastFrame - FirstFrame + 1 );
		const int32 GroupLevel = ChooseGroupLevel( NumVisible, Width );   // 0: 1 フレームずつ描く / 1 以上: 縮小しているので、フレームをまとめた群で描く

		DrawRuler( Header, HeaderOrigin.y, false );

		// フレームの帯（番号と時間）
		int32 HoveredFrame = INDEX_NONE;
		int32 HoveredGroupFirst = INDEX_NONE, HoveredGroupLast = INDEX_NONE;
		{
			ImDrawList* DL = Header.DrawList;
			const float Y0 = HeaderOrigin.y + RulerHeight;
			DL->AddRectFilled( ImVec2( HeaderOrigin.x, Y0 ), ImVec2( HeaderOrigin.x + Width, Y0 + FrameTrackHeight ), IM_COL32( 18, 18, 18, 255 ) );

			if ( GroupLevel > 0 )
			{
				ForEachFrameGroup( Profiler, State, FirstFrame, LastFrame, GroupLevel, [&]( int32 FirstIndex, int32 LastIndex, float32 GroupMs )
				{
					const float X0 = Header.ToX( TickToMs( State, Profiler.GetFrame( FirstIndex ).StartTick ) );
					const float X1 = Header.ToX( TickToMs( State, Profiler.GetFrame( LastIndex ).EndTick ) );
					const float L = NMath::Max( X0, HeaderOrigin.x );
					const float R = NMath::Min( X1, HeaderOrigin.x + Width );
					if ( R <= L ) return;

					const bool bHovered = bHeaderHovered && IO.MousePos.x >= L && IO.MousePos.x < R && IO.MousePos.y >= Y0;
					if ( bHovered )
					{
						HoveredGroupFirst = FirstIndex;
						HoveredGroupLast = LastIndex;
					}
					DL->AddRectFilled( ImVec2( L, Y0 + 1.0f ), ImVec2( NMath::Max( R - 1.0f, L + 1.0f ), Y0 + FrameTrackHeight - 1.0f ), FrameBarColor( GroupMs, bHovered ? 255 : 190 ) );
				} );
			}

			for ( int32 i = FirstFrame; GroupLevel == 0 && i <= LastFrame && i < Profiler.GetNumFrames(); ++i )
			{
				const FProfilerFrame& Frame = Profiler.GetFrame( i );
				const float64 Ms = NPlatformTime::TicksToMs( Frame.GetDurationTick() );
				const float X0 = Header.ToX( TickToMs( State, Frame.StartTick ) );
				const float X1 = Header.ToX( TickToMs( State, Frame.EndTick ) );
				const float L = NMath::Max( X0, HeaderOrigin.x );
				const float R = NMath::Min( X1, HeaderOrigin.x + Width );
				if ( R <= L ) continue;

				const bool bHovered = bHeaderHovered && IO.MousePos.x >= L && IO.MousePos.x < R && IO.MousePos.y >= Y0;
				if ( bHovered ) HoveredFrame = i;
				DL->AddRectFilled( ImVec2( L, Y0 + 1.0f ), ImVec2( NMath::Max( R - 1.0f, L + 1.0f ), Y0 + FrameTrackHeight - 1.0f ), FrameBarColor( Ms, bHovered ? 255 : 190 ) );
				if ( R - L > 74.0f )
				{
					char Label[40];
					NCString::Format( Label, sizeof( Label ), "#%llu  %.2f ms", static_cast<unsigned long long>( Frame.FrameNumber ), Ms );
					DL->PushClipRect( ImVec2( L, Y0 ), ImVec2( R, Y0 + FrameTrackHeight ), true );
					DL->AddText( ImVec2( L + 4.0f, Y0 + 4.0f ), IM_COL32( 10, 10, 10, 255 ), Label );
					DL->PopClipRect();
				}
			}
		}
		if ( HoveredFrame != INDEX_NONE )
		{
			const FProfilerFrame& Frame = Profiler.GetFrame( HoveredFrame );
			const float64 Ms = NPlatformTime::TicksToMs( Frame.GetDurationTick() );
			ImGui::SetTooltip( "Frame #%llu: %.2f ms (%.0f FPS), %d samples\nDouble-click to zoom to this frame", static_cast<unsigned long long>( Frame.FrameNumber ), Ms,
							   Ms > 0.0 ? 1000.0 / Ms : 0.0, Frame.Samples.Num() );
			if ( bHeaderDoubleClicked )
			{
				ZoomToRange( State, Mode, TickToMs( State, Frame.StartTick ), TickToMs( State, Frame.EndTick ) );
				bFollow = false;
			}
		}

		if ( HoveredGroupFirst != INDEX_NONE )
		{
			const FProfilerFrame& First = Profiler.GetFrame( HoveredGroupFirst );
			const FProfilerFrame& Last = Profiler.GetFrame( HoveredGroupLast );
			ImGui::SetTooltip( "Frames #%llu - #%llu (%d frames)\nDouble-click to zoom in", static_cast<unsigned long long>( First.FrameNumber ),
							   static_cast<unsigned long long>( Last.FrameNumber ), HoveredGroupLast - HoveredGroupFirst + 1 );
			if ( bHeaderDoubleClicked )
			{
				ZoomToRange( State, Mode, TickToMs( State, First.StartTick ), TickToMs( State, Last.EndTick ) );
				bFollow = false;
			}
		}

		// ---- サンプル
		const float RowHeight = static_cast<float>( State.RowHeight );
		const int32 TotalFrames = Profiler.GetNumFrames();
		uint16 MaxDepth = 0;
		for ( int32 i = FirstFrame; GroupLevel == 0 && i <= LastFrame && i < TotalFrames; ++i ) MaxDepth = NMath::Max( MaxDepth, Profiler.GetFrame( i ).MaxDepth );

		// メインスレッド以外のスレッドの行（見えているフレームに記録があるスレッドだけ。縮小表示のときは出さない）
		TArray<int32> ActiveThreads;   // State.Threads のインデックス
		const int32 ThreadScanFirst = NMath::Max( 0, FirstFrame - ThreadLookbackFrames );
		if ( GroupLevel == 0 )
		{
			for ( FProfilerTimelineThread& Thread : State.Threads )
			{
				Thread.bInView = false;
				Thread.ViewMaxDepth = 0;
			}
			for ( int32 i = ThreadScanFirst; i <= LastFrame && i < TotalFrames; ++i )
			{
				for ( const FProfilerThreadTrack& Track : Profiler.GetFrame( i ).Threads )
				{
					int32 Found = INDEX_NONE;
					for ( int32 k = 0; k < State.Threads.Num(); ++k ) if ( State.Threads[k].ThreadId == Track.ThreadId ) Found = k;
					if ( Found == INDEX_NONE )
					{
						Found = State.Threads.Emplace();
						State.Threads[Found].ThreadId = Track.ThreadId;
					}
					FProfilerTimelineThread& Thread = State.Threads[Found];
					if ( Track.Name != 0 ) Thread.NameId = Track.Name;
					Thread.bInView = true;
					Thread.ViewMaxDepth = NMath::Max( Thread.ViewMaxDepth, Track.MaxDepth );
				}
			}
			if ( State.bShowThreads )
			{
				for ( int32 k = 0; k < State.Threads.Num(); ++k ) if ( State.Threads[k].bInView && State.Threads[k].bVisible ) ActiveThreads.Add( k );
			}
		}

		// 縦の並び: メインスレッド（スレッドの行があるときは見出し付き）、続けて各スレッド（見出し + 行）
		const float MainRowsTop = 2.0f + ( ActiveThreads.IsEmpty() ? 0.0f : ThreadHeaderHeight );
		float ContentBottom = MainRowsTop + RowHeight * static_cast<float>( MaxDepth + 1 );
		TArray<float32> ThreadRowsTop;   // 各スレッドの行の上端（見出しの下。サンプルの領域の上端からの距離）
		for ( const int32 k : ActiveThreads )
		{
			ThreadRowsTop.Add( ContentBottom + 4.0f + ThreadHeaderHeight );
			ContentBottom += 4.0f + ThreadHeaderHeight + RowHeight * static_cast<float>( State.Threads[k].ViewMaxDepth + 1 );
		}
		const float ContentHeight = NMath::Max( ContentBottom + 6.0f, ChildHeight - 4.0f );

		ImGui::BeginChild( "##TimelineSamples", ImVec2( 0.0f, ChildHeight ), ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
		const ImVec2 SamplesOrigin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton( "##TimelineSamplesHit", ImVec2( Width, ContentHeight ) );
		const bool bHovered = ImGui::IsItemHovered();
		const bool bClicked = ImGui::IsItemDeactivated() && IO.MouseDragMaxDistanceSqr[0] < 16.0f && ImGui::IsMouseReleased( 0 );
		const bool bDoubleClicked = bHovered && ImGui::IsMouseDoubleClicked( 0 );
		HandleTimeInput( State, Mode, bFollow, SamplesOrigin.x, Width, true, MaxSpanMs, true );
		if ( bHovered && IO.MouseWheel != 0.0f )
		{
			ImGui::SetItemKeyOwner( ImGuiKey_MouseWheelY );   // 親のウィンドウをスクロールさせない
			if ( IO.KeyAlt ) ImGui::SetScrollY( ImGui::GetScrollY() - IO.MouseWheel * RowHeight * 3.0f );
		}

		FArea Area = Header;
		Area.DrawList = ImGui::GetWindowDrawList();
		Area.Origin = SamplesOrigin;
		Area.StartMs = State.ViewStartMs[Mode];

		FHit Hit;
		for ( int32 i = FirstFrame; GroupLevel == 0 && i <= LastFrame && i < Profiler.GetNumFrames(); ++i )
		{
			const FProfilerFrame& Frame = Profiler.GetFrame( i );
			const float64 FrameStartMs = TickToMs( State, Frame.StartTick );

			// フレームごとに交互の背景と、境目の線
			const float X0 = Area.ToX( FrameStartMs );
			const float X1 = Area.ToX( TickToMs( State, Frame.EndTick ) );
			const float L = NMath::Max( X0, SamplesOrigin.x );
			const float R = NMath::Min( X1, SamplesOrigin.x + Width );
			if ( R > L && ( Frame.FrameNumber & 1 ) == 0 )
			{
				Area.DrawList->AddRectFilled( ImVec2( L, SamplesOrigin.y ), ImVec2( R, SamplesOrigin.y + ContentHeight ), IM_COL32( 255, 255, 255, 10 ) );
			}
			if ( State.bShowFrameSeparators && X0 >= SamplesOrigin.x && X0 <= SamplesOrigin.x + Width )
			{
				Area.DrawList->AddLine( ImVec2( X0, SamplesOrigin.y ), ImVec2( X0, SamplesOrigin.y + ContentHeight ), IM_COL32( 255, 255, 255, 55 ) );
			}

			// 1 フレームが 2 ピクセルに満たないほど縮小しているときは、サンプルは描かずフレーム全体を 1 本の帯で示す（全サンプルを調べると重い）
			if ( X1 - X0 < 2.0f )
			{
				const float Left = NMath::Max( X0, SamplesOrigin.x );
				Area.DrawList->AddRectFilled( ImVec2( Left, SamplesOrigin.y + 2.0f ), ImVec2( NMath::Max( X1, Left + 1.0f ), SamplesOrigin.y + 2.0f + RowHeight - 1.0f ),
											  FrameBarColor( NPlatformTime::TicksToMs( Frame.GetDurationTick() ), 200 ) );
				continue;
			}

			DrawFrameSamples( Profiler, State, Area, Frame, i, Frame.Samples, 0, FrameStartMs, SamplesOrigin.y + MainRowsTop, RowHeight, IO.MousePos, bHovered, Highlight, Hit );
		}

		// 縮小しているときは、サンプルは描かず、まとめた群の帯だけ描く（高さ = 中で一番長いフレームの色）
		if ( GroupLevel > 0 )
		{
			ForEachFrameGroup( Profiler, State, FirstFrame, LastFrame, GroupLevel, [&]( int32 FirstIndex, int32 LastIndex, float32 GroupMs )
			{
				const float X0 = Area.ToX( TickToMs( State, Profiler.GetFrame( FirstIndex ).StartTick ) );
				const float X1 = Area.ToX( TickToMs( State, Profiler.GetFrame( LastIndex ).EndTick ) );
				const float Left = NMath::Max( X0, SamplesOrigin.x );
				const float Right = NMath::Min( X1, SamplesOrigin.x + Width );
				if ( Right <= Left ) return;
				Area.DrawList->AddRectFilled( ImVec2( Left, SamplesOrigin.y + 2.0f ), ImVec2( NMath::Max( Right - 1.0f, Left + 1.0f ), SamplesOrigin.y + 2.0f + RowHeight - 1.0f ),
											  FrameBarColor( GroupMs, 200 ) );
			} );
		}

		// メインスレッド以外のスレッド（ジョブシステムのワーカーなど）の行
		if ( !ActiveThreads.IsEmpty() )
		{
			const auto DrawThreadHeader = [&]( float Y, const FString& Label )
			{
				Area.DrawList->AddRectFilled( ImVec2( SamplesOrigin.x, Y ), ImVec2( SamplesOrigin.x + Width, Y + ThreadHeaderHeight - 1.0f ), IM_COL32( 44, 44, 44, 255 ) );
				Area.DrawList->AddText( ImVec2( SamplesOrigin.x + 4.0f, Y + 1.0f ), IM_COL32( 235, 235, 235, 255 ), UTF8( Label ) );
			};
			DrawThreadHeader( SamplesOrigin.y + 2.0f, FString( "Main thread" ) );
			for ( int32 a = 0; a < ActiveThreads.Num(); ++a )
			{
				const FProfilerTimelineThread& Thread = State.Threads[ActiveThreads[a]];
				const FString Name = Thread.NameId != 0 ? Profiler.GetString( Thread.NameId ) : FString::Printf( "Thread %u", static_cast<unsigned>( Thread.ThreadId ) );
				DrawThreadHeader( SamplesOrigin.y + ThreadRowsTop[a] - ThreadHeaderHeight, Name );
			}

			for ( int32 i = ThreadScanFirst; i <= LastFrame && i < TotalFrames; ++i )
			{
				const FProfilerFrame& Frame = Profiler.GetFrame( i );
				if ( Frame.Threads.IsEmpty() ) continue;
				const float64 FrameStartMs = TickToMs( State, Frame.StartTick );
				for ( const FProfilerThreadTrack& Track : Frame.Threads )
				{
					for ( int32 a = 0; a < ActiveThreads.Num(); ++a )
					{
						if ( State.Threads[ActiveThreads[a]].ThreadId != Track.ThreadId ) continue;
						DrawFrameSamples( Profiler, State, Area, Frame, i, Track.Samples, Track.ThreadId, FrameStartMs, SamplesOrigin.y + ThreadRowsTop[a], RowHeight, IO.MousePos,
										  bHovered, Highlight, Hit );
						break;
					}
				}
			}
		}

		if ( Hit.FrameIndex != INDEX_NONE )
		{
			const FProfilerFrame& Frame = Profiler.GetFrame( Hit.FrameIndex );
			const FProfileSample& Sample = SamplesOf( Frame, Hit.ThreadId )[Hit.SampleIndex];
			DrawSampleTooltip( Profiler, Frame, Sample, Hit.ThreadId );
			if ( bClicked )
			{
				State.SelectedFrameNumber = Frame.FrameNumber;
				State.SelectedThreadId = Hit.ThreadId;
				State.SelectedSampleIndex = Hit.SampleIndex;
			}
			if ( bDoubleClicked )
			{
				ZoomToRange( State, Mode, TickToMs( State, Sample.StartTick ), TickToMs( State, Sample.StartTick + Sample.DurationTick ) );
				bFollow = false;
			}
		}
		ImGui::EndChild();

		// ズームなどで範囲の外へ出たら戻す。下に横スクロールバー
		ClampView( State, Mode );
		DrawHScrollbar( State, Mode, bFollow, Width, true );
	}
}
