#include "ModelAnimationEditorInternal.h"

namespace NModelAnimationEditorUI
{
	//=========================================================================
	// タイムライン
	//=========================================================================
	void DrawTimeline( FState& S )
	{
		const FModelClipDef* Clip = SelectedClip( S );
		if ( Clip == nullptr )
		{
			ImGui::TextDisabled( "Select a clip to see its timeline." );
			return;
		}
		const int32 ClipIndex = S.SelectedClip;
		const float32 Length = S.Animator.GetClipLength( Clip->Name );

		// 再生の操作
		if ( ImGui::Button( S.bPlaying ? "Pause" : "Play" ) )
		{
			S.bPlaying = !S.bPlaying;
			if ( S.bPlaying )
			{
				S.Animator.Resume();
			}
			else
			{
				S.Animator.Pause();
			}
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Restart" ) )
		{
			PlaySelected( S, false );
			S.bPlaying = true;
			S.Animator.Resume();
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 100.0f );
		ImGui::SliderFloat( "##Speed", &S.PreviewSpeed, 0.1f, 3.0f, "x%.2f" );
		ImGui::SameLine();
		ImGui::TextDisabled( "length %s", UI_TEXT( NDebugUI::FormatSeconds( Length ) ) );
		ImGui::SameLine();
		if ( ImGui::Button( "Add event at playhead" ) )
		{
			S.SelectedEvent = NModelAnimationEdit::AddEvent( S.Document, ClipIndex, S.Animator.GetNormalizedTime(), S.NewEventName );
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 120.0f );
		NDebugUI::InputString( "##NewEvent", S.NewEventName );

		const ImVec2 Origin = ImGui::GetCursorScreenPos();
		const float Width = ImGui::GetContentRegionAvail().x;
		const float RulerHeight = 16.0f, FlagArea = 44.0f, TrackHeight = 26.0f;
		const float TotalHeight = RulerHeight + FlagArea + TrackHeight;
		const float TrackLeft = Origin.x + 8.0f;
		const float TrackWidth = Width - 16.0f;
		ImGuiIO& IO = ImGui::GetIO();
		ImDrawList* DrawList = ImGui::GetWindowDrawList();

		ImGui::InvisibleButton( "##TimelineArea", ImVec2( Width, TotalHeight ) );
		const bool bScrubbing = ImGui::IsItemActive();
		DrawList->AddRectFilled( Origin, ImVec2( Origin.x + Width, Origin.y + TotalHeight ), IM_COL32( 30, 30, 34, 255 ) );

		// 目盛り（秒）
		if ( Length > 0.0f )
		{
			const float32 Step = Length > 8.0f ? 1.0f : ( Length > 2.0f ? 0.5f : ( Length > 0.5f ? 0.1f : 0.05f ) );
			for ( float32 T = 0.0f; T <= Length + 1.0e-4f; T += Step )
			{
				const float X = TrackLeft + T / Length * TrackWidth;
				DrawList->AddLine( ImVec2( X, Origin.y ), ImVec2( X, Origin.y + RulerHeight ), IM_COL32( 160, 160, 170, 255 ) );
				char Label[16];
				NCString::Format( Label, sizeof( Label ), "%.2f", T );
				DrawList->AddText( ImVec2( X + 2.0f, Origin.y ), IM_COL32( 160, 160, 170, 255 ), Label );
			}
		}
		const float TrackTop = Origin.y + RulerHeight + FlagArea;
		DrawList->AddRectFilled( ImVec2( TrackLeft, TrackTop ), ImVec2( TrackLeft + TrackWidth, TrackTop + TrackHeight ), IM_COL32( 52, 60, 78, 255 ) );

		// 出来事の旗（ドラッグで動かす。右クリックで削除）
		for ( int32 i = 0; i < Clip->Events.Num(); ++i )
		{
			const FModelEventDef& Event = Clip->Events[i];
			const float X = TrackLeft + Event.Time * TrackWidth;
			const float Y = Origin.y + RulerHeight + 2.0f + static_cast<float>( i % 3 ) * 14.0f;
			const bool bSelected = S.SelectedEvent == i;
			bool bFlash = false;
			for ( const TPair<FString, float32>& Flash : S.EventFlashes )
			{
				if ( Flash.Key == Event.Name )
				{
					bFlash = true;
				}
			}
			const ImVec2 Flag0( X, Y ), Flag1( X + 10.0f + static_cast<float>( Event.Name.Len() ) * 6.5f, Y + 13.0f );
			DrawList->AddLine( ImVec2( X, Y ), ImVec2( X, TrackTop + TrackHeight ), bSelected || bFlash ? IM_COL32( 255, 220, 80, 255 ) : IM_COL32( 255, 150, 60, 200 ) );
			DrawList->AddRectFilled( Flag0, Flag1, bSelected || bFlash ? IM_COL32( 255, 210, 70, 255 ) : IM_COL32( 230, 120, 40, 255 ) );
			DrawList->AddText( ImVec2( X + 4.0f, Y ), IM_COL32( 20, 20, 20, 255 ), UI_TEXT( Event.Name ) );

			ImGui::SetCursorScreenPos( Flag0 );
			ImGui::PushID( 3000 + i );
			ImGui::InvisibleButton( "##Flag", ImVec2( Flag1.x - Flag0.x, Flag1.y - Flag0.y ) );
			if ( ImGui::IsItemClicked() )
			{
				S.SelectedEvent = i;
			}
			if ( ImGui::IsItemActive() && ImGui::IsMouseDragging( ImGuiMouseButton_Left, 1.0f ) )
			{
				const float32 NewTime = NMath::Clamp( ( IO.MousePos.x - TrackLeft ) / TrackWidth, 0.0f, 1.0f );
				const FString Key = FString::Printf( "evdrag%d", i );
				const int32 NewIndex = NModelAnimationEdit::SetEvent( S.Document, ClipIndex, i, NewTime, Event.Name, *Key );
				if ( S.SelectedEvent == i )
				{
					S.SelectedEvent = NewIndex;
				}
			}
			if ( ImGui::BeginPopupContextItem( "##FlagMenu" ) )
			{
				if ( ImGui::MenuItem( "Delete event" ) )
				{
					NModelAnimationEdit::RemoveEvent( S.Document, ClipIndex, i );
					S.SelectedEvent = INDEX_NONE;
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}

		// 再生位置
		const float32 Normalized = S.Animator.GetNormalizedTime();
		const float PlayheadX = TrackLeft + Normalized * TrackWidth;
		DrawList->AddLine( ImVec2( PlayheadX, Origin.y ), ImVec2( PlayheadX, Origin.y + TotalHeight ), IM_COL32( 255, 80, 80, 255 ), 2.0f );
		char TimeLabel[48];
		NCString::Format( TimeLabel, sizeof( TimeLabel ), "%.3f s (%.0f%%)", Normalized * Length, Normalized * 100.0f );
		DrawList->AddText( ImVec2( NMath::Min( PlayheadX + 4.0f, Origin.x + Width - 110.0f ), TrackTop + 6.0f ), IM_COL32( 255, 200, 200, 255 ), TimeLabel );

		// 背景のドラッグで、再生位置を動かす（動かすと一時停止）
		if ( bScrubbing && ImGui::IsMouseDown( ImGuiMouseButton_Left ) && Length > 0.0f )
		{
			bool bOnFlag = false;
			for ( int32 i = 0; i < Clip->Events.Num(); ++i )
			{
				bOnFlag = bOnFlag || ImGui::IsPopupOpen( "##FlagMenu" );
			}
			if ( !bOnFlag )
			{
				S.bPlaying = false;
				S.Animator.Pause();
				S.Animator.Seek( NMath::Clamp( ( IO.MousePos.x - TrackLeft ) / TrackWidth, 0.0f, 1.0f ) );
			}
		}
		ImGui::SetCursorScreenPos( ImVec2( Origin.x, Origin.y + TotalHeight + 4.0f ) );

		// 出来事の一覧
		int32 RemoveEvent = INDEX_NONE;
		for ( int32 i = 0; i < Clip->Events.Num(); ++i )
		{
			ImGui::PushID( 4000 + i );
			const FModelEventDef& Event = Clip->Events[i];
			if ( ImGui::Selectable( "##Event", S.SelectedEvent == i, 0, ImVec2( 12.0f, 0.0f ) ) )
			{
				S.SelectedEvent = i;
			}
			ImGui::SameLine();
			float32 Time = Event.Time;
			ImGui::SetNextItemWidth( 90.0f );
			if ( ImGui::DragFloat( "##Time", &Time, 0.002f, 0.0f, 1.0f, "%.3f" ) )
			{
				const FString Key = FString::Printf( "evtime%d", i );
				const int32 NewIndex = NModelAnimationEdit::SetEvent( S.Document, ClipIndex, i, Time, Event.Name, *Key );
				if ( S.SelectedEvent == i )
				{
					S.SelectedEvent = NewIndex;
				}
			}
			ImGui::SameLine();
			ImGui::TextDisabled( "%6.3f s", Event.Time * Length );
			ImGui::SameLine();
			if ( S.EventNameBufferIndex != i || !ImGui::IsAnyItemActive() )
			{
				S.EventNameBuffer = Event.Name;
			}
			FString Buffer = S.EventNameBufferIndex == i ? S.EventNameBuffer : Event.Name;
			ImGui::SetNextItemWidth( 150.0f );
			if ( NDebugUI::InputString( "##Name", Buffer ) )
			{
				S.EventNameBuffer = Buffer;
				S.EventNameBufferIndex = i;
			}
			if ( ImGui::IsItemDeactivatedAfterEdit() )
			{
				NModelAnimationEdit::SetEvent( S.Document, ClipIndex, i, Event.Time, S.EventNameBuffer );
				S.EventNameBufferIndex = INDEX_NONE;
			}
			ImGui::SameLine();
			if ( ImGui::SmallButton( "x" ) )
			{
				RemoveEvent = i;
			}
			ImGui::PopID();
		}
		if ( RemoveEvent != INDEX_NONE )
		{
			NModelAnimationEdit::RemoveEvent( S.Document, ClipIndex, RemoveEvent );
			S.SelectedEvent = INDEX_NONE;
		}
		if ( Clip->Events.IsEmpty() )
		{
			ImGui::TextDisabled( "No events. 'Add event at playhead' puts one where the playhead is (0..1 of the clip)." );
		}
	}
}
