#include "ProfilerTimeline.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Library/Core/Collection/Map.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Framework/Profiler/ProfilerAnalyzer.h"
#include "VendorModules/ImGui/imgui.h"

namespace
{
	constexpr float OverviewHeight = 38.0f;
	constexpr float RulerHeight = 20.0f;
	constexpr float FrameTrackHeight = 22.0f;
	constexpr float StackedHeaderHeight = 16.0f;
	constexpr float StackedGap = 6.0f;
	constexpr float DetailHeight = 84.0f;
	constexpr float ScrollbarHeight = 14.0f;
	constexpr float PanStepFraction = 0.15f;   // 横ホイール 1 目盛りで動く量（表示幅に対する割合）
	constexpr float ThreadHeaderHeight = 16.0f;
	constexpr int32 ThreadLookbackFrames = 16;   // 別スレッドの木は始まったフレームに入る。長いジョブが見えている範囲にはみ出すぶんを拾うため、手前のフレームも調べる

	enum EMode : int32
	{
		ModeContinuous = 0,
		ModeStacked = 1,
	};

	// ツールバーのボタンが押されたときの、データを見てから行う操作
	enum class EPendingAction : uint8
	{
		None,
		FitAll,
		Span1s,
		Span100ms,
		Span10ms,
		Span1ms,
		Span100us,
	};

	// ImGui に渡す UTF-8 文字列（FString はシステムのマルチバイト）。式の終わりまで有効
	#define UTF8( Text ) ( *( Text ).ToUtf8() )

	const char* const ModeLabels[] = { "Continuous (frames in one time axis)", "Stacked (one row per frame)" };

	// 文字列 ID ごとに色を決める（同じ Category は同じ色）
	ImU32 IdColor( uint32 Id, float Value = 0.78f, float Alpha = 1.0f )
	{
		const float Hue = std::fmod( static_cast<float>( Id ) * 0.61803398875f, 1.0f );
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
		const float64 Magnitude = std::pow( 10.0, std::floor( std::log10( Raw ) ) );
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

	FORCEINLINE float64 TickToMs( const FProfilerTimelineState& State, int64 Tick )
	{
		return NPlatformTime::TicksToMs( Tick - State.BaseTick );
	}

	//-------------------------------------------------------------------------
	// 検索ハイライト（名前・分類・持ち主・個体名に文字が含まれるか。文字列 ID ごとに結果を覚える）
	//-------------------------------------------------------------------------
	class FHighlight
	{
	public:
		FHighlight( const IProfiler& InProfiler, const char* Utf8Text ) : Profiler( InProfiler ), Needle( FString::FromUtf8( Utf8Text ) ) {}

		bool IsActive() const { return !Needle.IsEmpty(); }

		bool Matches( const FProfileSample& Sample )
		{
			return Test( Sample.Name ) || Test( Sample.Category ) || Test( Sample.Owner ) || Test( Sample.Label );
		}

	private:
		bool Test( uint32 Id )
		{
			if ( Id == 0 ) return false;
			if ( const uint8* Found = Cache.Find( Id ) ) return *Found != 0;
			const bool bMatch = Profiler.GetString( Id ).Contains( Needle, ESearchCase::IgnoreCase );
			Cache.Add( Id, bMatch ? 1 : 0 );
			return bMatch;
		}

		const IProfiler& Profiler;
		FString Needle;
		TMap<uint32, uint8> Cache;
	};

	// マウスの下にあるサンプル
	struct FHit
	{
		int32 FrameIndex = INDEX_NONE;
		uint32 ThreadId = 0;   // 0 = メインスレッド
		int32 SampleIndex = INDEX_NONE;
	};

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

	// 時間軸の描画領域（左端の時刻と、幅あたりの時間）
	struct FArea
	{
		ImDrawList* DrawList = nullptr;
		ImVec2 Origin;      // 領域の左上（画面座標）
		float Width = 1.0f;
		float64 StartMs = 0.0;
		float64 SpanMs = 1.0;

		FORCEINLINE float ToX( float64 Ms ) const { return Origin.x + static_cast<float>( ( Ms - StartMs ) / SpanMs * static_cast<float64>( Width ) ); }
		FORCEINLINE float64 ToMs( float X ) const { return StartMs + static_cast<float64>( X - Origin.x ) / static_cast<float64>( Width ) * SpanMs; }
		FORCEINLINE float Right() const { return Origin.x + Width; }
	};

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

		// 別スレッドの木は、終わってから（数フレーム遅れて）始まったフレームに足されることがあるので、直近のフレームの分は毎回見直す
		for ( int32 i = std::max( 0, NumFrames - 4 ); i < NumFrames; ++i )
		{
			for ( const FProfilerThreadTrack& Track : Profiler.GetFrame( i ).Threads ) NoteSampleDurations( State, Track.Samples );
		}

		// ズームの限界 = 一番短い命令に合わせる（タイマーの 2 カウントより細かくはできない）
		if ( State.MinSampleTicks > 0 ) State.MinSpanMs = NPlatformTime::TicksToMs( std::max<int64>( State.MinSampleTicks, 2 ) );
	}

