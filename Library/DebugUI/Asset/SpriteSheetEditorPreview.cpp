#include "SpriteSheetEditorInternal.h"

namespace NSpriteSheetEditorUI
{
	//=========================================================================
	// 右: プレビュー
	//=========================================================================
	void DrawPreview( FState& S, float DeltaTime )
	{
		const FSpriteSheetDefinition& Definition = S.Document.Get();
		const FSpriteClip* Clip = Definition.Clips.IsValidIndex( S.SelectedClip ) ? &Definition.Clips[S.SelectedClip] : nullptr;
		const TSharedPtr<CTexture> Texture = S.Sheet.IsValid() ? S.Sheet->GetTexture() : TSharedPtr<CTexture>();
		const int32 TexW = TextureWidth( S );
		const int32 TexH = TextureHeight( S );

		// 再生
		S.Preview.Speed = S.PreviewSpeed;
		if ( S.bPlaying && Clip != nullptr )
		{
			if ( S.Preview.IsFinished() && Clip->Loop == EAnimLoopMode::Once && ImGui::GetTime() - 0.0 > 0.0 )
			{
				// Once は最後まで行ったら止まる（Restart で戻る）
			}
			S.Preview.Resume();
			S.Preview.Update( DeltaTime );
			for ( const FString& Event : S.Preview.GetEvents() )
			{
				S.EventLog.Add( TPair<FString, float32> { Event, 1.2f } );
				if ( S.EventLog.Num() > 5 ) S.EventLog.RemoveAt( 0 );
			}
		}
		for ( int32 i = S.EventLog.Num() - 1; i >= 0; --i )
		{
			S.EventLog[i].Value -= DeltaTime;
			if ( S.EventLog[i].Value <= 0.0f ) S.EventLog.RemoveAt( i );
		}

		if ( ImGui::Button( S.bPlaying ? "Pause" : "Play" ) )
		{
			S.bPlaying = !S.bPlaying;
			if ( !S.bPlaying ) S.Preview.Pause();
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Restart" ) )
		{
			RestartPreview( S, false );
			S.bPlaying = true;
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 90.0f );
		ImGui::SliderFloat( "##Speed", &S.PreviewSpeed, 0.1f, 4.0f, "x%.2f" );
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 90.0f );
		ImGui::SliderFloat( "##PreviewScale", &S.PreviewScale, 1.0f, 12.0f, "%.0fx", ImGuiSliderFlags_AlwaysClamp );
		ImGui::Checkbox( "Flip X", &S.bPreviewFlipX );
		ImGui::SameLine();
		ImGui::Checkbox( "Pivot", &S.bPreviewPivots );

		const ImVec2 Origin = ImGui::GetCursorScreenPos();
		const ImVec2 Size( ImGui::GetContentRegionAvail().x, 230.0f );
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImVec2 Max( Origin.x + Size.x, Origin.y + Size.y );
		ImGui::InvisibleButton( "##PreviewArea", Size );
		DrawList->PushClipRect( Origin, Max, true );
		NDebugUI::DrawChecker( DrawList, Origin, Max );

		const int32 FrameIndex = S.Preview.GetFrameIndex();
		const FSpriteFrame* Frame = S.Sheet.IsValid() ? S.Sheet->GetFrame( FrameIndex ) : nullptr;
		const ImVec2 Center( Origin.x + Size.x * 0.5f, Origin.y + Size.y * 0.55f );
		DrawList->AddLine( ImVec2( Origin.x, Center.y ), ImVec2( Max.x, Center.y ), IM_COL32( 255, 255, 255, 30 ) );
		DrawList->AddLine( ImVec2( Center.x, Origin.y ), ImVec2( Center.x, Max.y ), IM_COL32( 255, 255, 255, 30 ) );
		if ( Frame != nullptr && Frame->Width > 0 && Texture.IsValid() && TexW > 0 )
		{
			// 基準点が中心に来るように置く。左右反転は基準点を軸にする。大きなコマは、領域に収まるように倍率を下げる
			const float Above = NMath::Max( Frame->PivotY, 1.0f ), Below = NMath::Max( static_cast<float>( Frame->Height ) - Frame->PivotY, 1.0f );
			const float Side = NMath::Max( NMath::Max( Frame->PivotX, static_cast<float>( Frame->Width ) - Frame->PivotX ), 1.0f );
			const float Fit = NMath::Min( NMath::Min( ( Center.y - Origin.y - 6.0f ) / Above, ( Max.y - Center.y - 6.0f ) / Below ), ( Size.x * 0.5f - 6.0f ) / Side );
			const float Scale = NMath::Max( NMath::Min( S.PreviewScale, Fit ), 0.5f );
			const float Left = S.bPreviewFlipX ? Center.x - ( static_cast<float>( Frame->Width ) - Frame->PivotX ) * Scale : Center.x - Frame->PivotX * Scale;
			const ImVec2 P0( Left, Center.y - Frame->PivotY * Scale );
			const ImVec2 P1( P0.x + static_cast<float>( Frame->Width ) * Scale, P0.y + static_cast<float>( Frame->Height ) * Scale );
			ImVec2 Uv0( static_cast<float>( Frame->X ) / static_cast<float>( TexW ), static_cast<float>( Frame->Y ) / static_cast<float>( TexH ) );
			ImVec2 Uv1( static_cast<float>( Frame->X + Frame->Width ) / static_cast<float>( TexW ), static_cast<float>( Frame->Y + Frame->Height ) / static_cast<float>( TexH ) );
			if ( S.bPreviewFlipX ) NAlgo::Swap( Uv0.x, Uv1.x );
			NDebugUI::BeginNearest( DrawList );
			DrawList->AddImage( NDebugUI::GetImGuiTexture( Texture->GetHandle() ), P0, P1, Uv0, Uv1 );
			NDebugUI::EndNearest( DrawList );
			if ( S.bPreviewPivots )
			{
				DrawList->AddCircle( Center, 4.0f, IM_COL32( 255, 60, 60, 255 ) );
				DrawList->AddRect( P0, P1, IM_COL32( 255, 255, 255, 50 ) );
			}
		}
		else
		{
			DrawList->AddText( ImVec2( Origin.x + 8.0f, Origin.y + 8.0f ), IM_COL32( 230, 230, 230, 255 ), "Select a clip, or a frame, to preview it." );
		}

		// 出来事の表示
		float Y = Origin.y + 6.0f;
		for ( const TPair<FString, float32>& Entry : S.EventLog )
		{
			DrawList->AddText( ImVec2( Max.x - 150.0f, Y ), IM_COL32( 255, 220, 80, static_cast<int>( 255.0f * NMath::Min( Entry.Value * 2.0f, 1.0f ) ) ), UI_TEXT( "event: " + Entry.Key ) );
			Y += 14.0f;
		}
		DrawList->PopClipRect();

		if ( Clip != nullptr )
		{
			ImGui::Text( "%s   step %d/%d   frame %d   %.3f / %.3f s%s", UI_TEXT( Clip->Name ), S.Preview.GetStep() + 1, Clip->Frames.Num(), FrameIndex,
						 S.Preview.GetNormalizedTime() * Clip->GetTotalDuration(), Clip->GetTotalDuration(), S.Preview.IsFinished() ? "   (finished)" : "" );
		}
		else if ( FrameIndex != INDEX_NONE )
		{
			ImGui::Text( "Frame %d", FrameIndex );
		}
	}
}
