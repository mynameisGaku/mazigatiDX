#include "ProfilerWindow.h"

#include <algorithm>
#include <cmath>

#include "Library/Core/Log/Log.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Profiler/ProfilerExport.h"
#include "VendorModules/ImGui/imgui.h"

DEFINE_LOG_CATEGORY_STATIC( LogProfilerUI, Log )

namespace
{
	// Stats タブの列
	enum EStatColumn : int
	{
		ColName,
		ColCalls,
		ColInclusive,
		ColSelf,
		ColPerCall,
		ColMax,
		ColSelfPercent,
	};

	// Hierarchy タブの列（列見出しのクリックで並べ替える。ID は ImGui の列の user_id）
	enum EHierarchyColumn : int
	{
		HColName,
		HColCategory,
		HColOwner,
		HColCalls,
		HColTotal,
		HColSelf,
		HColPercent,
	};

	const char* const RangeLabels[] = { "Selected frame", "Last 30 frames", "Last 120 frames", "All stored (up to 20000)" };
	const char* const DepthLabels[] = { "All depths", "Top level only", "Depth <= 1", "Depth <= 2", "Depth <= 3" };

	// 表示の更新間隔
	constexpr int64 DisplayRefreshDivisor = 4;   // 追従中の表示フレーム・集計: 1 秒を 4 で割る = 0.25 秒
	constexpr int64 GraphRefreshDivisor = 10;    // フレームグラフ: 0.1 秒

	// 履歴はすべて残るので、全フレームを毎回調べる処理には上限を設ける
	constexpr int32 GraphMaxFrames = 1200;       // フレームグラフに出すフレーム数（直近）
	constexpr int32 AnalyzeMaxFrames = 20000;    // 「All stored」の集計の対象（直近）
	constexpr int32 SlowRefreshFrames = 2000;    // これより多いフレームを集計する範囲は、更新を 1 秒に 1 回にする
	constexpr int32 ExportMaxFrames = 20000;     // Chrome トレースに書き出すフレーム数（直近）

	// ImGui に渡す UTF-8 文字列（FString はシステムのマルチバイトなので変換する）。式の終わりまで有効
	#define UTF8( Text ) ( *( Text ).ToUtf8() )

	FORCEINLINE ImVec2 Add( const ImVec2& A, const ImVec2& B ) { return ImVec2( A.x + B.x, A.y + B.y ); }

	// 文字列 ID ごとに色を決める（同じ Category は同じ色）
	ImU32 IdColor( uint32 Id, float Value = 0.80f, float Alpha = 1.0f )
	{
		const float Hue = std::fmod( static_cast<float>( Id ) * 0.61803398875f, 1.0f );
		ImVec4 Color( 0, 0, 0, Alpha );
		ImGui::ColorConvertHSVtoRGB( Hue, 0.55f, Value, Color.x, Color.y, Color.z );
		return ImGui::ColorConvertFloat4ToU32( Color );
	}

	FORCEINLINE void MixHash( uint64& Hash, uint64 Value )
	{
		Hash ^= Value + 0x9E3779B97F4A7C15ull + ( Hash << 6 ) + ( Hash >> 2 );
	}

	FString FormatMs( float64 Ms )
	{
		return Ms >= 100.0 ? FString::Printf( "%.1f", Ms ) : ( Ms >= 1.0 ? FString::Printf( "%.2f", Ms ) : FString::Printf( "%.3f", Ms ) );
	}

	// フレーム番号からインデックスを探す（履歴から捨てられていれば INDEX_NONE）
	int32 FindFrameIndex( const IProfiler& Profiler, uint64 FrameNumber )
	{
		const int32 NumFrames = Profiler.GetNumFrames();
		if ( NumFrames <= 0 ) return INDEX_NONE;

		// フレーム番号は増えていく順に並んでいる（履歴が長くても速いように二分探索）
		int32 Low = 0, High = NumFrames;
		while ( Low < High )
		{
			const int32 Mid = ( Low + High ) / 2;
			if ( Profiler.GetFrame( Mid ).FrameNumber < FrameNumber ) Low = Mid + 1;
			else High = Mid;
		}
		return Low < NumFrames && Profiler.GetFrame( Low ).FrameNumber == FrameNumber ? Low : INDEX_NONE;
	}

	//-------------------------------------------------------------------------
	// 画面に出すフレームの決定
	//   選んだフレームがあればそれ。追従中は 0.25 秒ごとに最新へ進める
	//-------------------------------------------------------------------------
	void UpdateDisplayFrame( const IProfiler& Profiler, FProfilerWindowState& State )
	{
		const int32 NumFrames = Profiler.GetNumFrames();
		if ( NumFrames <= 0 ) return;

		if ( !State.bFollowLatest )
		{
			if ( FindFrameIndex( Profiler, State.SelectedFrameNumber ) != INDEX_NONE )
			{
				State.DisplayFrameNumber = State.SelectedFrameNumber;
				return;
			}
			State.bFollowLatest = true;   // 選んだフレームが履歴から流れた
		}

		const int64 Now = NPlatformTime::GetTicks();
		const bool bDue = ( Now - State.DisplayTick ) >= NPlatformTime::GetTicksPerSecond() / DisplayRefreshDivisor;
		if ( bDue || FindFrameIndex( Profiler, State.DisplayFrameNumber ) == INDEX_NONE )
		{
			State.DisplayFrameNumber = Profiler.GetFrame( NumFrames - 1 ).FrameNumber;
			State.DisplayTick = Now;
		}
	}