	// 表示するフレーム数と幅から、フレームを 2^Level 個ずつまとめる段を選ぶ（まとめた群が画面で 3 ピクセル程度になるように）。0 ならまとめない
	int32 ChooseGroupLevel( int32 NumFrames, float Width )
	{
		const int32 MaxEntries = std::max( 1, static_cast<int32>( Width / 3.0f ) );
		int32 Level = 0;
		while ( ( NumFrames >> Level ) > MaxEntries && Level < 30 ) ++Level;
		return Level;
	}

	// FirstFrame..LastFrame を 2^Level 個ずつの群に分けて、群ごとに Callback( 最初のフレームのインデックス, 最後のインデックス, 群の中で一番長い時間 ) を呼ぶ。
	// 群の区切りはフレーム番号に対して固定なので、表示を動かしても境目は動かない。1 フレームずつ見るより圧倒的に速い
	template<typename TCallback>
	void ForEachFrameGroup( const IProfiler& Profiler, const FProfilerTimelineState& State, int32 FirstFrame, int32 LastFrame, int32 Level, TCallback&& Callback )
	{
		const int32 NumFrames = Profiler.GetNumFrames();
		if ( NumFrames <= 0 || FirstFrame > LastFrame || Level >= State.PyramidLevels.Num() ) return;

		const int64 OldestId = static_cast<int64>( Profiler.GetFrame( 0 ).FrameNumber ) - static_cast<int64>( State.PyramidBaseNumber );
		if ( OldestId < 0 ) return;
		const TArray<float32>& Entries = State.PyramidLevels[Level];

		const int64 FirstGroup = ( OldestId + FirstFrame ) >> Level;
		const int64 LastGroup = ( OldestId + LastFrame ) >> Level;
		for ( int64 Group = FirstGroup; Group <= LastGroup; ++Group )
		{
			const int64 FirstIndex = std::max<int64>( ( Group << Level ) - OldestId, 0 );
			const int64 LastIndex = std::min<int64>( ( ( Group + 1 ) << Level ) - 1 - OldestId, NumFrames - 1 );
			if ( FirstIndex > LastIndex ) continue;
			const float32 GroupMs = Group < Entries.Num() ? Entries[static_cast<int32>( Group )] : 0.0f;
			Callback( static_cast<int32>( FirstIndex ), static_cast<int32>( LastIndex ), GroupMs );
		}
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
		const float64 Span = std::max( Duration * 1.25, State.MinSpanMs );
		State.ViewSpanMs[Mode] = Span;
		State.ViewStartMs[Mode] = StartMs + Duration * 0.5 - Span * 0.5;
	}

