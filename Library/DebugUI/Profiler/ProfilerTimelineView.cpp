#include "ProfilerTimelineInternal.h"

namespace NProfilerTimelineUI
{
	//-------------------------------------------------------------------------
	// 範囲の操作
	//-------------------------------------------------------------------------
	// フレームの時間の最大値の表（履歴が長くなっても、縮小表示を速く描くための控え）
	void ResetPyramid( FProfilerTimelineState& State, uint64 BaseNumber )
	{
		State.PyramidLevels.Reset();
		State.PyramidBaseNumber = BaseNumber;
		State.PyramidNextNumber = BaseNumber;
		State.MaxFrameMsSeen = 0.0f;
		State.MinSampleTicks = 0;
		State.MinSpanMs = 0.002;
		State.Threads.Reset();
	}

	// 一番短いサンプルの長さを覚える（ズームの限界に使う）。0 カウントのものは、タイマーの細かさより短いので数えない
	void NoteSampleDurations( FProfilerTimelineState& State, const TArray<FProfileSample>& Samples )
	{
		for ( const FProfileSample& Sample : Samples )
		{
			if ( Sample.DurationTick > 0 && ( State.MinSampleTicks == 0 || Sample.DurationTick < State.MinSampleTicks ) ) State.MinSampleTicks = Sample.DurationTick;
		}
	}

	// 新しい段を足す。1 つ下の段から作る（その段までの記録を取りこぼさないため）
	void AddPyramidLevel( FProfilerTimelineState& State )
	{
		const int32 Level = State.PyramidLevels.Num();
		State.PyramidLevels.Emplace();
		if ( Level == 0 ) return;

		const TArray<float32>& Below = State.PyramidLevels[Level - 1];
		TArray<float32>& Entries = State.PyramidLevels[Level];
		for ( int32 i = 0; i < Below.Num(); ++i )
		{
			const int32 Index = i >> 1;
			while ( Entries.Num() <= Index ) Entries.Add( 0.0f );
			if ( Below[i] > Entries[Index] ) Entries[Index] = Below[i];
		}
	}

	// まだ表に入れていないフレームを足す（新しい分だけ。1 フレームあたり 20 段ほどの更新）
	void UpdatePyramid( const IProfiler& Profiler, FProfilerTimelineState& State )
	{
		const int32 NumFrames = Profiler.GetNumFrames();
		int32 Low = 0, High = NumFrames;
		while ( Low < High )
		{
			const int32 Mid = ( Low + High ) / 2;
			if ( Profiler.GetFrame( Mid ).FrameNumber < State.PyramidNextNumber ) Low = Mid + 1;
			else High = Mid;
		}

		for ( int32 i = Low; i < NumFrames; ++i )
		{
			const FProfilerFrame& Frame = Profiler.GetFrame( i );
			State.PyramidNextNumber = Frame.FrameNumber + 1;
			if ( Frame.FrameNumber < State.PyramidBaseNumber ) continue;

			const uint64 Id = Frame.FrameNumber - State.PyramidBaseNumber;
			const float32 Ms = static_cast<float32>( NPlatformTime::TicksToMs( Frame.GetDurationTick() ) );
			if ( Ms > State.MaxFrameMsSeen ) State.MaxFrameMsSeen = Ms;
			NoteSampleDurations( State, Frame.Samples );
			for ( const FProfilerThreadTrack& Track : Frame.Threads ) NoteSampleDurations( State, Track.Samples );

			// 一番上の段は、Id の 2 進数の桁数より 1 つ上（そこでは全体で 1 つの値になる）
			int32 Top = 0;
			while ( ( ( Id + 1 ) >> Top ) > 0 ) ++Top;
			for ( int32 Level = 0; Level <= Top; ++Level )
			{
				if ( State.PyramidLevels.Num() <= Level ) AddPyramidLevel( State );
				TArray<float32>& Entries = State.PyramidLevels[Level];
				const int32 Index = static_cast<int32>( Id >> Level );
				while ( Entries.Num() <= Index ) Entries.Add( 0.0f );
				if ( Ms > Entries[Index] ) Entries[Index] = Ms;
			}
		}

		// 別スレッドのツリーは、終わってから（数フレーム遅れて）始まったフレームに足されることがあるので、直近のフレームの分は毎回見直す
		for ( int32 i = NMath::Max( 0, NumFrames - 4 ); i < NumFrames; ++i )
		{
			for ( const FProfilerThreadTrack& Track : Profiler.GetFrame( i ).Threads ) NoteSampleDurations( State, Track.Samples );
		}

		// ズームの限界 = 一番短い命令に合わせる（タイマーの 2 カウントより細かくはできない）
		if ( State.MinSampleTicks > 0 ) State.MinSpanMs = NPlatformTime::TicksToMs( NMath::Max<int64>( State.MinSampleTicks, 2 ) );
	}

