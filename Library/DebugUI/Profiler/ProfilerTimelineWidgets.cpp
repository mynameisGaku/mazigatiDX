#include "ProfilerTimelineInternal.h"

namespace NProfilerTimelineUI
{
	//-------------------------------------------------------------------------
	// 目盛り（Continuous は記録の基準からの時間、Stacked はフレーム開始からの時間）
	//-------------------------------------------------------------------------
	void DrawRuler( const FArea& Area, float Y, bool bStacked )
	{
		ImDrawList* DL = Area.DrawList;
		const float64 MsPerPixel = Area.SpanMs / static_cast<float64>( Area.Width );
		float64 Step = NiceStep( MsPerPixel * 90.0 );
		if ( Step < 0.001 ) Step = NiceStep( MsPerPixel * 135.0 );   // ごく細かいときは、ラベル（小数の桁数）が長くなるので間隔を広げる
		const float64 First = NMath::Floor( Area.StartMs / Step ) * Step;

		DL->AddRectFilled( ImVec2( Area.Origin.x, Y ), ImVec2( Area.Right(), Y + RulerHeight ), IM_COL32( 26, 26, 26, 255 ) );

		// 細かい目盛り（5 分割）
		for ( float64 T = First; T <= Area.StartMs + Area.SpanMs; T += Step / 5.0 )
		{
			const float X = Area.ToX( T );
			if ( X < Area.Origin.x || X > Area.Right() ) continue;
			DL->AddLine( ImVec2( X, Y + RulerHeight - 4.0f ), ImVec2( X, Y + RulerHeight ), IM_COL32( 255, 255, 255, 60 ) );
		}

		// 数字つきの目盛り
		for ( float64 T = First; T <= Area.StartMs + Area.SpanMs + Step; T += Step )
		{
			const float X = Area.ToX( T );
			if ( X < Area.Origin.x - 80.0f || X > Area.Right() ) continue;
			DL->AddLine( ImVec2( X, Y + 4.0f ), ImVec2( X, Y + RulerHeight ), IM_COL32( 255, 255, 255, 110 ) );

			char Label[48];
			if ( bStacked )
			{
				const int32 Decimals = Step >= 1.0 ? 0 : NMath::Clamp( static_cast<int32>( NMath::Ceil( -NMath::Log10( Step ) ) ), 0, 8 );
				NCString::Format( Label, sizeof( Label ), "%.*f ms", Decimals, T );
			}
			else
			{
				// 秒で表示する。目盛りの細かさに合わせて小数の桁数を決める
				const float64 StepSeconds = Step / 1000.0;
				const int32 Decimals = StepSeconds >= 1.0 ? 0 : NMath::Clamp( static_cast<int32>( NMath::Ceil( -NMath::Log10( StepSeconds ) ) ), 0, 10 );
				NCString::Format( Label, sizeof( Label ), "%.*f s", Decimals, T / 1000.0 );
			}
			if ( X >= Area.Origin.x ) DL->AddText( ImVec2( X + 3.0f, Y + 2.0f ), IM_COL32( 220, 220, 220, 255 ), Label );
		}
	}