	//-------------------------------------------------------------------------
	// フレームグラフ用の控え（0.1 秒ごと。止まっているときは中身が変わったときだけ）
	//   履歴は長くなるので、グラフは直近 GraphMaxFrames フレームだけ。Live でないときは右端を固定して、見ているフレームが流れないようにする
	//-------------------------------------------------------------------------
	void RefreshGraph( const IProfiler& Profiler, FProfilerWindowState& State )
	{
		const int32 NumFrames = Profiler.GetNumFrames();
		if ( NumFrames <= 0 )
		{
			State.GraphMs.Reset();
			State.GraphEndFrameNumber = 0;
			return;
		}

		int32 EndIndex = NumFrames;   // グラフの右端（この番号を含まない）
		if ( !State.bFollowLatest && State.GraphEndFrameNumber != 0 )
		{
			const int32 Found = FindFrameIndex( Profiler, State.GraphEndFrameNumber );
			if ( Found != INDEX_NONE ) EndIndex = Found + 1;
		}
		const int32 NumBars = NMath::Min( EndIndex, GraphMaxFrames );
		const int32 FirstIndex = EndIndex - NumBars;
		const uint64 FirstNumber = Profiler.GetFrame( FirstIndex ).FrameNumber;

		const int64 Now = NPlatformTime::GetTicks();
		const bool bDue = ( Now - State.GraphTick ) >= NPlatformTime::GetTicksPerSecond() / GraphRefreshDivisor;
		const bool bContentChanged = ( !Profiler.IsEnabled() || !State.bFollowLatest )
			&& ( State.GraphMs.Num() != NumBars || State.GraphFirstFrameNumber != FirstNumber );
		if ( !bDue && !bContentChanged && !State.GraphMs.IsEmpty() ) return;

		State.GraphTick = Now;
		State.GraphFirstFrameNumber = FirstNumber;
		if ( State.bFollowLatest ) State.GraphEndFrameNumber = Profiler.GetFrame( NumFrames - 1 ).FrameNumber;
		State.GraphMs.SetNum( NumBars );
		for ( int32 i = 0; i < NumBars; ++i )
		{
			State.GraphMs[i] = static_cast<float32>( NPlatformTime::TicksToMs( Profiler.GetFrame( FirstIndex + i ).GetDurationTick() ) );
		}
	}

	//-------------------------------------------------------------------------
	// 集計のキャッシュ
	//   設定や表示フレームが変わったらすぐ、データが増えただけなら 0.25 秒ごとに作り直す（毎フレーム集計すると重い）
	//-------------------------------------------------------------------------
	void RefreshCache( IProfiler& Profiler, FProfilerWindowState& State, int32 DisplayIndex )
	{
		const int32 NumFrames = Profiler.GetNumFrames();
		if ( NumFrames <= 0 || DisplayIndex == INDEX_NONE )
		{
			State.Rows.Reset();
			State.SortedRows.Reset();
			State.TreeNodes.Reset();
			State.TreeNodesBase.Reset();
			State.RangeStats = FProfileFrameStats();
			State.SettingsKey = 0;
			State.DataKey = 0;
			return;
		}

		uint64 SettingsKey = 1469598103934665603ull;
		MixHash( SettingsKey, static_cast<uint64>( State.RangeMode ) );
		MixHash( SettingsKey, State.GroupBy );
		MixHash( SettingsKey, static_cast<uint64>( State.MaxDepth + 2 ) );
		MixHash( SettingsKey, State.bMergeSiblings ? 1 : 2 );
		MixHash( SettingsKey, State.bMergeInstances ? 1 : 2 );
		MixHash( SettingsKey, State.DisplayFrameNumber );   // 追従中は 0.25 秒ごとにしか変わらない
		MixHash( SettingsKey, GetTypeHash( FString( State.Filter ) ) );

		const uint64 DataKey = static_cast<uint64>( NumFrames ) * 1000003ull + Profiler.GetFrame( NumFrames - 1 ).FrameNumber;

		const int64 Now = NPlatformTime::GetTicks();
		const bool bSettingsChanged = SettingsKey != State.SettingsKey;
		// 広い範囲の集計は重いので、更新は 1 秒に 1 回にする
		const bool bWideRange = State.RangeLast - State.RangeFirst >= SlowRefreshFrames;
		const int64 RefreshInterval = bWideRange ? NPlatformTime::GetTicksPerSecond() : NPlatformTime::GetTicksPerSecond() / DisplayRefreshDivisor;
		const bool bDataChanged = DataKey != State.DataKey && ( Now - State.CacheTick ) >= RefreshInterval;
		if ( !bSettingsChanged && !bDataChanged ) return;

		State.SettingsKey = SettingsKey;
		State.DataKey = DataKey;
		State.CacheTick = Now;

		// 範囲は「表示しているフレーム」までにする（追従中でなければ、新しいフレームが増えても動かない）
		const int32 Last = DisplayIndex;
		switch ( State.RangeMode )
		{
		case 0:  State.RangeFirst = Last;                                 break;
		case 1:  State.RangeFirst = Last >= 29 ? Last - 29 : 0;           break;
		case 2:  State.RangeFirst = Last >= 119 ? Last - 119 : 0;         break;
		default: State.RangeFirst = 0;                                    break;
		}
		State.RangeLast = State.RangeMode == 3 ? NumFrames - 1 : Last;
		if ( State.RangeLast - State.RangeFirst + 1 > AnalyzeMaxFrames ) State.RangeFirst = State.RangeLast - AnalyzeMaxFrames + 1;   // 履歴が長いときは直近だけ

		State.RangeStats = NProfilerAnalyzer::ComputeFrameStats( Profiler, State.RangeFirst, State.RangeLast );

		FProfileQuery Query;
		Query.FirstFrame = State.RangeFirst;
		Query.LastFrame = State.RangeLast;
		Query.GroupBy = State.GroupBy;
		Query.Filter = FString( State.Filter );
		Query.MaxDepth = State.MaxDepth;
		NProfilerAnalyzer::BuildStats( Profiler, Query, State.Rows );
		State.bSortDirty = true;

		NProfilerAnalyzer::BuildCallTree( Profiler, State.RangeFirst, State.RangeLast, State.bMergeSiblings, State.bMergeInstances, State.TreeNodesBase );
		State.bTreeSortDirty = true;
	}