	// 表示するフレーム数と幅から、フレームを 2^Level 個ずつまとめる段を選ぶ（まとめた群が画面で 3 ピクセル程度になるように）。0 ならまとめない
	int32 ChooseGroupLevel( int32 NumFrames, float Width )
	{
		const int32 MaxEntries = NMath::Max( 1, static_cast<int32>( Width / 3.0f ) );
		int32 Level = 0;
		while ( ( NumFrames >> Level ) > MaxEntries && Level < 30 ) ++Level;
		return Level;
	}

	void SetSpan( FProfilerTimelineState& State, int32 Mode, float64 SpanMs, float64 AnchorMs, float AnchorFraction )
	{
		State.ViewSpanMs[Mode] = SpanMs;
		State.ViewStartMs[Mode] = AnchorMs - SpanMs * static_cast<float64>( AnchorFraction );
	}

	// StartMs..EndMs が余白付きで収まるように表示する
	void ZoomToRange( FProfilerTimelineState& State, int32 Mode, float64 StartMs, float64 EndMs )
	{
		const float64 Duration = EndMs - StartMs;
		const float64 Span = NMath::Max( Duration * 1.25, State.MinSpanMs );
		State.ViewSpanMs[Mode] = Span;
		State.ViewStartMs[Mode] = StartMs + Duration * 0.5 - Span * 0.5;
	}

	// ホイールで、AnchorFraction（0..1）の位置を動かさずに拡大・縮小する
	void ZoomAt( FProfilerTimelineState& State, int32 Mode, bool bFollow, float AnchorFraction, float Wheel, float64 MaxSpanMs )
	{
		const float64 OldSpan = State.ViewSpanMs[Mode];
		const float64 NewSpan = NMath::Clamp( OldSpan * NMath::Pow( 0.78, static_cast<float64>( Wheel ) ), State.MinSpanMs, MaxSpanMs );
		if ( Mode == ModeContinuous && bFollow )
		{
			// 追従中は右端（最新）を動かさない
			State.ViewStartMs[Mode] += OldSpan - NewSpan;
			State.ViewSpanMs[Mode] = NewSpan;
			return;
		}
		const float64 Anchor = State.ViewStartMs[Mode] + OldSpan * static_cast<float64>( AnchorFraction );
		SetSpan( State, Mode, NewSpan, Anchor, AnchorFraction );
	}

	// 表示の左端が取れる範囲。Continuous は最新（右端）に、Stacked はフレームの開始（左端）にそろえる
	void GetPanLimits( const FProfilerTimelineState& State, int32 Mode, float64& OutMinStart, float64& OutMaxStart )
	{
		const float64 Span = State.ViewSpanMs[Mode];
		if ( Mode == ModeContinuous )
		{
			OutMaxStart = State.PanMaxMs - Span;
			OutMinStart = NMath::Min( State.PanMinMs, OutMaxStart );
		}
		else
		{
			OutMinStart = State.PanMinMs;
			OutMaxStart = NMath::Max( State.PanMinMs, State.PanMaxMs - Span );
		}
	}

	void ClampView( FProfilerTimelineState& State, int32 Mode )
	{
		float64 MinStart = 0.0, MaxStart = 0.0;
		GetPanLimits( State, Mode, MinStart, MaxStart );
		State.ViewStartMs[Mode] = NMath::Clamp( State.ViewStartMs[Mode], MinStart, MaxStart );
	}