	//-------------------------------------------------------------------------
	// 1 フレームのサンプルを描く
	//   FrameOffsetMs: そのフレームの開始が時間軸のどこか（Continuous: 記録の基準からの時間、Stacked: 0）
	//   RowTop: 深さ 0 の段の上端 (y)
	//-------------------------------------------------------------------------
	void DrawFrameSamples( const IProfiler& Profiler, const FProfilerTimelineState& State, const FArea& Area, const FProfilerFrame& Frame, int32 FrameIndex,
						   const TArray<FProfileSample>& Samples, uint32 ThreadId, float64 FrameOffsetMs, float RowTop, float RowHeight, const ImVec2& Mouse, bool bCanHit,
						   FHighlight& Highlight, FHit& InOutHit )
	{
		ImDrawList* DL = Area.DrawList;
		const float64 MsPerTick = 1000.0 / static_cast<float64>( NPlatformTime::GetTicksPerSecond() );
		const bool bHighlighting = Highlight.IsActive();
		const bool bFrameSelected = Frame.FrameNumber == State.SelectedFrameNumber;

		for ( int32 i = 0; i < Samples.Num(); ++i )
		{
			const FProfileSample& Sample = Samples[i];
			const float64 StartMs = FrameOffsetMs + static_cast<float64>( Sample.StartTick - Frame.StartTick ) * MsPerTick;
			const float64 EndMs = StartMs + static_cast<float64>( Sample.DurationTick ) * MsPerTick;

			float X0 = Area.ToX( StartMs );
			float X1 = Area.ToX( EndMs );
			if ( X1 < Area.Origin.x || X0 > Area.Right() ) continue;
			if ( X1 - X0 < 0.6f ) continue;   // 1 ピクセルに満たないものは描かない（フレームの帯や上の段に含まれている）
			X0 = NMath::Max( X0, Area.Origin.x );
			X1 = NMath::Min( X1, Area.Right() );

			const float Y0 = RowTop + RowHeight * static_cast<float>( Sample.Depth );
			const ImVec2 Min( X0, Y0 );
			const ImVec2 Max( X1, Y0 + RowHeight - 1.0f );

			const bool bMatch = !bHighlighting || Highlight.Matches( Sample );
			const bool bHovered = bCanHit && Mouse.x >= Min.x && Mouse.x < Max.x && Mouse.y >= Min.y && Mouse.y < Max.y;
			if ( bHovered )
			{
				InOutHit.FrameIndex = FrameIndex;
				InOutHit.ThreadId = ThreadId;
				InOutHit.SampleIndex = i;
			}

			DL->AddRectFilled( Min, Max, IdColor( Sample.Category, bHovered ? 1.0f : ( bMatch ? 0.78f : 0.30f ), bMatch ? 1.0f : 0.55f ) );
			if ( bHighlighting && bMatch ) DL->AddRect( Min, Max, IM_COL32( 255, 255, 255, 200 ) );
			if ( bFrameSelected && ThreadId == State.SelectedThreadId && i == State.SelectedSampleIndex ) DL->AddRect( ImVec2( Min.x - 1.0f, Min.y - 1.0f ), ImVec2( Max.x + 1.0f, Max.y + 1.0f ), IM_COL32( 255, 255, 80, 255 ), 0.0f, 0, 2.0f );

			if ( X1 - X0 > 38.0f )
			{
				const FString Name = Profiler.GetString( Sample.Name );
				DL->PushClipRect( Min, Max, true );
				DL->AddText( ImVec2( X0 + 3.0f, Y0 + 1.0f ), bMatch ? IM_COL32( 10, 10, 10, 255 ) : IM_COL32( 200, 200, 200, 150 ), UTF8( Name ) );
				DL->PopClipRect();
			}
		}
	}

	void DrawSampleTooltip( const IProfiler& Profiler, const FProfilerFrame& Frame, const FProfileSample& Sample, uint32 ThreadId )
	{
		const float64 TotalMs = NPlatformTime::TicksToMs( Sample.DurationTick );
		ImGui::BeginTooltip();
		ImGui::TextUnformatted( UTF8( Profiler.GetString( Sample.Name ) ) );
		if ( ThreadId != 0 ) ImGui::TextDisabled( "Thread: %s", UTF8( ThreadLabel( Profiler, Frame, ThreadId ) ) );
		ImGui::TextDisabled( "Category: %s", UTF8( Profiler.GetString( Sample.Category ) ) );
		if ( Sample.Owner != 0 ) ImGui::TextDisabled( "Owner: %s", UTF8( Profiler.GetString( Sample.Owner ) ) );
		const FString Instance = NProfilerAnalyzer::MakeInstanceLabel( Profiler, Sample.Instance, Sample.Label );
		if ( !Instance.IsEmpty() ) ImGui::TextDisabled( "Instance: %s", UTF8( Instance ) );
		ImGui::Separator();
		ImGui::Text( "Frame #%llu   +%s ms   depth %u", static_cast<unsigned long long>( Frame.FrameNumber ),
					 UTF8( FormatMs( NPlatformTime::TicksToMs( Sample.StartTick - Frame.StartTick ) ) ), static_cast<unsigned>( Sample.Depth ) );
		ImGui::Text( "Total: %s ms   Self: %s ms", UTF8( FormatMs( TotalMs ) ), UTF8( FormatMs( NPlatformTime::TicksToMs( Sample.GetSelfTick() ) ) ) );
		const int64 FrameTick = Frame.GetDurationTick();
		if ( FrameTick > 0 && ThreadId == 0 ) ImGui::Text( "%.1f%% of the frame", 100.0 * static_cast<float64>( Sample.DurationTick ) / static_cast<float64>( FrameTick ) );
		ImGui::EndTooltip();
	}