	// ホイールで、AnchorFraction（0..1）の位置を動かさずに拡大・縮小する
	void ZoomAt( FProfilerTimelineState& State, int32 Mode, bool bFollow, float AnchorFraction, float Wheel, float64 MaxSpanMs )
	{
		const float64 OldSpan = State.ViewSpanMs[Mode];
		const float64 NewSpan = std::clamp( OldSpan * std::pow( 0.78, static_cast<float64>( Wheel ) ), State.MinSpanMs, MaxSpanMs );
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
			OutMinStart = std::min( State.PanMinMs, OutMaxStart );
		}
		else
		{
			OutMinStart = State.PanMinMs;
			OutMaxStart = std::max( State.PanMinMs, State.PanMaxMs - Span );
		}
	}

	void ClampView( FProfilerTimelineState& State, int32 Mode )
	{
		float64 MinStart = 0.0, MaxStart = 0.0;
		GetPanLimits( State, Mode, MinStart, MaxStart );
		State.ViewStartMs[Mode] = std::clamp( State.ViewStartMs[Mode], MinStart, MaxStart );
	}

	// ユーザーの操作で表示の左端を動かす。範囲の外へは出さない。
	// bControlsFollow のとき、右端（最新）まで行ったら追従（Auto Scroll）を再開し、右端から離れたら止める
	void PanTo( FProfilerTimelineState& State, int32 Mode, float64 NewStartMs, bool& bFollow, bool bControlsFollow )
	{
		float64 MinStart = 0.0, MaxStart = 0.0;
		GetPanLimits( State, Mode, MinStart, MaxStart );
		const bool bAtEnd = NewStartMs >= MaxStart - 1.0e-6;
		State.ViewStartMs[Mode] = std::clamp( NewStartMs, MinStart, MaxStart );
		if ( bControlsFollow ) bFollow = bAtEnd;
	}

	// 横スクロールバー（時間軸の下）。つまみ = 今見えている範囲。右端まで動かすと Auto Scroll が戻る
	void DrawHScrollbar( FProfilerTimelineState& State, int32 Mode, bool& bFollow, float Width, bool bControlsFollow )
	{
		ImGuiIO& IO = ImGui::GetIO();
		const ImVec2 Origin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton( "##TimelineHScroll", ImVec2( Width, ScrollbarHeight ) );
		const bool bHovered = ImGui::IsItemHovered();

		const float64 Range = std::max( State.PanMaxMs - State.PanMinMs, 1.0e-6 );
		const float64 Span = State.ViewSpanMs[Mode];
		const float64 Scrollable = Range - Span;   // 動かせる幅
		const bool bScrollable = Scrollable > 1.0e-6;

		const float ThumbWidth = bScrollable ? std::clamp( static_cast<float>( static_cast<float64>( Width ) * Span / Range ), 24.0f, Width ) : Width;
		const float Free = Width - ThumbWidth;
		const auto ThumbLeft = [&]() -> float
		{
			if ( !bScrollable ) return 0.0f;
			return static_cast<float>( std::clamp( ( State.ViewStartMs[Mode] - State.PanMinMs ) / Scrollable, 0.0, 1.0 ) ) * Free;
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

	//-------------------------------------------------------------------------
	// 目盛り（Continuous は記録の基準からの時間、Stacked はフレーム開始からの時間）
	//-------------------------------------------------------------------------
	void DrawRuler( const FArea& Area, float Y, bool bStacked )
	{
		ImDrawList* DL = Area.DrawList;
		const float64 MsPerPixel = Area.SpanMs / static_cast<float64>( Area.Width );
		float64 Step = NiceStep( MsPerPixel * 90.0 );
		if ( Step < 0.001 ) Step = NiceStep( MsPerPixel * 135.0 );   // ごく細かいときは、ラベル（小数の桁数）が長くなるので間隔を広げる
		const float64 First = std::floor( Area.StartMs / Step ) * Step;

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
				const int32 Decimals = Step >= 1.0 ? 0 : std::clamp( static_cast<int32>( std::ceil( -std::log10( Step ) ) ), 0, 8 );
				std::snprintf( Label, sizeof( Label ), "%.*f ms", Decimals, T );
			}
			else
			{
				// 秒で表示する。目盛りの細かさに合わせて小数の桁数を決める
				const float64 StepSeconds = Step / 1000.0;
				const int32 Decimals = StepSeconds >= 1.0 ? 0 : std::clamp( static_cast<int32>( std::ceil( -std::log10( StepSeconds ) ) ), 0, 10 );
				std::snprintf( Label, sizeof( Label ), "%.*f s", Decimals, T / 1000.0 );
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
			X0 = std::max( X0, Area.Origin.x );
			X1 = std::min( X1, Area.Right() );

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

	void DrawSampleTooltip( const IProfiler& Profiler, const FProfilerFrame& Frame, const FProfileSample& Sample, uint32 ThreadId = 0 )
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
		ImGui::Text( "%s%s", UTF8( Profiler.GetString( Sample.Name ) ), Instance.IsEmpty() ? "" : *( FString( "  " ) + Instance ).ToUtf8() );
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
			State.ViewSpanMs[ModeStacked] = std::max( TotalMs * 1.25, State.MinSpanMs );
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

		const float64 DataSpan = std::max( DataEndMs - DataStartMs, 1.0 );
		const float64 PixelsPerMs = static_cast<float64>( Width ) / DataSpan;
		const float64 MaxMs = std::clamp( static_cast<float64>( State.MaxFrameMsSeen ), 16.7, 66.7 );   // 極端に長い 1 フレームで他が見えなくならないよう、上限を決める
		const float BaseY = Origin.y + OverviewHeight;

		// 全フレームを、画面の幅に収まる数の群にまとめて描く（群の高さ = 中で一番長いフレーム）
		const int32 NumFrames = Profiler.GetNumFrames();
		ForEachFrameGroup( Profiler, State, 0, NumFrames - 1, ChooseGroupLevel( NumFrames, Width ), [&]( int32 FirstIndex, int32 LastIndex, float32 GroupMs )
		{
			const float X0 = Origin.x + static_cast<float>( ( TickToMs( State, Profiler.GetFrame( FirstIndex ).StartTick ) - DataStartMs ) * PixelsPerMs );
			const float X1 = std::max( X0 + 1.0f, Origin.x + static_cast<float>( ( TickToMs( State, Profiler.GetFrame( LastIndex ).EndTick ) - DataStartMs ) * PixelsPerMs ) );
			const float Height = std::min( static_cast<float>( GroupMs / MaxMs ), 1.0f ) * ( OverviewHeight - 4.0f );
			DL->AddRectFilled( ImVec2( X0, BaseY - Height ), ImVec2( X1, BaseY ), FrameBarColor( GroupMs, 255 ) );
		} );

		// 表示している範囲
		const int32 Mode = ModeContinuous;
		const float VX0 = Origin.x + static_cast<float>( std::clamp( ( State.ViewStartMs[Mode] - DataStartMs ) * PixelsPerMs, 0.0, static_cast<float64>( Width ) ) );
		const float VX1 = Origin.x + static_cast<float>( std::clamp( ( State.ViewStartMs[Mode] + State.ViewSpanMs[Mode] - DataStartMs ) * PixelsPerMs, 0.0, static_cast<float64>( Width ) ) );
		DL->AddRectFilled( ImVec2( VX0, Origin.y ), ImVec2( std::max( VX1, VX0 + 2.0f ), BaseY ), IM_COL32( 255, 255, 255, 38 ) );
		DL->AddRect( ImVec2( VX0, Origin.y ), ImVec2( std::max( VX1, VX0 + 2.0f ), BaseY ), IM_COL32( 255, 255, 255, 190 ) );

		if ( bActive )
		{
			const float64 MouseMs = DataStartMs + static_cast<float64>( ImGui::GetIO().MousePos.x - Origin.x ) / PixelsPerMs;
			PanTo( State, Mode, MouseMs - State.ViewSpanMs[Mode] * 0.5, bFollow, true );
		}
		if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Whole recording. Click / drag to move the view." );
	}

	//-------------------------------------------------------------------------
	// Continuous: 複数のフレームを 1 本の時間軸に並べる
	//-------------------------------------------------------------------------
	void DrawContinuous( IProfiler& Profiler, FProfilerTimelineState& State, bool& bFollow, float Width, float64 DataStartMs, float64 DataEndMs, float ChildHeight )
	{
		ImGuiIO& IO = ImGui::GetIO();
		const int32 Mode = ModeContinuous;
		const float64 DataSpan = std::max( DataEndMs - DataStartMs, 1.0 );
		const float64 MaxSpanMs = std::max( DataSpan * 1.1, 10.0 );

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
		const int32 NumVisible = std::max( 0, LastFrame - FirstFrame + 1 );
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
					const float L = std::max( X0, HeaderOrigin.x );
					const float R = std::min( X1, HeaderOrigin.x + Width );
					if ( R <= L ) return;

					const bool bHovered = bHeaderHovered && IO.MousePos.x >= L && IO.MousePos.x < R && IO.MousePos.y >= Y0;
					if ( bHovered )
					{
						HoveredGroupFirst = FirstIndex;
						HoveredGroupLast = LastIndex;
					}
					DL->AddRectFilled( ImVec2( L, Y0 + 1.0f ), ImVec2( std::max( R - 1.0f, L + 1.0f ), Y0 + FrameTrackHeight - 1.0f ), FrameBarColor( GroupMs, bHovered ? 255 : 190 ) );
				} );
			}

			for ( int32 i = FirstFrame; GroupLevel == 0 && i <= LastFrame && i < Profiler.GetNumFrames(); ++i )
			{
				const FProfilerFrame& Frame = Profiler.GetFrame( i );
				const float64 Ms = NPlatformTime::TicksToMs( Frame.GetDurationTick() );
				const float X0 = Header.ToX( TickToMs( State, Frame.StartTick ) );
				const float X1 = Header.ToX( TickToMs( State, Frame.EndTick ) );
				const float L = std::max( X0, HeaderOrigin.x );
				const float R = std::min( X1, HeaderOrigin.x + Width );
				if ( R <= L ) continue;

				const bool bHovered = bHeaderHovered && IO.MousePos.x >= L && IO.MousePos.x < R && IO.MousePos.y >= Y0;
				if ( bHovered ) HoveredFrame = i;
				DL->AddRectFilled( ImVec2( L, Y0 + 1.0f ), ImVec2( std::max( R - 1.0f, L + 1.0f ), Y0 + FrameTrackHeight - 1.0f ), FrameBarColor( Ms, bHovered ? 255 : 190 ) );
				if ( R - L > 74.0f )
				{
					char Label[40];
					std::snprintf( Label, sizeof( Label ), "#%llu  %.2f ms", static_cast<unsigned long long>( Frame.FrameNumber ), Ms );
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
		for ( int32 i = FirstFrame; GroupLevel == 0 && i <= LastFrame && i < TotalFrames; ++i ) MaxDepth = std::max( MaxDepth, Profiler.GetFrame( i ).MaxDepth );

		// メインスレッド以外のスレッドの行（見えているフレームに記録があるスレッドだけ。縮小表示のときは出さない）
		TArray<int32> ActiveThreads;   // State.Threads のインデックス
		const int32 ThreadScanFirst = std::max( 0, FirstFrame - ThreadLookbackFrames );
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
					Thread.ViewMaxDepth = std::max( Thread.ViewMaxDepth, Track.MaxDepth );
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
		const float ContentHeight = std::max( ContentBottom + 6.0f, ChildHeight - 4.0f );

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
			const float L = std::max( X0, SamplesOrigin.x );
			const float R = std::min( X1, SamplesOrigin.x + Width );
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
				const float Left = std::max( X0, SamplesOrigin.x );
				Area.DrawList->AddRectFilled( ImVec2( Left, SamplesOrigin.y + 2.0f ), ImVec2( std::max( X1, Left + 1.0f ), SamplesOrigin.y + 2.0f + RowHeight - 1.0f ),
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
				const float Left = std::max( X0, SamplesOrigin.x );
				const float Right = std::min( X1, SamplesOrigin.x + Width );
				if ( Right <= Left ) return;
				Area.DrawList->AddRectFilled( ImVec2( Left, SamplesOrigin.y + 2.0f ), ImVec2( std::max( Right - 1.0f, Left + 1.0f ), SamplesOrigin.y + 2.0f + RowHeight - 1.0f ),
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

	//-------------------------------------------------------------------------
	// Stacked: 1 フレームを 1 段にして縦に積む（新しいものが上）
	//-------------------------------------------------------------------------
	void DrawStacked( IProfiler& Profiler, FProfilerTimelineState& State, bool& bFollow, float Width, float ChildHeight )
	{
		ImGuiIO& IO = ImGui::GetIO();
		const int32 Mode = ModeStacked;
		const int32 NumFrames = Profiler.GetNumFrames();
		const float64 MaxSpanMs = std::max( static_cast<float64>( State.MaxFrameMsSeen ) * 2.0, 4.0 );

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
		for ( int32 i = std::max( 0, TopIndex - 255 ); i <= TopIndex; ++i ) MaxDepth = std::max( MaxDepth, Profiler.GetFrame( i ).MaxDepth );
		const float Stride = StackedHeaderHeight + RowHeight * static_cast<float>( MaxDepth + 1 ) + StackedGap;
		const float TotalHeight = Stride * static_cast<float>( TopIndex + 1 );

		ImGui::BeginChild( "##TimelineStackedSamples", ImVec2( 0.0f, ChildHeight ), ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar );
		const ImVec2 SamplesOrigin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton( "##TimelineStackedHit", ImVec2( Width, std::max( TotalHeight, 1.0f ) ) );
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
		const int32 FirstGroup = std::max( 0, static_cast<int32>( std::floor( ScrollY / Stride ) ) );
		const int32 LastGroup = std::min( TopIndex, static_cast<int32>( std::floor( ( ScrollY + ChildHeight ) / Stride ) ) );
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
			std::snprintf( Label, sizeof( Label ), "#%llu   %.2f ms", static_cast<unsigned long long>( Frame.FrameNumber ), FrameMs );
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

	#undef UTF8
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
	const float64 LongestMs = std::max( static_cast<float64>( State.MaxFrameMsSeen ), 1.0 );

	// 最初は、最新のフレーム数枚分が見えるように合わせる（そのあとは勝手に動かさない）
	if ( State.bViewNeedsPlacement )
	{
		State.bViewNeedsPlacement = false;
		State.ViewSpanMs[ModeContinuous] = std::clamp( NewestMs * 8.0, 10.0, 200.0 );
		State.ViewStartMs[ModeContinuous] = DataEndMs - State.ViewSpanMs[ModeContinuous];
		State.StackedPinnedFrameNumber = Newest.FrameNumber;
		State.LiveFrameNumber = Newest.FrameNumber;
		State.LiveEndMs = DataEndMs;
	}

	if ( !State.bStackedInitialized )
	{
		State.bStackedInitialized = true;
		State.ViewStartMs[ModeStacked] = 0.0;
		State.ViewSpanMs[ModeStacked] = std::clamp( NewestMs * 1.3, 2.0, std::max( LongestMs * 1.05, 2.0 ) );   // std::clamp は、下限が上限より大きいと assert する
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
			State.ViewSpanMs[ModeContinuous] = std::max( DataEndMs - DataStartMs, 1.0 ) * 1.02;
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
	State.ViewSpanMs[ModeContinuous] = std::max( State.ViewSpanMs[ModeContinuous], State.MinSpanMs );
	if ( Action >= EPendingAction::Span1s && !bAutoScroll )
	{
		// Auto Scroll していなければ、今の表示の中央を保ったまま幅を変える
		State.ViewStartMs[ModeContinuous] = OldCenterMs - State.ViewSpanMs[ModeContinuous] * 0.5;
	}

	//-------------------------------------------------------------------------
	// 窓全体で選ばれたフレーム（フレームグラフのクリックなど）へ画面を合わせる。Auto Scroll は止まる
	//-------------------------------------------------------------------------
	if ( SelectedFrameNumber != 0 && SelectedFrameNumber != State.FocusedFrameNumber )
	{
		const int32 Index = FindFrameByNumber( Profiler, SelectedFrameNumber );
		if ( Index != INDEX_NONE )
		{
			const FProfilerFrame& Frame = Profiler.GetFrame( Index );
			ZoomToRange( State, ModeContinuous, TickToMs( State, Frame.StartTick ), TickToMs( State, Frame.EndTick ) );
			State.StackedPinnedFrameNumber = Profiler.GetFrame( std::min( Index + 5, NumFrames - 1 ) ).FrameNumber;   // 選んだフレームが先頭の少し下に見えるように
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
		const float64 Hz = std::max( static_cast<float64>( State.LiveRefreshHz ), 1.0 );
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
	const float Width = std::max( 140.0f, ImGui::GetContentRegionAvail().x - Style.ScrollbarSize );

	const float FixedAbove = ( State.Mode == ModeContinuous ? OverviewHeight + RulerHeight + FrameTrackHeight + 3.0f * Style.ItemSpacing.y : RulerHeight + Style.ItemSpacing.y );
	const float BottomReserve = ImGui::GetTextLineHeightWithSpacing() + 6.0f;   // タブの下の状態表示の行
	const float ChildHeight = std::max( 90.0f, ImGui::GetContentRegionAvail().y - FixedAbove - ScrollbarHeight - Style.ItemSpacing.y - DetailHeight - 2.0f * Style.ItemSpacing.y - BottomReserve );

	if ( State.Mode == ModeContinuous ) DrawContinuous( Profiler, State, bAutoScroll, Width, DataStartMs, DataEndMs, ChildHeight );
	else DrawStacked( Profiler, State, bAutoScroll, Width, ChildHeight );

	DrawDetail( Profiler, State );
}