	//-------------------------------------------------------------------------
	// 操作部
	//-------------------------------------------------------------------------
	void DrawControls( IProfiler& Profiler, FProfilerWindowState& State )
	{
		const bool bEnabled = Profiler.IsEnabled();

		ImGui::PushStyleColor( ImGuiCol_Button, bEnabled ? ImVec4( 0.75f, 0.20f, 0.20f, 1.0f ) : ImVec4( 0.20f, 0.55f, 0.25f, 1.0f ) );
		if ( ImGui::Button( bEnabled ? "Stop  [F2]" : "Record  [F2]", ImVec2( 110.0f, 0.0f ) ) ) Profiler.SetEnabled( !bEnabled );
		ImGui::PopStyleColor();

		ImGui::SameLine();
		if ( ImGui::Button( "Capture" ) ) Profiler.CaptureFrames( State.CaptureFrames );
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 80.0f );
		ImGui::InputInt( "frames##Capture", &State.CaptureFrames, 0, 0 );
		if ( State.CaptureFrames < 1 ) State.CaptureFrames = 1;

		ImGui::SameLine();
		if ( ImGui::Button( "Clear" ) )
		{
			Profiler.Clear();
			State.SettingsKey = 0;
			State.StatusMessage = FString();
		}

		ImGui::SameLine();
		if ( ImGui::Button( "Export trace" ) )
		{
			// 履歴は長くなるので、書き出すのは直近の ExportMaxFrames フレームまで（ブラウザで開けなくなるため）
			const FString Path = NProfilerExport::MakeDefaultTracePath();
			const int32 NumStored = Profiler.GetNumFrames();
			const int32 FirstFrame = NumStored > ExportMaxFrames ? NumStored - ExportMaxFrames : 0;
			State.StatusMessage = NProfilerExport::WriteChromeTrace( Profiler, FirstFrame, -1, Path )
				? FString::Printf( "Exported the latest %d frame(s): ", NumStored - FirstFrame ) + Path + "  (open in chrome://tracing or https://ui.perfetto.dev)"
				: FString( "Export failed. See the log for details." );
		}
		if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Save the latest stored frames (up to 20000) as a Chrome trace (JSON)" );

		// 状態
		if ( Profiler.IsRecording() )
		{
			ImGui::TextColored( ImVec4( 0.95f, 0.30f, 0.30f, 1.0f ), "REC" );
			ImGui::SameLine();
			if ( Profiler.GetRemainingCaptureFrames() > 0 ) ImGui::Text( "capturing: %d frame(s) left", Profiler.GetRemainingCaptureFrames() );
			else ImGui::Text( "recording" );
		}
		else if ( Profiler.IsEnabled() )
		{
			ImGui::TextColored( ImVec4( 0.95f, 0.75f, 0.30f, 1.0f ), "Starting at the next frame..." );
		}
		else
		{
			ImGui::TextDisabled( "Stopped (nothing is measured; the cost is one pointer check per scope)" );
		}
		ImGui::SameLine();
		ImGui::Text( "  stored: %d frame(s), %.2f M samples, ~%.0f MB", Profiler.GetNumFrames(), static_cast<float64>( Profiler.GetNumStoredSamples() ) / 1.0e6,
					 static_cast<float64>( Profiler.GetStoredBytes() ) / ( 1024.0 * 1024.0 ) );
		if ( Profiler.IsMemoryLimitReached() )
		{
			ImGui::TextColored( ImVec4( 0.95f, 0.70f, 0.25f, 1.0f ), "Memory limit reached: recording stopped. The data is kept; raise the limit in Settings to record more." );
		}