	//-------------------------------------------------------------------------
	// 時間軸の入力（ホイール: ズーム / ドラッグ: 移動）。直前の ImGui アイテム（InvisibleButton）に対して呼ぶ
	//-------------------------------------------------------------------------
	void HandleTimeInput( FProfilerTimelineState& State, int32 Mode, bool& bFollow, float OriginX, float Width, bool bWheelScrolls, float64 MaxSpanMs,
						  bool bPanControlsFollow )
	{
		ImGuiIO& IO = ImGui::GetIO();

		if ( ImGui::IsItemHovered() )
		{
			if ( IO.KeyCtrl && IO.MouseWheel != 0.0f )
			{
				// Ctrl+ホイール: カーソル位置を中心にズーム（細かさを変える）
				ZoomAt( State, Mode, bFollow, ( IO.MousePos.x - OriginX ) / Width, IO.MouseWheel, MaxSpanMs );
				ImGui::SetItemKeyOwner( ImGuiKey_MouseWheelY );   // 親のウィンドウをスクロールさせない
			}
			else
			{
				// 左右にスクロール: 横ホイール、ホイール（bWheelScrolls のとき）、Shift+ホイール。上 / 左へ回すと過去へ
				float PanNotches = IO.MouseWheelH;
				if ( !IO.KeyAlt && ( bWheelScrolls || IO.KeyShift ) ) PanNotches += IO.MouseWheel;
				if ( PanNotches != 0.0f )
				{
					PanTo( State, Mode, State.ViewStartMs[Mode] - static_cast<float64>( PanNotches ) * State.ViewSpanMs[Mode] * PanStepFraction, bFollow, bPanControlsFollow );
					ImGui::SetItemKeyOwner( ImGuiKey_MouseWheelY );
				}
			}
		}

		if ( ImGui::IsItemActive() && ImGui::IsMouseDragging( 0, 2.0f ) )
		{
			PanTo( State, Mode, State.ViewStartMs[Mode] - static_cast<float64>( IO.MouseDelta.x ) * State.ViewSpanMs[Mode] / static_cast<float64>( Width ), bFollow, bPanControlsFollow );
		}
	}

