#include "SpriteSheetEditorInternal.h"

namespace NSpriteSheetEditorUI
{
	//=========================================================================
	// 右: タイムライン（コマの長さ・出来事）
	//=========================================================================
	void DrawTimeline( FState& S )
	{
		const FSpriteSheetDefinition& Definition = S.Document.Get();
		if ( !Definition.Clips.IsValidIndex( S.SelectedClip ) )
		{
			ImGui::TextDisabled( "Select a clip to see its timeline." );
			return;
		}
		const int32 ClipIndex = S.SelectedClip;
		const FSpriteClip& Clip = Definition.Clips[ClipIndex];
		if ( Clip.Frames.IsEmpty() ) return;

		const TSharedPtr<CTexture> Texture = S.Sheet.IsValid() ? S.Sheet->GetTexture() : TSharedPtr<CTexture>();
		const int32 TexW = TextureWidth( S );
		const int32 TexH = TextureHeight( S );
		const ImTextureID TextureId = Texture.IsValid() ? NDebugUI::GetImGuiTexture( Texture->GetHandle() ) : 0;

		ImGui::SeparatorText( "Timeline" );
		const float32 Total = NMath::Max( Clip.GetTotalDuration(), 0.001f );
		const ImVec2 Origin = ImGui::GetCursorScreenPos();
		const float Width = ImGui::GetContentRegionAvail().x;
		const float RulerHeight = 16.0f, EventHeight = 30.0f, BlockHeight = 56.0f;
		const float TotalHeight = RulerHeight + EventHeight + BlockHeight + 6.0f;
		const float PixelsPerSecond = ( Width - 8.0f ) / Total;
		ImGui::InvisibleButton( "##TimelineArea", ImVec2( Width, TotalHeight ) );
		const bool bAreaHovered = ImGui::IsItemHovered();
		const bool bAreaActive = ImGui::IsItemActive();
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		ImGuiIO& IO = ImGui::GetIO();
		DrawList->AddRectFilled( Origin, ImVec2( Origin.x + Width, Origin.y + TotalHeight ), IM_COL32( 30, 30, 34, 255 ) );

		// 目盛り
		const float32 Step = Total > 4.0f ? 1.0f : ( Total > 1.0f ? 0.25f : 0.1f );
		for ( float32 T = 0.0f; T <= Total + 1.0e-4f; T += Step )
		{
			const float X = Origin.x + 4.0f + T * PixelsPerSecond;
			DrawList->AddLine( ImVec2( X, Origin.y ), ImVec2( X, Origin.y + RulerHeight ), IM_COL32( 160, 160, 170, 255 ) );
			char Label[16];
			NCString::Format( Label, sizeof( Label ), "%.2f", T );
			DrawList->AddText( ImVec2( X + 2.0f, Origin.y ), IM_COL32( 160, 160, 170, 255 ), Label );
		}

		// コマのブロック
		const float BlockTop = Origin.y + RulerHeight + EventHeight;
		float Cumulative = 0.0f;
		float StepStartX[256] = {};
		const int32 NumDrawn = NMath::Min( Clip.Frames.Num(), 256 );
		int32 ResizingStep = INDEX_NONE;
		for ( int32 i = 0; i < NumDrawn; ++i )
		{
			const float Duration = Clip.GetDuration( i );
			const float X0 = Origin.x + 4.0f + Cumulative * PixelsPerSecond;
			const float X1 = X0 + Duration * PixelsPerSecond;
			StepStartX[i] = X0;
			Cumulative += Duration;

			const bool bSelected = S.SelectedStep == i;
			const bool bCurrent = S.Preview.GetStep() >= 0 && Clip.Loop != EAnimLoopMode::PingPong && S.Preview.GetClipIndex() == ClipIndex && S.Preview.GetNormalizedTime() * Total >= Cumulative - Duration && S.Preview.GetNormalizedTime() * Total < Cumulative;
			DrawList->AddRectFilled( ImVec2( X0, BlockTop ), ImVec2( X1 - 1.0f, BlockTop + BlockHeight ), bSelected ? IM_COL32( 80, 110, 170, 255 ) : ( i % 2 == 0 ? IM_COL32( 58, 66, 84, 255 ) : IM_COL32( 50, 58, 74, 255 ) ) );
			if ( bCurrent ) DrawList->AddRect( ImVec2( X0, BlockTop ), ImVec2( X1 - 1.0f, BlockTop + BlockHeight ), IM_COL32( 255, 220, 80, 255 ), 0.0f, 0, 2.0f );
			const FSpriteFrame* Frame = S.Sheet.IsValid() ? S.Sheet->GetFrame( Clip.Frames[i] ) : nullptr;
			if ( Frame != nullptr && Frame->Width > 0 && X1 - X0 > 28.0f )
			{
				const float Scale = NMath::Min( ( X1 - X0 - 6.0f ) / static_cast<float>( Frame->Width ), 34.0f / static_cast<float>( Frame->Height ) );
				const ImVec2 P0( X0 + 3.0f, BlockTop + 3.0f );
				const ImVec2 P1( P0.x + static_cast<float>( Frame->Width ) * Scale, P0.y + static_cast<float>( Frame->Height ) * Scale );
				NDebugUI::BeginNearest( DrawList );
				DrawList->AddImage( TextureId, P0, P1, ImVec2( static_cast<float>( Frame->X ) / static_cast<float>( TexW ), static_cast<float>( Frame->Y ) / static_cast<float>( TexH ) ),
									ImVec2( static_cast<float>( Frame->X + Frame->Width ) / static_cast<float>( TexW ), static_cast<float>( Frame->Y + Frame->Height ) / static_cast<float>( TexH ) ) );
				NDebugUI::EndNearest( DrawList );
			}
			char Label[32];
			NCString::Format( Label, sizeof( Label ), "#%d", Clip.Frames[i] );
			DrawList->AddText( ImVec2( X0 + 3.0f, BlockTop + BlockHeight - 16.0f ), IM_COL32( 230, 230, 230, 255 ), Label );

			// ブロックの右端をドラッグして、表示時間を変える
			ImGui::SetCursorScreenPos( ImVec2( X1 - 5.0f, BlockTop ) );
			ImGui::PushID( 5000 + i );
			ImGui::InvisibleButton( "##Resize", ImVec2( 8.0f, BlockHeight ) );
			if ( ImGui::IsItemHovered() || ImGui::IsItemActive() ) ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
			if ( ImGui::IsItemActive() && IO.MouseDelta.x != 0.0f )
			{
				const float32 NewDuration = NMath::Max( Duration + IO.MouseDelta.x / PixelsPerSecond, 0.005f );
				const FString Key = FString::Printf( "tldur%d", i );
				NSpriteSheetEdit::SetStepDuration( S.Document, ClipIndex, i, NewDuration, *Key );
				ResizingStep = i;
			}
			ImGui::PopID();
			// ブロックそのものをクリックして選ぶ
			ImGui::SetCursorScreenPos( ImVec2( X0, BlockTop ) );
			ImGui::PushID( 6000 + i );
			ImGui::InvisibleButton( "##Block", ImVec2( NMath::Max( X1 - X0 - 6.0f, 4.0f ), BlockHeight ) );
			if ( ImGui::IsItemClicked() )
			{
				S.SelectedStep = i;
				S.SelectedFrame = Clip.Frames[i];
				if ( !S.bPlaying ) S.Preview.Seek( ( Cumulative - Duration + Duration * 0.5f ) / Total );
			}
			ImGui::PopID();
		}
		(void)ResizingStep;

		// 出来事の旗（ドラッグで別のステップへ。右クリックで削除）
		for ( int32 i = 0; i < Clip.Events.Num(); ++i )
		{
			const FSpriteAnimEvent& Event = Clip.Events[i];
			if ( Event.Step < 0 || Event.Step >= NumDrawn ) continue;
			const float X = StepStartX[Event.Step] + 1.0f + static_cast<float>( i % 3 ) * 0.0f;
			const float Y = Origin.y + RulerHeight + ( i % 2 ) * 14.0f;
			const bool bSelected = S.SelectedEvent == i;
			const ImVec2 Flag0( X, Y ), Flag1( X + 8.0f + static_cast<float>( Event.Name.Len() ) * 6.5f, Y + 13.0f );
			DrawList->AddLine( ImVec2( X, Y ), ImVec2( X, BlockTop ), bSelected ? IM_COL32( 255, 220, 80, 255 ) : IM_COL32( 255, 150, 60, 200 ) );
			DrawList->AddRectFilled( Flag0, Flag1, bSelected ? IM_COL32( 255, 200, 60, 255 ) : IM_COL32( 230, 120, 40, 255 ) );
			DrawList->AddText( ImVec2( X + 4.0f, Y ), IM_COL32( 20, 20, 20, 255 ), UI_TEXT( Event.Name ) );

			ImGui::SetCursorScreenPos( Flag0 );
			ImGui::PushID( 7000 + i );
			ImGui::InvisibleButton( "##Event", ImVec2( Flag1.x - Flag0.x, Flag1.y - Flag0.y ) );
			if ( ImGui::IsItemClicked() ) S.SelectedEvent = i;
			if ( ImGui::IsItemActive() && ImGui::IsMouseDragging( ImGuiMouseButton_Left ) )
			{
				// マウスの位置に近いステップへ動かす
				int32 Nearest = Event.Step;
				float Best = 1.0e30f;
				for ( int32 s = 0; s < NumDrawn; ++s )
				{
					const float D = NMath::Abs( IO.MousePos.x - StepStartX[s] );
					if ( D < Best ) { Best = D; Nearest = s; }
				}
				if ( Nearest != Event.Step )
				{
					const FString Key = FString::Printf( "evdrag%d", i );
					NSpriteSheetEdit::SetEvent( S.Document, ClipIndex, i, Nearest, Event.Name, *Key );
				}
			}
			if ( ImGui::BeginPopupContextItem( "##EventMenu" ) )
			{
				if ( ImGui::MenuItem( "Delete event" ) )
				{
					NSpriteSheetEdit::RemoveEvent( S.Document, ClipIndex, i );
					S.SelectedEvent = INDEX_NONE;
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}

		// 再生位置（Ping-pong 以外）
		if ( Clip.Loop != EAnimLoopMode::PingPong && S.Preview.GetClipIndex() == ClipIndex )
		{
			const float X = Origin.x + 4.0f + S.Preview.GetNormalizedTime() * Total * PixelsPerSecond;
			DrawList->AddLine( ImVec2( X, Origin.y ), ImVec2( X, Origin.y + TotalHeight ), IM_COL32( 255, 80, 80, 255 ), 2.0f );
		}

		// 目盛りの上のドラッグで、再生位置を動かす（動かすと一時停止）
		if ( bAreaActive && ImGui::IsMouseDragging( ImGuiMouseButton_Left, 0.0f ) && IO.MousePos.y < Origin.y + RulerHeight + 2.0f )
		{
			S.bPlaying = false;
			S.Preview.Pause();
			S.Preview.Seek( NMath::Clamp( ( IO.MousePos.x - Origin.x - 4.0f ) / PixelsPerSecond / Total, 0.0f, 1.0f ) );
		}
		(void)bAreaHovered;
		ImGui::SetCursorScreenPos( ImVec2( Origin.x, Origin.y + TotalHeight + 4.0f ) );
		ImGui::PushTextWrapPos( 0.0f );
		ImGui::TextDisabled( "Drag a block's right edge: change its time.  Drag a flag: move the event.  Right-click a flag: delete.  Drag the ruler: scrub." );
		ImGui::PopTextWrapPos();
	}
}