		if ( ImGui::CollapsingHeader( "Settings" ) )
		{
			// 履歴は既定で捨てずに残す。メモリの上限に達したら記録を止める（捨てない）
			int32 MemoryLimitMB = Profiler.GetMemoryLimitMB();
			ImGui::SetNextItemWidth( 100.0f );
			if ( ImGui::InputInt( "Memory limit (MB)", &MemoryLimitMB, 256, 1024 ) ) Profiler.SetMemoryLimitMB( MemoryLimitMB );
			ImGui::SameLine();
			ImGui::TextDisabled( "(0 = none. History is kept; recording stops when this is reached)" );

			int32 MaxFrames = Profiler.GetMaxFrames();
			ImGui::SetNextItemWidth( 100.0f );
			if ( ImGui::InputInt( "Drop oldest beyond (frames)", &MaxFrames, 1000, 10000 ) ) Profiler.SetMaxFrames( MaxFrames );
			ImGui::SameLine();
			ImGui::TextDisabled( "(0 = never drop)" );

			int32 MaxSamples = Profiler.GetMaxSamplesPerFrame();
			ImGui::SetNextItemWidth( 100.0f );
			if ( ImGui::InputInt( "Max samples / frame", &MaxSamples, 1024, 4096 ) ) Profiler.SetMaxSamplesPerFrame( MaxSamples );

			// 保存するサンプルの合計。超えたら古いフレームから捨てる（オブジェクトが多いときにメモリが膨らまないように）
			int32 MaxStoredSamples = Profiler.GetMaxStoredSamples();
			ImGui::SetNextItemWidth( 100.0f );
			if ( ImGui::InputInt( "Drop oldest beyond (samples)", &MaxStoredSamples, 100000, 1000000 ) ) Profiler.SetMaxStoredSamples( MaxStoredSamples );
			ImGui::SameLine();
			ImGui::TextDisabled( "(0 = never drop; 1 sample is %d bytes)", static_cast<int>( sizeof( FProfileSample ) ) );

			int32 NumSamplesLatest = 0;
			if ( Profiler.GetNumFrames() > 0 ) NumSamplesLatest = Profiler.GetFrame( Profiler.GetNumFrames() - 1 ).Samples.Num();
			ImGui::TextDisabled( "Measuring costs ~%.0f ns per sample: ~%.3f ms/frame at the current %d samples/frame.",
								 Profiler.GetOverheadNsPerSample(), Profiler.GetOverheadNsPerSample() * NumSamplesLatest / 1.0e6, NumSamplesLatest );
		}
	}

	//-------------------------------------------------------------------------
	// フレームグラフ（棒 1 本 = 1 フレーム。クリック / ドラッグでフレームを選ぶ）
	//-------------------------------------------------------------------------
	void DrawFrameGraph( IProfiler& Profiler, FProfilerWindowState& State )
	{
		const int32 NumBars = State.GraphMs.Num();
		const ImVec2 Size( ImGui::GetContentRegionAvail().x, 84.0f );
		const ImVec2 Origin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton( "##FrameGraph", Size );

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		DrawList->AddRectFilled( Origin, Add( Origin, Size ), IM_COL32( 22, 22, 22, 255 ) );
		if ( NumBars <= 0 )
		{
			DrawList->AddText( Add( Origin, ImVec2( 8.0f, 8.0f ) ), IM_COL32( 160, 160, 160, 255 ), "No data. Press Record (F2)." );
			return;
		}

		float32 MaxMs = 20.0f;
		for ( int32 i = 0; i < NumBars; ++i ) if ( State.GraphMs[i] > MaxMs ) MaxMs = State.GraphMs[i];

		float BarWidth = Size.x / static_cast<float>( NumBars );
		if ( BarWidth > 14.0f ) BarWidth = 14.0f;
		const float BaseY = Origin.y + Size.y;

		for ( int32 i = 0; i < NumBars; ++i )
		{
			const float32 Ms = State.GraphMs[i];
			const float Height = ( Ms / MaxMs ) * ( Size.y - 4.0f );
			const ImU32 Color = Ms <= 8.4f ? IM_COL32( 80, 190, 100, 255 ) : ( Ms <= 16.8f ? IM_COL32( 215, 190, 70, 255 ) : IM_COL32( 220, 85, 70, 255 ) );
			const float X0 = Origin.x + BarWidth * static_cast<float>( i );
			DrawList->AddRectFilled( ImVec2( X0, BaseY - Height ), ImVec2( X0 + ( BarWidth > 2.0f ? BarWidth - 1.0f : BarWidth ), BaseY ), Color );
		}

		// 目安線（60 FPS / 30 FPS）
		const float Y60 = BaseY - ( 16.667f / MaxMs ) * ( Size.y - 4.0f );
		const float Y30 = BaseY - ( 33.333f / MaxMs ) * ( Size.y - 4.0f );
		DrawList->AddLine( ImVec2( Origin.x, Y60 ), ImVec2( Origin.x + Size.x, Y60 ), IM_COL32( 255, 255, 255, 70 ) );
		DrawList->AddText( ImVec2( Origin.x + 4.0f, Y60 - 14.0f ), IM_COL32( 255, 255, 255, 120 ), "60 FPS" );
		if ( Y30 > Origin.y + 12.0f )
		{
			DrawList->AddLine( ImVec2( Origin.x, Y30 ), ImVec2( Origin.x + Size.x, Y30 ), IM_COL32( 255, 255, 255, 40 ) );
			DrawList->AddText( ImVec2( Origin.x + 4.0f, Y30 - 14.0f ), IM_COL32( 255, 255, 255, 90 ), "30 FPS" );
		}

		// 表示中のフレーム
		const int64 DisplayBar = static_cast<int64>( State.DisplayFrameNumber ) - static_cast<int64>( State.GraphFirstFrameNumber );
		if ( DisplayBar >= 0 && DisplayBar < NumBars )
		{
			const float X0 = Origin.x + BarWidth * static_cast<float>( DisplayBar );
			DrawList->AddRect( ImVec2( X0 - 1.0f, Origin.y ), ImVec2( X0 + BarWidth, BaseY ), IM_COL32( 255, 255, 255, 220 ) );
		}

		// 操作
		if ( ImGui::IsItemHovered() || ImGui::IsItemActive() )
		{
			const int32 Bar = static_cast<int32>( ( ImGui::GetIO().MousePos.x - Origin.x ) / BarWidth );
			if ( Bar >= 0 && Bar < NumBars )
			{
				const uint64 FrameNumber = State.GraphFirstFrameNumber + static_cast<uint64>( Bar );
				const int32 FrameIndex = FindFrameIndex( Profiler, FrameNumber );
				const float32 Ms = State.GraphMs[Bar];
				ImGui::SetTooltip( "Frame #%llu: %.2f ms (%.0f FPS)\n%d samples\nClick to inspect this frame", static_cast<unsigned long long>( FrameNumber ), Ms,
								   Ms > 0.0f ? 1000.0f / Ms : 0.0f, FrameIndex != INDEX_NONE ? Profiler.GetFrame( FrameIndex ).Samples.Num() : 0 );
				if ( ImGui::IsItemActive() && FrameIndex != INDEX_NONE )
				{
					// 1 フレームを選んだら、そのフレームそのものを見る（平均ではなく）
					State.bFollowLatest = false;
					State.SelectedFrameNumber = FrameNumber;
					State.Timeline.FocusedFrameNumber = 0;   // タイムラインがそのフレームへ移動する
					State.RangeMode = 0;
				}
			}
		}
	}

	//-------------------------------------------------------------------------
	// Hierarchy: 呼び出しツリー（範囲の平均、または選んだ 1 フレーム）
	//   値はすべて「1 フレームあたり」
	//-------------------------------------------------------------------------
	void DrawTreeNodes( const IProfiler& Profiler, const FProfilerWindowState& State, int32 Begin, int32 End, float64 FrameMs, float64 NumFrames,
						int64 HideBelowTick )
	{
		for ( int32 i = Begin; i < End; i += 1 + State.TreeNodes[i].NumDescendants )
		{
			const FProfileTreeNode& Node = State.TreeNodes[i];
			const float64 TotalTick = static_cast<float64>( Node.TotalTick ) / NumFrames;
			if ( TotalTick < static_cast<float64>( HideBelowTick ) ) continue;   // 短いノードは子孫ごと隠す

			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_OpenOnArrow;
			if ( Node.NumDescendants == 0 ) Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet;
			if ( Node.Depth <= 1 ) Flags |= ImGuiTreeNodeFlags_DefaultOpen;

			// 開閉状態は ID で覚える。位置（インデックス）にすると、並びが変わったときに別の行へ移ってしまう
			const FString Label = NProfilerAnalyzer::MakeNodeLabel( Profiler, Node );
			const bool bOpen = ImGui::TreeNodeEx( reinterpret_cast<const void*>( static_cast<uintptr_t>( Node.Id ) ), Flags, "%s", UTF8( Label ) );

			ImGui::TableNextColumn();
			ImGui::PushStyleColor( ImGuiCol_Text, IdColor( Node.Category, 1.0f ) );
			ImGui::TextUnformatted( UTF8( Profiler.GetString( Node.Category ) ) );
			ImGui::PopStyleColor();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted( UTF8( Profiler.GetString( Node.Owner ) ) );
			ImGui::TableNextColumn();
			ImGui::Text( "%.1f", Node.Calls / NumFrames );
			ImGui::TableNextColumn();
			const float64 TotalMs = NPlatformTime::TicksToMs( Node.TotalTick ) / NumFrames;
			ImGui::TextUnformatted( UTF8( FormatMs( TotalMs ) ) );
			ImGui::TableNextColumn();
			ImGui::TextUnformatted( UTF8( FormatMs( NPlatformTime::TicksToMs( Node.SelfTick ) / NumFrames ) ) );
			ImGui::TableNextColumn();
			ImGui::Text( "%.1f%%", FrameMs > 0.0 ? 100.0 * TotalMs / FrameMs : 0.0 );

			if ( bOpen )
			{
				if ( Node.NumDescendants > 0 ) DrawTreeNodes( Profiler, State, i + 1, i + 1 + Node.NumDescendants, FrameMs, NumFrames, HideBelowTick );
				ImGui::TreePop();
			}
		}
	}

	// 並べ替えの設定を TreeNodes に反映する（-1 なら呼ばれた順のまま）
	void ApplyTreeSort( const IProfiler& Profiler, FProfilerWindowState& State )
	{
		State.bTreeSortDirty = false;
		if ( State.TreeSortColumn < 0 )
		{
			State.TreeNodes = State.TreeNodesBase;
			return;
		}

		NProfilerAnalyzer::ETreeSortKey Key = NProfilerAnalyzer::ETreeSortKey::Total;
		switch ( State.TreeSortColumn )
		{
		case HColName:     Key = NProfilerAnalyzer::ETreeSortKey::Name; break;
		case HColCategory: Key = NProfilerAnalyzer::ETreeSortKey::Category; break;
		case HColOwner:    Key = NProfilerAnalyzer::ETreeSortKey::Owner; break;
		case HColCalls:    Key = NProfilerAnalyzer::ETreeSortKey::Calls; break;
		case HColSelf:     Key = NProfilerAnalyzer::ETreeSortKey::Self; break;
		case HColTotal:
		case HColPercent:
		default:           Key = NProfilerAnalyzer::ETreeSortKey::Total; break;
		}
		NProfilerAnalyzer::SortCallTree( Profiler, State.TreeNodesBase, Key, State.bTreeSortAscending, State.TreeNodes );
	}

	void DrawHierarchyTab( IProfiler& Profiler, FProfilerWindowState& State, const FProfilerFrame& DisplayFrame )
	{
		const bool bSingleFrame = State.RangeFirst == State.RangeLast;

		// 複数フレームの平均では、フレームをまたいで同じ処理を揃えるために常にまとめる
		ImGui::BeginDisabled( !bSingleFrame );
		ImGui::Checkbox( "Merge same calls", &State.bMergeSiblings );
		if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
		{
			ImGui::SetTooltip( "Combine the same call under the same parent (e.g. 1000 balls -> one row, sorted by time).\nAlways on when averaging several frames." );
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled( !State.bMergeSiblings && bSingleFrame );
		ImGui::Checkbox( "Ignore instances", &State.bMergeInstances );
		if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) ) ImGui::SetTooltip( "On: all objects of one type become one row.\nOff: one row per object." );
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 110.0f );
		ImGui::DragFloat( "Hide below (us)", &State.HideBelowUs, 1.0f, 0.0f, 100000.0f, "%.0f" );

		const float64 NumFrames = bSingleFrame ? 1.0 : static_cast<float64>( State.RangeStats.NumFrames > 0 ? State.RangeStats.NumFrames : 1 );
		const float64 FrameMs = bSingleFrame ? NPlatformTime::TicksToMs( DisplayFrame.GetDurationTick() ) : State.RangeStats.AverageMs;
		if ( bSingleFrame )
		{
			ImGui::Text( "Frame #%llu: %s ms, %d samples%s", static_cast<unsigned long long>( DisplayFrame.FrameNumber ), UTF8( FormatMs( FrameMs ) ),
						 DisplayFrame.Samples.Num(), DisplayFrame.NumDropped > 0 ? "  (samples were dropped: raise Max samples / frame)" : "" );
		}
		else
		{
			ImGui::Text( "Average of %d frames: %s ms per frame (per-frame values below)", State.RangeStats.NumFrames, UTF8( FormatMs( FrameMs ) ) );
		}

		const ImGuiTableFlags TableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY
			| ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate;
		if ( ImGui::BeginTable( "##Hierarchy", 7, TableFlags, ImVec2( 0.0f, 0.0f ) ) )
		{
			ImGui::TableSetupScrollFreeze( 0, 1 );
			ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_PreferSortAscending, 4.0f, HColName );
			ImGui::TableSetupColumn( "Category", ImGuiTableColumnFlags_PreferSortAscending, 1.6f, HColCategory );
			ImGui::TableSetupColumn( "Owner", ImGuiTableColumnFlags_PreferSortAscending, 1.4f, HColOwner );
			ImGui::TableSetupColumn( "Calls", ImGuiTableColumnFlags_PreferSortDescending, 0.7f, HColCalls );
			ImGui::TableSetupColumn( "Total ms", ImGuiTableColumnFlags_PreferSortDescending, 1.0f, HColTotal );
			ImGui::TableSetupColumn( "Self ms", ImGuiTableColumnFlags_PreferSortDescending, 1.0f, HColSelf );
			ImGui::TableSetupColumn( "% frame", ImGuiTableColumnFlags_PreferSortDescending, 0.9f, HColPercent );
			ImGui::TableHeadersRow();

			// 列見出しのクリックで 昇順 -> 降順 -> 呼ばれた順（並べ替えなし）。親子関係は保ち、同じ親の下の兄弟どうしを並べ替える
			if ( ImGuiTableSortSpecs* Specs = ImGui::TableGetSortSpecs() )
			{
				const int32 Column = Specs->SpecsCount > 0 ? static_cast<int32>( Specs->Specs[0].ColumnUserID ) : -1;
				const bool bAscending = Specs->SpecsCount > 0 && Specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
				if ( Specs->SpecsDirty || Column != State.TreeSortColumn || bAscending != State.bTreeSortAscending )
				{
					State.TreeSortColumn = Column;
					State.bTreeSortAscending = bAscending;
					State.bTreeSortDirty = true;
				}
				Specs->SpecsDirty = false;
			}
			if ( State.bTreeSortDirty ) ApplyTreeSort( Profiler, State );

			const int64 HideBelowTick = static_cast<int64>( State.HideBelowUs * 1.0e-6 * static_cast<float64>( NPlatformTime::GetTicksPerSecond() ) );
			DrawTreeNodes( Profiler, State, 0, State.TreeNodes.Num(), FrameMs, NumFrames, HideBelowTick );
			ImGui::EndTable();
		}
	}

	//-------------------------------------------------------------------------
	// Stats: 軸を選んでまとめた表
	//-------------------------------------------------------------------------
	void SortRows( FProfilerWindowState& State )
	{
		State.SortedRows.Reset();
		State.SortedRows.Reserve( State.Rows.Num() );
		for ( int32 i = 0; i < State.Rows.Num(); ++i ) State.SortedRows.Add( i );

		const TArray<FProfileStatRow>& Rows = State.Rows;
		const int32 Column = State.SortColumn;
		const bool bAscending = State.bSortAscending;

		// 値が同じ行は元の順（見つかった順）のままにして、並びが入れ替わり続けないようにする
		State.SortedRows.StableSort( [&Rows, Column, bAscending]( int32 A, int32 B )
		{
			const FProfileStatRow& RowA = Rows[A];
			const FProfileStatRow& RowB = Rows[B];

			float64 ValueA = 0.0, ValueB = 0.0;
			switch ( Column )
			{
			case ColName:
			{
				const int32 Compare = RowA.Label.Compare( RowB.Label, ESearchCase::IgnoreCase );
				return bAscending ? Compare < 0 : Compare > 0;
			}
			case ColCalls:       ValueA = RowA.Calls;       ValueB = RowB.Calls; break;
			case ColInclusive:   ValueA = RowA.InclusiveMs; ValueB = RowB.InclusiveMs; break;
			case ColPerCall:
				ValueA = RowA.Calls > 0 ? RowA.InclusiveMs / RowA.Calls : 0.0;
				ValueB = RowB.Calls > 0 ? RowB.InclusiveMs / RowB.Calls : 0.0;
				break;
			case ColMax:         ValueA = RowA.MaxMs;       ValueB = RowB.MaxMs; break;
			case ColSelf:
			case ColSelfPercent:
			default:             ValueA = RowA.SelfMs;      ValueB = RowB.SelfMs; break;
			}
			return bAscending ? ValueA < ValueB : ValueA > ValueB;
		} );
		State.bSortDirty = false;
	}

	void DrawStatsTab( IProfiler& Profiler, FProfilerWindowState& State )
	{
		// ---- 軸
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted( "Group by:" );
		const auto GroupFlag = [&State]( const char* Label, uint32 Bit )
		{
			ImGui::SameLine();
			bool bOn = ( State.GroupBy & Bit ) != 0;
			if ( ImGui::Checkbox( Label, &bOn ) ) State.GroupBy = bOn ? ( State.GroupBy | Bit ) : ( State.GroupBy & ~Bit );
		};
		GroupFlag( "Category", NProfileGroupBy::Category );
		GroupFlag( "Owner (scene)", NProfileGroupBy::Owner );
		GroupFlag( "Name (type)", NProfileGroupBy::Name );
		GroupFlag( "Instance", NProfileGroupBy::Instance );

		ImGui::TextDisabled( "Presets:" );
		const auto Preset = [&State]( const char* Label, uint32 GroupBy )
		{
			ImGui::SameLine();
			if ( ImGui::SmallButton( Label ) ) State.GroupBy = GroupBy;
		};
		Preset( "Category", NProfileGroupBy::Category );
		Preset( "Scene", NProfileGroupBy::Owner );
		Preset( "Scene + Name", NProfileGroupBy::Owner | NProfileGroupBy::Name );
		Preset( "Category + Name", NProfileGroupBy::Category | NProfileGroupBy::Name );
		Preset( "Each object", NProfileGroupBy::Name | NProfileGroupBy::Instance );
		Preset( "Name", NProfileGroupBy::Name );

		// ---- 深さ・フィルター
		int32 DepthIndex = State.MaxDepth < 0 ? 0 : ( State.MaxDepth + 1 > 4 ? 4 : State.MaxDepth + 1 );
		ImGui::SetNextItemWidth( 130.0f );
		if ( ImGui::Combo( "##Depth", &DepthIndex, DepthLabels, IM_ARRAYSIZE( DepthLabels ) ) ) State.MaxDepth = DepthIndex == 0 ? -1 : DepthIndex - 1;
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 220.0f );
		ImGui::InputTextWithHint( "##Filter", "filter (name / category / owner)", State.Filter, sizeof( State.Filter ) );

		const FProfileFrameStats& Stats = State.RangeStats;
		if ( Stats.NumFrames > 0 )
		{
			ImGui::Text( "%d frame(s): average %s ms (%.0f FPS), min %s, max %s   |   %d row(s)", Stats.NumFrames, UTF8( FormatMs( Stats.AverageMs ) ),
						 Stats.AverageMs > 0.0 ? 1000.0 / Stats.AverageMs : 0.0, UTF8( FormatMs( Stats.MinMs ) ), UTF8( FormatMs( Stats.MaxMs ) ),
						 State.Rows.Num() );
		}
		const float64 NumFrames = Stats.NumFrames > 0 ? static_cast<float64>( Stats.NumFrames ) : 1.0;
		const float64 FrameAverageMs = Stats.AverageMs > 0.0 ? Stats.AverageMs : 1.0;

		const ImGuiTableFlags TableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable
			| ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
		if ( !ImGui::BeginTable( "##Stats", 7, TableFlags, ImVec2( 0.0f, 0.0f ) ) ) return;

		ImGui::TableSetupScrollFreeze( 0, 1 );
		ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_PreferSortAscending, 4.5f, ColName );
		ImGui::TableSetupColumn( "Calls / frame", ImGuiTableColumnFlags_PreferSortDescending, 1.0f, ColCalls );
		ImGui::TableSetupColumn( "Total ms / frame", ImGuiTableColumnFlags_PreferSortDescending, 1.2f, ColInclusive );
		ImGui::TableSetupColumn( "Self ms / frame", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 1.2f, ColSelf );
		ImGui::TableSetupColumn( "ms / call", ImGuiTableColumnFlags_PreferSortDescending, 1.0f, ColPerCall );
		ImGui::TableSetupColumn( "Max ms", ImGuiTableColumnFlags_PreferSortDescending, 1.0f, ColMax );
		ImGui::TableSetupColumn( "Self % of frame", ImGuiTableColumnFlags_PreferSortDescending, 1.6f, ColSelfPercent );
		ImGui::TableHeadersRow();

		if ( ImGuiTableSortSpecs* Specs = ImGui::TableGetSortSpecs() )
		{
			if ( Specs->SpecsCount > 0 )
			{
				const int32 Column = static_cast<int32>( Specs->Specs[0].ColumnUserID );
				const bool bAscending = Specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
				if ( Specs->SpecsDirty || Column != State.SortColumn || bAscending != State.bSortAscending )
				{
					State.SortColumn = Column;
					State.bSortAscending = bAscending;
					State.bSortDirty = true;
				}
				Specs->SpecsDirty = false;
			}
		}
		if ( State.bSortDirty || State.SortedRows.Num() != State.Rows.Num() ) SortRows( State );

		ImGuiListClipper Clipper;
		Clipper.Begin( State.SortedRows.Num() );
		while ( Clipper.Step() )
		{
			for ( int32 i = Clipper.DisplayStart; i < Clipper.DisplayEnd; ++i )
			{
				const FProfileStatRow& Row = State.Rows[State.SortedRows[i]];
				const float64 SelfPerFrame = Row.SelfMs / NumFrames;

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( UTF8( Row.Label ) );
				ImGui::TableNextColumn();
				ImGui::Text( "%.1f", Row.Calls / NumFrames );
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( UTF8( FormatMs( Row.InclusiveMs / NumFrames ) ) );
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( UTF8( FormatMs( SelfPerFrame ) ) );
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( UTF8( FormatMs( Row.Calls > 0 ? Row.InclusiveMs / Row.Calls : 0.0 ) ) );
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( UTF8( FormatMs( Row.MaxMs ) ) );

				// % の列には、割合に応じた棒を敷く
				ImGui::TableNextColumn();
				const float Fraction = static_cast<float>( SelfPerFrame / FrameAverageMs );
				const float ClampedFraction = Fraction < 0.0f ? 0.0f : ( Fraction > 1.0f ? 1.0f : Fraction );
				const ImVec2 CellMin = ImGui::GetCursorScreenPos();
				const float CellWidth = ImGui::GetContentRegionAvail().x;
				ImGui::GetWindowDrawList()->AddRectFilled( CellMin, ImVec2( CellMin.x + CellWidth * ClampedFraction, CellMin.y + ImGui::GetTextLineHeight() ),
														   IM_COL32( 80, 140, 220, 110 ) );
				ImGui::Text( "%.1f%%", Fraction * 100.0f );
			}
		}
		ImGui::EndTable();
	}

	// カウンター（メモリ使用量・個数など、時間ではない値）。範囲は Hierarchy / Stats と同じ
	void DrawCountersTab( IProfiler& Profiler, FProfilerWindowState& State )
	{
		ImGui::SetNextItemWidth( 260.0f );
		ImGui::InputTextWithHint( "##CounterFilter", "filter (e.g. Asset.Memory)", State.CounterFilter, sizeof( State.CounterFilter ) );
		ImGui::SameLine();
		ImGui::Checkbox( "Graphs", &State.bCounterGraphs );

		TArray<FProfileCounterRow> Rows;
		NProfilerAnalyzer::BuildCounterRows( Profiler, State.RangeFirst, State.RangeLast, FString( State.CounterFilter ), Rows );
		if ( Rows.IsEmpty() )
		{
			ImGui::TextDisabled( "No counters were recorded in this range. (Counters are recorded with PROFILE_COUNTER while profiling is on.)" );
			return;
		}
		ImGui::TextDisabled( "%d counter(s). Values are per frame; Last / Avg / Min / Max cover the selected range.", Rows.Num() );

		const ImGuiTableFlags TableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
		if ( !ImGui::BeginTable( "##Counters", State.bCounterGraphs ? 6 : 5, TableFlags, ImVec2( 0.0f, 0.0f ) ) ) return;

		ImGui::TableSetupScrollFreeze( 0, 1 );
		ImGui::TableSetupColumn( "Counter", ImGuiTableColumnFlags_NoHide, 4.0f );
		ImGui::TableSetupColumn( "Last", 0, 1.3f );
		ImGui::TableSetupColumn( "Average", 0, 1.3f );
		ImGui::TableSetupColumn( "Min", 0, 1.3f );
		ImGui::TableSetupColumn( "Max", 0, 1.3f );
		if ( State.bCounterGraphs ) ImGui::TableSetupColumn( "History", 0, 3.0f );
		ImGui::TableHeadersRow();

		TArray<float32> Series;
		ImGuiListClipper Clipper;
		Clipper.Begin( Rows.Num() );
		while ( Clipper.Step() )
		{
			for ( int32 i = Clipper.DisplayStart; i < Clipper.DisplayEnd; ++i )
			{
				const FProfileCounterRow& Row = Rows[i];
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( UTF8( Row.Label ) );
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( UTF8( NProfilerAnalyzer::FormatCounterValue( Row.Last, Row.Unit ) ) );
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( UTF8( NProfilerAnalyzer::FormatCounterValue( Row.Average, Row.Unit ) ) );
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( UTF8( NProfilerAnalyzer::FormatCounterValue( Row.Min, Row.Unit ) ) );
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( UTF8( NProfilerAnalyzer::FormatCounterValue( Row.Max, Row.Unit ) ) );
				if ( State.bCounterGraphs )
				{
					ImGui::TableNextColumn();
					NProfilerAnalyzer::GetCounterSeries( Profiler, Row.Name, State.RangeFirst, State.RangeLast, Series );
					const float Lo = static_cast<float>( Row.Min );
					const float Hi = static_cast<float>( Row.Max );
					ImGui::PushID( static_cast<int>( Row.Name ) );
					ImGui::PlotLines( "##History", Series.GetData(), Series.Num(), 0, nullptr, Lo - ( Hi - Lo ) * 0.1f, Hi + ( Hi - Lo ) * 0.1f + 1.0e-6f,
									  ImVec2( ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight() ) );
					ImGui::PopID();
				}
			}
		}
		ImGui::EndTable();
	}

	#undef UTF8
}