	//-------------------------------------------------------------------------
	// ツールバー
	//-------------------------------------------------------------------------
	void DrawToolbar( const IProfiler& Profiler, FProfilerTimelineState& State, bool& bFollow, EPendingAction& OutAction, float64 ViewSpan )
	{
		ImGui::SetNextItemWidth( 260.0f );
		ImGui::Combo( "##TimelineMode", &State.Mode, ModeLabels, IM_ARRAYSIZE( ModeLabels ) );
		if ( ImGui::IsItemHovered() )
		{
			ImGui::SetTooltip( "Continuous: frames side by side on one time axis (like Unreal Insights).\nStacked: one row per frame, aligned at the frame start, to compare frames." );
		}

		ImGui::SameLine();
		if ( ImGui::Button( "Fit all" ) ) OutAction = EPendingAction::FitAll;
		if ( State.Mode == ModeContinuous )
		{
			ImGui::SameLine();
			if ( ImGui::SmallButton( "1 s" ) ) OutAction = EPendingAction::Span1s;
			ImGui::SameLine();
			if ( ImGui::SmallButton( "100 ms" ) ) OutAction = EPendingAction::Span100ms;
			ImGui::SameLine();
			if ( ImGui::SmallButton( "10 ms" ) ) OutAction = EPendingAction::Span10ms;
			ImGui::SameLine();
			if ( ImGui::SmallButton( "1 ms" ) ) OutAction = EPendingAction::Span1ms;
			ImGui::SameLine();
			if ( ImGui::SmallButton( "100 us" ) ) OutAction = EPendingAction::Span100us;
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth( 170.0f );
		ImGui::InputTextWithHint( "##TimelineHighlight", "highlight (name / category / owner)", State.Highlight, sizeof( State.Highlight ) );

		ImGui::SameLine();
		ImGui::SetNextItemWidth( 70.0f );
		ImGui::SliderInt( "row", &State.RowHeight, 10, 30 );

		// Auto Scroll: 最新（右端 / 先頭）へ移って、新しいデータに追従し続ける。横にスクロールすると外れ、右端まで戻ると再び有効になる
		const bool bAutoScrolling = bFollow;
		if ( bAutoScrolling )
		{
			ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.16f, 0.50f, 0.24f, 1.0f ) );
			ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.20f, 0.60f, 0.30f, 1.0f ) );
			ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0.24f, 0.68f, 0.34f, 1.0f ) );
		}
		if ( ImGui::Button( bAutoScrolling ? "Auto Scroll: ON" : "Auto Scroll: OFF" ) ) bFollow = true;
		if ( bAutoScrolling ) ImGui::PopStyleColor( 3 );
		if ( ImGui::IsItemHovered() )
		{
			ImGui::SetTooltip( "Jump to the newest data and keep following it.\nScrolling away turns it off; scrolling back to the right edge turns it on again." );
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth( 90.0f );
		ImGui::SliderFloat( "Scroll rate (Hz)", &State.LiveRefreshHz, 1.0f, 60.0f, "%.0f" );
		if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "How often the view moves while Auto Scroll follows new frames.\nLower = easier to read." );
		ImGui::SameLine();
		ImGui::Checkbox( "Frame separators", &State.bShowFrameSeparators );

		// メインスレッド以外のスレッド（ジョブシステムのワーカーなど）の行
		ImGui::SameLine();
		if ( ImGui::Button( "Threads" ) ) ImGui::OpenPopup( "##TimelineThreads" );
		if ( ImGui::BeginPopup( "##TimelineThreads" ) )
		{
			ImGui::Checkbox( "Show worker threads", &State.bShowThreads );
			ImGui::Separator();
			if ( State.Threads.IsEmpty() ) ImGui::TextDisabled( "No worker thread has recorded anything yet.\nCall NProfiler::SetThreadName() in each worker." );
			for ( FProfilerTimelineThread& Thread : State.Threads )
			{
				const FString Name = Thread.NameId != 0 ? Profiler.GetString( Thread.NameId ) : FString::Printf( "Thread %u", static_cast<unsigned>( Thread.ThreadId ) );
				ImGui::Checkbox( UTF8( FString::Printf( "%s##%u", *Name, static_cast<unsigned>( Thread.ThreadId ) ) ), &Thread.bVisible );
			}
			ImGui::EndPopup();
		}
		ImGui::SameLine();
		ImGui::TextDisabled( "view %s ms", UTF8( FormatMs( ViewSpan ) ) );
		ImGui::SameLine();
		ImGui::TextDisabled( "(?)" );
		if ( ImGui::IsItemHovered() )
		{
			if ( State.Mode == ModeContinuous )
			{
				ImGui::SetTooltip( "Wheel: scroll sideways (stops Auto Scroll)\nCtrl+wheel: zoom around the cursor\nScroll bar / drag: scroll sideways\n"
								   "Reaching the right edge, or Auto Scroll button: follow the newest data again\nClick: select a bar\nDouble-click: zoom to the bar / frame\nAlt+wheel: scroll rows" );
			}
			else
			{
				ImGui::SetTooltip( "Wheel: scroll frames (scrolling down stops Auto Scroll)\nShift+wheel / scroll bar / drag: scroll sideways\nCtrl+wheel: zoom\nClick: select a bar\nDouble-click: zoom to the bar" );
			}
		}
	}

	//-------------------------------------------------------------------------
	// 選んだサンプルの詳細
	//-------------------------------------------------------------------------
	void DrawDetail( IProfiler& Profiler, FProfilerTimelineState& State )
	{
		ImGui::BeginChild( "##TimelineDetail", ImVec2( 0.0f, DetailHeight ), ImGuiChildFlags_Borders );

		const int32 FrameIndex = FindFrameByNumber( Profiler, State.SelectedFrameNumber );
		if ( FrameIndex == INDEX_NONE || !SamplesOf( Profiler.GetFrame( FrameIndex ), State.SelectedThreadId ).IsValidIndex( State.SelectedSampleIndex ) )
		{
			ImGui::TextDisabled( "Click a bar to see its details here." );
			ImGui::EndChild();
			return;
		}

		const FProfilerFrame& Frame = Profiler.GetFrame( FrameIndex );
		const TArray<FProfileSample>& Samples = SamplesOf( Frame, State.SelectedThreadId );
		const FProfileSample& Sample = Samples[State.SelectedSampleIndex];

		// 親をたどった道筋
		TArray<FString> Path;
		for ( int32 Index = State.SelectedSampleIndex; Index != INDEX_NONE; Index = Samples[Index].Parent )
		{
			Path.Add( Profiler.GetString( Samples[Index].Name ) );
		}
		Path.Reverse();

		const FString Instance = NProfilerAnalyzer::MakeInstanceLabel( Profiler, Sample.Instance, Sample.Label );
		ImGui::Text( "%s%s", UTF8( Profiler.GetString( Sample.Name ) ), Instance.IsEmpty() ? "" : *( FString( "  " ) + Instance ) );
		ImGui::SameLine();
		ImGui::TextDisabled( "  [%s]  owner: %s  depth: %u  thread: %s", UTF8( Profiler.GetString( Sample.Category ) ),
							 Sample.Owner != 0 ? UTF8( Profiler.GetString( Sample.Owner ) ) : "-", static_cast<unsigned>( Sample.Depth ),
							 UTF8( ThreadLabel( Profiler, Frame, State.SelectedThreadId ) ) );
		ImGui::TextDisabled( "%s", UTF8( FString::Join( Path, "  >  " ) ) );

		const float64 TotalMs = NPlatformTime::TicksToMs( Sample.DurationTick );
		ImGui::Text( "Frame #%llu  start +%s ms   total %s ms   self %s ms   %.1f%% of the frame", static_cast<unsigned long long>( Frame.FrameNumber ),
					 UTF8( FormatMs( NPlatformTime::TicksToMs( Sample.StartTick - Frame.StartTick ) ) ), UTF8( FormatMs( TotalMs ) ),
					 UTF8( FormatMs( NPlatformTime::TicksToMs( Sample.GetSelfTick() ) ) ),
					 Frame.GetDurationTick() > 0 ? 100.0 * static_cast<float64>( Sample.DurationTick ) / static_cast<float64>( Frame.GetDurationTick() ) : 0.0 );
		ImGui::SameLine();
		if ( ImGui::SmallButton( "Zoom to this" ) )
		{
			State.ViewSpanMs[ModeStacked] = NMath::Max( TotalMs * 1.25, State.MinSpanMs );
			State.ViewStartMs[ModeStacked] = NPlatformTime::TicksToMs( Sample.StartTick - Frame.StartTick ) + TotalMs * 0.5 - State.ViewSpanMs[ModeStacked] * 0.5;
			ZoomToRange( State, ModeContinuous, TickToMs( State, Sample.StartTick ), TickToMs( State, Sample.StartTick + Sample.DurationTick ) );
		}
		ImGui::EndChild();
	}

	//-------------------------------------------------------------------------
	// 俯瞰バー（記録全体。表示している範囲を枠で示し、ドラッグで動かす）
	//-------------------------------------------------------------------------
	void DrawOverview( const IProfiler& Profiler, FProfilerTimelineState& State, bool& bFollow, float Width, float64 DataStartMs, float64 DataEndMs )
	{
		const ImVec2 Origin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton( "##TimelineOverview", ImVec2( Width, OverviewHeight ) );
		const bool bActive = ImGui::IsItemActive();

		ImDrawList* DL = ImGui::GetWindowDrawList();
		DL->AddRectFilled( Origin, ImVec2( Origin.x + Width, Origin.y + OverviewHeight ), IM_COL32( 22, 22, 22, 255 ) );

		const float64 DataSpan = NMath::Max( DataEndMs - DataStartMs, 1.0 );
		const float64 PixelsPerMs = static_cast<float64>( Width ) / DataSpan;
		const float64 MaxMs = NMath::Clamp( static_cast<float64>( State.MaxFrameMsSeen ), 16.7, 66.7 );   // 極端に長い 1 フレームで他が見えなくならないよう、上限を決める
		const float BaseY = Origin.y + OverviewHeight;

		// 全フレームを、画面の幅に収まる数の群にまとめて描く（群の高さ = 中で一番長いフレーム）
		const int32 NumFrames = Profiler.GetNumFrames();
		ForEachFrameGroup( Profiler, State, 0, NumFrames - 1, ChooseGroupLevel( NumFrames, Width ), [&]( int32 FirstIndex, int32 LastIndex, float32 GroupMs )
		{
			const float X0 = Origin.x + static_cast<float>( ( TickToMs( State, Profiler.GetFrame( FirstIndex ).StartTick ) - DataStartMs ) * PixelsPerMs );
			const float X1 = NMath::Max( X0 + 1.0f, Origin.x + static_cast<float>( ( TickToMs( State, Profiler.GetFrame( LastIndex ).EndTick ) - DataStartMs ) * PixelsPerMs ) );
			const float Height = NMath::Min( static_cast<float>( GroupMs / MaxMs ), 1.0f ) * ( OverviewHeight - 4.0f );
			DL->AddRectFilled( ImVec2( X0, BaseY - Height ), ImVec2( X1, BaseY ), FrameBarColor( GroupMs, 255 ) );
		} );

		// 表示している範囲
		const int32 Mode = ModeContinuous;
		const float VX0 = Origin.x + static_cast<float>( NMath::Clamp( ( State.ViewStartMs[Mode] - DataStartMs ) * PixelsPerMs, 0.0, static_cast<float64>( Width ) ) );
		const float VX1 = Origin.x + static_cast<float>( NMath::Clamp( ( State.ViewStartMs[Mode] + State.ViewSpanMs[Mode] - DataStartMs ) * PixelsPerMs, 0.0, static_cast<float64>( Width ) ) );
		DL->AddRectFilled( ImVec2( VX0, Origin.y ), ImVec2( NMath::Max( VX1, VX0 + 2.0f ), BaseY ), IM_COL32( 255, 255, 255, 38 ) );
		DL->AddRect( ImVec2( VX0, Origin.y ), ImVec2( NMath::Max( VX1, VX0 + 2.0f ), BaseY ), IM_COL32( 255, 255, 255, 190 ) );

		if ( bActive )
		{
			const float64 MouseMs = DataStartMs + static_cast<float64>( ImGui::GetIO().MousePos.x - Origin.x ) / PixelsPerMs;
			PanTo( State, Mode, MouseMs - State.ViewSpanMs[Mode] * 0.5, bFollow, true );
		}
		if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Whole recording. Click / drag to move the view." );
	}
}