	// ユーザーの操作で表示の左端を動かす。範囲の外へは出さない。
	// bControlsFollow のとき、右端（最新）まで行ったら追従（Auto Scroll）を再開し、右端から離れたら止める
	void PanTo( FProfilerTimelineState& State, int32 Mode, float64 NewStartMs, bool& bFollow, bool bControlsFollow )
	{
		float64 MinStart = 0.0, MaxStart = 0.0;
		GetPanLimits( State, Mode, MinStart, MaxStart );
		const bool bAtEnd = NewStartMs >= MaxStart - 1.0e-6;
		State.ViewStartMs[Mode] = NMath::Clamp( NewStartMs, MinStart, MaxStart );
		if ( bControlsFollow ) bFollow = bAtEnd;
	}

	// 横スクロールバー（時間軸の下）。つまみ = 今見えている範囲。右端まで動かすと Auto Scroll が戻る
	void DrawHScrollbar( FProfilerTimelineState& State, int32 Mode, bool& bFollow, float Width, bool bControlsFollow )
	{
		ImGuiIO& IO = ImGui::GetIO();
		const ImVec2 Origin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton( "##TimelineHScroll", ImVec2( Width, ScrollbarHeight ) );
		const bool bHovered = ImGui::IsItemHovered();

		const float64 Range = NMath::Max( State.PanMaxMs - State.PanMinMs, 1.0e-6 );
		const float64 Span = State.ViewSpanMs[Mode];
		const float64 Scrollable = Range - Span;   // 動かせる幅
		const bool bScrollable = Scrollable > 1.0e-6;

		const float ThumbWidth = bScrollable ? NMath::Clamp( static_cast<float>( static_cast<float64>( Width ) * Span / Range ), 24.0f, Width ) : Width;
		const float Free = Width - ThumbWidth;
		const auto ThumbLeft = [&]() -> float
		{
			if ( !bScrollable ) return 0.0f;
			return static_cast<float>( NMath::Clamp( ( State.ViewStartMs[Mode] - State.PanMinMs ) / Scrollable, 0.0, 1.0 ) ) * Free;
		};

		if ( bScrollable )
		{
			const float MouseX = IO.MousePos.x - Origin.x;
			if ( ImGui::IsItemActivated() )
			{
				// つまみを掴んだらその位置のまま、外を押したらつまみの中央をカーソルに合わせる
				const float Left = ThumbLeft();
				State.ScrollGrabPx = ( MouseX >= Left && MouseX <= Left + ThumbWidth ) ? MouseX - Left : ThumbWidth * 0.5f;
			}
			if ( ImGui::IsItemActive() && Free > 0.0f )
			{
				const float64 T = static_cast<float64>( ( MouseX - State.ScrollGrabPx ) / Free );   // 0 が左端、1 以上が右端
				PanTo( State, Mode, State.PanMinMs + T * Scrollable, bFollow, bControlsFollow );
			}
			else if ( bHovered && ( IO.MouseWheel != 0.0f || IO.MouseWheelH != 0.0f ) )
			{
				const float Notches = IO.MouseWheel + IO.MouseWheelH;   // 上 / 左へ回すと過去へ
				PanTo( State, Mode, State.ViewStartMs[Mode] - static_cast<float64>( Notches ) * Span * PanStepFraction, bFollow, bControlsFollow );
				ImGui::SetItemKeyOwner( ImGuiKey_MouseWheelY );
			}
		}

		ImDrawList* DL = ImGui::GetWindowDrawList();
		DL->AddRectFilled( Origin, ImVec2( Origin.x + Width, Origin.y + ScrollbarHeight ), IM_COL32( 24, 24, 24, 255 ), 3.0f );
		if ( bScrollable )
		{
			const float Left = Origin.x + ThumbLeft();
			const ImU32 Color = ImGui::IsItemActive() ? IM_COL32( 170, 170, 170, 255 ) : ( bHovered ? IM_COL32( 135, 135, 135, 255 ) : IM_COL32( 100, 100, 100, 255 ) );
			DL->AddRectFilled( ImVec2( Left, Origin.y + 2.0f ), ImVec2( Left + ThumbWidth, Origin.y + ScrollbarHeight - 2.0f ), Color, 3.0f );
		}
		else
		{
			DL->AddRectFilled( ImVec2( Origin.x, Origin.y + 2.0f ), ImVec2( Origin.x + Width, Origin.y + ScrollbarHeight - 2.0f ), IM_COL32( 55, 55, 55, 255 ), 3.0f );
		}
	}
}