void NDebugUI::DrawProfilerWindow( IProfiler& Profiler, FProfilerWindowState& State, bool* pOpen )
{
	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos( ImVec2( MainViewport->Pos.x + 24.0f, MainViewport->Pos.y + 44.0f ), ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( NMath::Min( 1000.0f, MainViewport->Size.x - 48.0f ), NMath::Min( 660.0f, MainViewport->Size.y - 60.0f ) ), ImGuiCond_FirstUseEver );
	if ( !ImGui::Begin( "Profiler", pOpen ) )
	{
		ImGui::End();
		return;
	}

	DrawControls( Profiler, State );
	ImGui::Separator();

	UpdateDisplayFrame( Profiler, State );
	const int32 DisplayIndex = Profiler.GetNumFrames() > 0 ? FindFrameIndex( Profiler, State.DisplayFrameNumber ) : INDEX_NONE;
	RefreshGraph( Profiler, State );
	RefreshCache( Profiler, State, DisplayIndex );

	DrawFrameGraph( Profiler, State );

	// 範囲（Hierarchy / Stats 共通）と、新しいフレームへの追従
	ImGui::SetNextItemWidth( 150.0f );
	ImGui::Combo( "Frames", &State.RangeMode, RangeLabels, IM_ARRAYSIZE( RangeLabels ) );
	if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Hierarchy and Stats use this range (per-frame averages).\n'Selected frame' shows exactly the frame you clicked in the graph." );
	ImGui::SameLine();
	if ( ImGui::Checkbox( "Live", &State.bFollowLatest ) && State.bFollowLatest && State.RangeMode == 0 ) State.RangeMode = 2;
	if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Follow the newest frames (updates 4 times a second).\nClick a bar in the graph to inspect one frame." );
	if ( !State.bFollowLatest && State.RangeMode != 0 )
	{
		ImGui::SameLine();
		ImGui::TextDisabled( "(paused at frame #%llu)", static_cast<unsigned long long>( State.DisplayFrameNumber ) );
	}

	if ( DisplayIndex != INDEX_NONE && ImGui::BeginTabBar( "##ProfilerTabs" ) )
	{
		const FProfilerFrame& Frame = Profiler.GetFrame( DisplayIndex );
		if ( ImGui::BeginTabItem( "Hierarchy" ) )
		{
			DrawHierarchyTab( Profiler, State, Frame );
			ImGui::EndTabItem();
		}
		if ( ImGui::BeginTabItem( "Timeline" ) )
		{
			// タイムラインの Auto Scroll は、この窓の Live とは別（押さない限り勝手に動かない）
			NDebugUI::DrawProfilerTimeline( Profiler, State.Timeline, State.SelectedFrameNumber );
			ImGui::EndTabItem();
		}
		if ( ImGui::BeginTabItem( "Stats" ) )
		{
			DrawStatsTab( Profiler, State );
			ImGui::EndTabItem();
		}
		if ( ImGui::BeginTabItem( "Counters" ) )
		{
			DrawCountersTab( Profiler, State );
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	if ( !State.StatusMessage.IsEmpty() ) ImGui::TextWrapped( "%s", *State.StatusMessage.ToUtf8() );
	ImGui::End();
}
