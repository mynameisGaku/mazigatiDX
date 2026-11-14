#pragma once
#include "ProfilerTimeline.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Profiler/ProfilerAnalyzer.h"
#include "Library/Math/MathUtils.h"
#include "VendorModules/ImGui/imgui.h"

//=============================================================================
// プロファイラのタイムラインの中で、ファイルをまたいで使うもの（ProfilerTimeline*.cpp 専用）
//=============================================================================
namespace NProfilerTimelineUI
{
	inline constexpr float OverviewHeight = 38.0f;
	inline constexpr float RulerHeight = 20.0f;
	inline constexpr float FrameTrackHeight = 22.0f;
	inline constexpr float StackedHeaderHeight = 16.0f;
	inline constexpr float StackedGap = 6.0f;
	inline constexpr float DetailHeight = 84.0f;
	inline constexpr float ScrollbarHeight = 14.0f;
	inline constexpr float PanStepFraction = 0.15f;   // 横ホイール 1 目盛りで動く量（表示幅に対する割合）
	inline constexpr float ThreadHeaderHeight = 16.0f;
	inline constexpr int32 ThreadLookbackFrames = 16;   // 別スレッドのツリーは始まったフレームに入る。長いジョブが見えている範囲にはみ出すぶんを拾うため、手前のフレームも調べる

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

	// ImGui に渡す文字列（FString は UTF-8 なので、そのまま渡せる）。式の終わりまで有効
	#define UTF8( Text ) ( *( Text ) )
	inline const char* const ModeLabels[] = { "Continuous (frames in one time axis)", "Stacked (one row per frame)" };

	// ---- ProfilerTimeline.cpp ----
	ImU32 IdColor( uint32 Id, float Value = 0.78f, float Alpha = 1.0f );
	ImU32 FrameBarColor( float64 Ms, int32 Alpha );
	FString FormatMs( float64 Ms );
	float64 NiceStep( float64 Raw );
	int32 FindFrameByNumber( const IProfiler& Profiler, uint64 FrameNumber );
	int32 FirstFrameEndingAtOrAfter( const IProfiler& Profiler, int64 Tick );
	int32 LastFrameStartingAtOrBefore( const IProfiler& Profiler, int64 Tick );

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
		FHighlight( const IProfiler& InProfiler, const char* Utf8Text ) : Profiler( InProfiler ), Needle( FString( Utf8Text ) ) {}

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

	// ---- ProfilerTimeline.cpp ----
	const TArray<FProfileSample>& SamplesOf( const FProfilerFrame& Frame, uint32 ThreadId );
	FString ThreadLabel( const IProfiler& Profiler, const FProfilerFrame& Frame, uint32 ThreadId );

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

	// ---- ProfilerTimelineView.cpp ----
	void ResetPyramid( FProfilerTimelineState& State, uint64 BaseNumber );
	void NoteSampleDurations( FProfilerTimelineState& State, const TArray<FProfileSample>& Samples );
	void AddPyramidLevel( FProfilerTimelineState& State );
	void UpdatePyramid( const IProfiler& Profiler, FProfilerTimelineState& State );
	int32 ChooseGroupLevel( int32 NumFrames, float Width );

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
			const int64 FirstIndex = NMath::Max<int64>( ( Group << Level ) - OldestId, 0 );
			const int64 LastIndex = NMath::Min<int64>( ( ( Group + 1 ) << Level ) - 1 - OldestId, NumFrames - 1 );
			if ( FirstIndex > LastIndex ) continue;
			const float32 GroupMs = Group < Entries.Num() ? Entries[static_cast<int32>( Group )] : 0.0f;
			Callback( static_cast<int32>( FirstIndex ), static_cast<int32>( LastIndex ), GroupMs );
		}
	}

	// ---- ProfilerTimelineView.cpp ----
	void SetSpan( FProfilerTimelineState& State, int32 Mode, float64 SpanMs, float64 AnchorMs, float AnchorFraction );
	void ZoomToRange( FProfilerTimelineState& State, int32 Mode, float64 StartMs, float64 EndMs );
	void ZoomAt( FProfilerTimelineState& State, int32 Mode, bool bFollow, float AnchorFraction, float Wheel, float64 MaxSpanMs );
	void GetPanLimits( const FProfilerTimelineState& State, int32 Mode, float64& OutMinStart, float64& OutMaxStart );
	void ClampView( FProfilerTimelineState& State, int32 Mode );
	void PanTo( FProfilerTimelineState& State, int32 Mode, float64 NewStartMs, bool& bFollow, bool bControlsFollow );
	void DrawHScrollbar( FProfilerTimelineState& State, int32 Mode, bool& bFollow, float Width, bool bControlsFollow );

	// ---- ProfilerTimelineWidgets.cpp ----
	void DrawRuler( const FArea& Area, float Y, bool bStacked );
	void DrawFrameSamples( const IProfiler& Profiler, const FProfilerTimelineState& State, const FArea& Area, const FProfilerFrame& Frame, int32 FrameIndex,
						   const TArray<FProfileSample>& Samples, uint32 ThreadId, float64 FrameOffsetMs, float RowTop, float RowHeight, const ImVec2& Mouse, bool bCanHit,
						   FHighlight& Highlight, FHit& InOutHit );
	void DrawSampleTooltip( const IProfiler& Profiler, const FProfilerFrame& Frame, const FProfileSample& Sample, uint32 ThreadId = 0 );
	void HandleTimeInput( FProfilerTimelineState& State, int32 Mode, bool& bFollow, float OriginX, float Width, bool bWheelScrolls, float64 MaxSpanMs,
						  bool bPanControlsFollow );
	void DrawToolbar( const IProfiler& Profiler, FProfilerTimelineState& State, bool& bFollow, EPendingAction& OutAction, float64 ViewSpan );
	void DrawDetail( IProfiler& Profiler, FProfilerTimelineState& State );
	void DrawOverview( const IProfiler& Profiler, FProfilerTimelineState& State, bool& bFollow, float Width, float64 DataStartMs, float64 DataEndMs );

	// ---- ProfilerTimelineContinuous.cpp ----
	void DrawContinuous( IProfiler& Profiler, FProfilerTimelineState& State, bool& bFollow, float Width, float64 DataStartMs, float64 DataEndMs, float ChildHeight );

	// ---- ProfilerTimelineStacked.cpp ----
	void DrawStacked( IProfiler& Profiler, FProfilerTimelineState& State, bool& bFollow, float Width, float ChildHeight );
}
