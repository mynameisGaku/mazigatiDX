#include "SpriteSheetEditorInternal.h"

namespace NSpriteSheetEditorUI
{
	//=========================================================================
	// 左: 元の画像・検証
	//=========================================================================
	void DrawSourceTab( FState& S )
	{
		const FSpriteSheetDefinition& Definition = S.Document.Get();

		ImGui::TextUnformatted( "Texture" );
		FString Selected = Definition.TexturePath;
		ImGui::SetNextItemWidth( -FLT_MIN );
		if ( NDebugUI::FileCombo( "##Texture", Selected, S.TextureFiles, "(choose a texture)" ) ) NSpriteSheetEdit::SetTexturePath( S.Document, Selected );

		const int32 W = TextureWidth( S );
		const int32 H = TextureHeight( S );
		if ( S.Sheet.IsValid() && S.Sheet->GetTexture().IsValid() )
		{
			ImGui::Text( "%d x %d px, %s in memory", W, H, UI_TEXT( NDebugUI::FormatBytes( S.Sheet->GetTexture()->GetMemoryBytes() ) ) );
		}
		ImGui::Text( "%d frame(s): %d from %d grid(s), %d free", Definition.GetNumFrames(), Definition.GetNumGridFrames(), Definition.Grids.Num(), Definition.Frames.Num() );
		ImGui::Text( "%d clip(s)", Definition.Clips.Num() );
		ImGui::Separator();

		const TArray<FString> Problems = NSpriteSheetEdit::Validate( Definition, W, H );
		if ( Problems.IsEmpty() )
		{
			ImGui::TextColored( ImVec4( 0.5f, 1.0f, 0.5f, 1.0f ), "No problems found." );
		}
		else
		{
			ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.35f, 1.0f ), "%d problem(s):", Problems.Num() );
			for ( const FString& Problem : Problems ) ImGui::TextWrapped( "- %s", UI_TEXT( Problem ) );
		}
	}

	//=========================================================================
	// 左: 格子
	//=========================================================================
	void DrawGridTab( FState& S )
	{
		const FSpriteSheetDefinition& Definition = S.Document.Get();
		if ( ImGui::Button( "Add grid" ) )
		{
			FSpriteSheetGrid Grid;
			S.SelectedGrid = NSpriteSheetEdit::AddGrid( S.Document, Grid );
		}
		ImGui::SameLine();
		ImGui::BeginDisabled( !Definition.Grids.IsValidIndex( S.SelectedGrid ) );
		if ( ImGui::Button( "Remove grid" ) )
		{
			NSpriteSheetEdit::RemoveGrid( S.Document, S.SelectedGrid );
			S.SelectedGrid = NMath::Min( S.SelectedGrid, S.Document.Get().Grids.Num() - 1 );
		}
		ImGui::EndDisabled();
		ImGui::PushTextWrapPos( 0.0f );
		ImGui::TextDisabled( "A grid cuts the image into equal cells. Frame numbers follow the grid order." );
		ImGui::PopTextWrapPos();

		int32 Start = 0;
		for ( int32 i = 0; i < Definition.Grids.Num(); ++i )
		{
			const FSpriteSheetGrid& Grid = Definition.Grids[i];
			char Label[96];
			NCString::Format( Label, sizeof( Label ), "Grid %d:  %d x %d,  frames %d..%d###grid%d", i, Grid.Columns, Grid.Rows, Start, Start + Grid.GetNumFrames() - 1, i );
			if ( ImGui::Selectable( Label, S.SelectedGrid == i ) ) S.SelectedGrid = i;
			Start += Grid.GetNumFrames();
		}
		if ( !Definition.Grids.IsValidIndex( S.SelectedGrid ) ) return;

		ImGui::Separator();
		FSpriteSheetGrid Grid = Definition.Grids[S.SelectedGrid];
		bool bChanged = false;
		bChanged |= ImGui::DragInt( "Columns", &Grid.Columns, 0.1f, 1, 512 );
		bChanged |= ImGui::DragInt( "Rows", &Grid.Rows, 0.1f, 1, 512 );
		bChanged |= ImGui::DragInt( "Cell width", &Grid.CellWidth, 0.2f, 0, 4096, Grid.CellWidth == 0 ? "auto" : "%d px" );
		bChanged |= ImGui::DragInt( "Cell height", &Grid.CellHeight, 0.2f, 0, 4096, Grid.CellHeight == 0 ? "auto" : "%d px" );
		bChanged |= ImGui::DragInt( "Frames used", &Grid.NumFrames, 0.1f, 0, NMath::Max( Grid.Columns * Grid.Rows, 1 ), Grid.NumFrames == 0 ? "all" : "%d" );
		bChanged |= ImGui::DragInt( "Margin X", &Grid.MarginX, 0.1f, 0, 1024, "%d px" );
		bChanged |= ImGui::DragInt( "Margin Y", &Grid.MarginY, 0.1f, 0, 1024, "%d px" );
		bChanged |= ImGui::DragInt( "Spacing X", &Grid.SpacingX, 0.1f, 0, 1024, "%d px" );
		bChanged |= ImGui::DragInt( "Spacing Y", &Grid.SpacingY, 0.1f, 0, 1024, "%d px" );
		int Order = Grid.Order == ESpriteGridOrder::RowMajor ? 0 : 1;
		if ( ImGui::Combo( "Order", &Order, "Row by row\0Column by column\0" ) )
		{
			Grid.Order = Order == 0 ? ESpriteGridOrder::RowMajor : ESpriteGridOrder::ColumnMajor;
			bChanged = true;
		}
		bChanged |= ImGui::Checkbox( "Custom pivot", &Grid.bCustomPivot );
		if ( Grid.bCustomPivot )
		{
			bChanged |= ImGui::DragFloat( "Pivot X", &Grid.PivotX, 0.1f, -4096.0f, 4096.0f, "%.1f px" );
			bChanged |= ImGui::DragFloat( "Pivot Y", &Grid.PivotY, 0.1f, -4096.0f, 4096.0f, "%.1f px" );
		}
		if ( bChanged )
		{
			Grid.Columns = NMath::Max( Grid.Columns, 1 );
			Grid.Rows = NMath::Max( Grid.Rows, 1 );
			NSpriteSheetEdit::SetGrid( S.Document, S.SelectedGrid, Grid, "grid" );
		}

		TArray<FSpriteFrame> Frames;
		CSpriteSheet::ComputeGridFrames( Grid, TextureWidth( S ), TextureHeight( S ), Frames );
		if ( !Frames.IsEmpty() ) ImGui::TextDisabled( "Cell size: %d x %d px%s", Frames[0].Width, Frames[0].Height, Grid.CellWidth == 0 || Grid.CellHeight == 0 ? " (automatic)" : "" );
	}

	//=========================================================================
	// 左: コマ
	//=========================================================================
	void DrawFramesTab( FState& S )
	{
		const FSpriteSheetDefinition& Definition = S.Document.Get();
		const int32 TexW = TextureWidth( S );
		const int32 TexH = TextureHeight( S );
		TArray<FOverlayFrame> Overlay;
		BuildOverlay( Definition, TexW, TexH, Overlay );

		if ( ImGui::Button( "Add free frame" ) )
		{
			FSpriteFrame Frame { 0, 0, 16, 16, 8.0f, 8.0f };
			if ( Overlay.IsValidIndex( S.SelectedFrame ) )
			{
				const FSpriteFrame& From = Overlay[S.SelectedFrame].Frame;
				Frame = From;
			}
			S.SelectedFrame = NSpriteSheetEdit::AddFrame( S.Document, Frame );
		}
		ImGui::SameLine();
		const bool bFreeSelected = Overlay.IsValidIndex( S.SelectedFrame ) && Overlay[S.SelectedFrame].bFree;
		ImGui::BeginDisabled( !bFreeSelected );
		if ( ImGui::Button( "Delete" ) )
		{
			NSpriteSheetEdit::RemoveFrame( S.Document, S.SelectedFrame );
			S.SelectedFrame = INDEX_NONE;
		}
		ImGui::EndDisabled();
		ImGui::PushTextWrapPos( 0.0f );
		ImGui::TextDisabled( "Shift+drag on the image to draw a free frame. Grid cells are edited in the Grid tab." );
		ImGui::PopTextWrapPos();

		ImGui::BeginChild( "##FrameList", ImVec2( 0.0f, ImGui::GetContentRegionAvail().y * 0.55f ), ImGuiChildFlags_Borders );
		ImGuiListClipper Clipper;
		Clipper.Begin( Overlay.Num() );
		while ( Clipper.Step() )
		{
			for ( int32 i = Clipper.DisplayStart; i < Clipper.DisplayEnd; ++i )
			{
				const FOverlayFrame& Item = Overlay[i];
				char Label[128];
				NCString::Format( Label, sizeof( Label ), "%3d  (%d,%d) %dx%d  %s%s###frame%d", Item.Index, Item.Frame.X, Item.Frame.Y, Item.Frame.Width, Item.Frame.Height,
							   Item.bFree ? "free" : "grid", Item.bOutside ? "  OUTSIDE" : "", Item.Index );
				if ( ImGui::Selectable( Label, S.SelectedFrame == Item.Index ) ) S.SelectedFrame = Item.Index;
			}
		}
		ImGui::EndChild();

		if ( !Overlay.IsValidIndex( S.SelectedFrame ) ) return;
		const FOverlayFrame& Item = Overlay[S.SelectedFrame];
		ImGui::Text( "Frame %d (%s)", Item.Index, Item.bFree ? "free" : "from a grid" );
		if ( Item.bFree )
		{
			FSpriteFrame Frame = Item.Frame;
			bool bChanged = false;
			bChanged |= ImGui::DragInt( "X", &Frame.X, 0.2f, 0, 65535 );
			bChanged |= ImGui::DragInt( "Y", &Frame.Y, 0.2f, 0, 65535 );
			bChanged |= ImGui::DragInt( "Width", &Frame.Width, 0.2f, 1, 65535 );
			bChanged |= ImGui::DragInt( "Height", &Frame.Height, 0.2f, 1, 65535 );
			bChanged |= ImGui::DragFloat( "Pivot X", &Frame.PivotX, 0.1f, -4096.0f, 4096.0f, "%.1f px" );
			bChanged |= ImGui::DragFloat( "Pivot Y", &Frame.PivotY, 0.1f, -4096.0f, 4096.0f, "%.1f px" );
			if ( bChanged ) NSpriteSheetEdit::SetFrame( S.Document, S.SelectedFrame, Frame, "frame" );
		}
		else
		{
			ImGui::Text( "Rectangle: %d,%d  %d x %d", Item.Frame.X, Item.Frame.Y, Item.Frame.Width, Item.Frame.Height );
			ImGui::Text( "Pivot: %.1f, %.1f", Item.Frame.PivotX, Item.Frame.PivotY );
			if ( ImGui::Button( "Copy as a free frame" ) ) S.SelectedFrame = NSpriteSheetEdit::AddFrame( S.Document, Item.Frame );
			ImGui::SameLine();
			ImGui::TextDisabled( "(to give just this frame its own pivot)" );
		}
	}

	//=========================================================================
	// 左: クリップ
	//=========================================================================
	void DrawClipsTab( FState& S )
	{
		const FSpriteSheetDefinition& Definition = S.Document.Get();
		const TSharedPtr<CTexture> Texture = S.Sheet.IsValid() ? S.Sheet->GetTexture() : TSharedPtr<CTexture>();
		const int32 TexW = TextureWidth( S );
		const int32 TexH = TextureHeight( S );
		const ImTextureID TextureId = Texture.IsValid() ? NDebugUI::GetImGuiTexture( Texture->GetHandle() ) : 0;

		if ( ImGui::Button( "Add clip" ) )
		{
			S.SelectedClip = NSpriteSheetEdit::AddClip( S.Document, "Clip", S.SelectedFrame );
			S.SelectedStep = 0;
			S.SelectedEvent = INDEX_NONE;
		}
		ImGui::SameLine();
		ImGui::BeginDisabled( !Definition.Clips.IsValidIndex( S.SelectedClip ) );
		if ( ImGui::Button( "Delete clip" ) )
		{
			NSpriteSheetEdit::RemoveClip( S.Document, S.SelectedClip );
			S.SelectedClip = NMath::Min( S.SelectedClip, S.Document.Get().Clips.Num() - 1 );
			S.SelectedStep = INDEX_NONE;
			S.SelectedEvent = INDEX_NONE;
		}
		ImGui::EndDisabled();

		ImGui::BeginChild( "##ClipList", ImVec2( 0.0f, 110.0f ), ImGuiChildFlags_Borders );
		for ( int32 i = 0; i < Definition.Clips.Num(); ++i )
		{
			const FSpriteClip& Clip = Definition.Clips[i];
			char Label[160];
			NCString::Format( Label, sizeof( Label ), "%s   (%d frame(s), %.2f s, %s)###clip%d", *Clip.Name, Clip.Frames.Num(), Clip.GetTotalDuration(), ToString( Clip.Loop ), i );
			if ( ImGui::Selectable( Label, S.SelectedClip == i ) )
			{
				S.SelectedClip = i;
				S.SelectedStep = INDEX_NONE;
				S.SelectedEvent = INDEX_NONE;
			}
		}
		if ( Definition.Clips.IsEmpty() ) ImGui::TextDisabled( "No clips yet. Click 'Add clip'." );
		ImGui::EndChild();

		if ( !Definition.Clips.IsValidIndex( S.SelectedClip ) ) return;
		const int32 ClipIndex = S.SelectedClip;
		const FSpriteClip& Clip = Definition.Clips[ClipIndex];

		// 名前: 入力が終わったときに反映する（途中で同じ名前と重なって番号がつくのを避ける）
		if ( !ImGui::IsAnyItemActive() ) S.NameBuffer = Clip.Name;
		NDebugUI::InputString( "Name", S.NameBuffer );
		if ( ImGui::IsItemDeactivatedAfterEdit() ) S.NameBuffer = NSpriteSheetEdit::RenameClip( S.Document, ClipIndex, S.NameBuffer );

		float32 Fps = Clip.Fps;
		if ( ImGui::DragFloat( "Fps", &Fps, 0.1f, 0.1f, 240.0f, "%.1f frames/s" ) ) NSpriteSheetEdit::SetClipFps( S.Document, ClipIndex, Fps, "fps" );
		int Loop = static_cast<int>( Clip.Loop );
		if ( ImGui::Combo( "Loop", &Loop, "Once\0Loop\0Ping-pong\0" ) ) NSpriteSheetEdit::SetClipLoop( S.Document, ClipIndex, static_cast<EAnimLoopMode>( Loop ) );
		ImGui::TextDisabled( "Length %.3f s (%d step(s)%s)", Clip.GetTotalDuration(), Clip.Frames.Num(), Clip.Durations.IsEmpty() ? ", even" : ", custom times" );

		// ステップ
		ImGui::SeparatorText( "Steps (in playing order)" );
		const FSpriteFrame* SelectedFrameRect = nullptr;
		(void)SelectedFrameRect;
		if ( ImGui::Button( "Append selected frame" ) && S.SelectedFrame != INDEX_NONE )
		{
			NSpriteSheetEdit::AppendStep( S.Document, ClipIndex, S.SelectedFrame );
			S.SelectedStep = S.Document.Get().Clips[ClipIndex].Frames.Num() - 1;
		}
		ImGui::TextUnformatted( "Range" );
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 56.0f );
		ImGui::DragInt( "##First", &S.RangeFirst, 0.1f, 0, NMath::Max( Definition.GetNumFrames() - 1, 0 ) );
		ImGui::SameLine();
		ImGui::TextUnformatted( "to" );
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 56.0f );
		ImGui::DragInt( "##Last", &S.RangeLast, 0.1f, 0, NMath::Max( Definition.GetNumFrames() - 1, 0 ) );
		ImGui::SameLine();
		if ( ImGui::Button( "Set range" ) )
		{
			NSpriteSheetEdit::SetStepRange( S.Document, ClipIndex, NMath::Clamp( S.RangeFirst, 0, NMath::Max( Definition.GetNumFrames() - 1, 0 ) ),
											NMath::Clamp( S.RangeLast, 0, NMath::Max( Definition.GetNumFrames() - 1, 0 ) ) );
			S.SelectedStep = 0;
		}
		if ( !Clip.Durations.IsEmpty() )
		{
			ImGui::SameLine();
			if ( ImGui::SmallButton( "Even times" ) ) NSpriteSheetEdit::ResetDurations( S.Document, ClipIndex );
		}

		ImGui::BeginChild( "##Steps", ImVec2( 0.0f, 150.0f ), ImGuiChildFlags_Borders );
		int32 RemoveStep = INDEX_NONE, MoveFrom = INDEX_NONE, MoveTo = INDEX_NONE, Duplicate = INDEX_NONE;
		for ( int32 Step = 0; Step < Clip.Frames.Num(); ++Step )
		{
			ImGui::PushID( Step );
			const bool bSelected = S.SelectedStep == Step;
			char Label[32];
			NCString::Format( Label, sizeof( Label ), "%2d", Step );
			if ( ImGui::Selectable( Label, bSelected, 0, ImVec2( 26.0f, 24.0f ) ) )
			{
				S.SelectedStep = Step;
				if ( Clip.Frames[Step] >= 0 ) S.SelectedFrame = Clip.Frames[Step];
			}
			ImGui::SameLine();
			const FSpriteFrame* Frame = S.Sheet.IsValid() ? S.Sheet->GetFrame( Clip.Frames[Step] ) : nullptr;
			if ( Frame != nullptr && Frame->Width > 0 )
			{
				const float Scale = NMath::Min( 24.0f / static_cast<float>( Frame->Width ), 24.0f / static_cast<float>( Frame->Height ) );
				ImGui::SetCursorPosY( ImGui::GetCursorPosY() );
				NDebugUI::ImageRect( TextureId, TexW, TexH, Frame->X, Frame->Y, Frame->Width, Frame->Height, ImVec2( static_cast<float>( Frame->Width ) * Scale, static_cast<float>( Frame->Height ) * Scale ) );
			}
			else
			{
				ImGui::Dummy( ImVec2( 24.0f, 24.0f ) );
			}
			ImGui::SameLine();
			int32 FrameNumber = Clip.Frames[Step];
			ImGui::SetNextItemWidth( 56.0f );
			if ( ImGui::DragInt( "##Frame", &FrameNumber, 0.1f, 0, NMath::Max( Definition.GetNumFrames() - 1, 0 ) ) ) NSpriteSheetEdit::SetStepFrame( S.Document, ClipIndex, Step, FrameNumber );
			ImGui::SameLine();
			float32 Milliseconds = Clip.GetDuration( Step ) * 1000.0f;
			ImGui::SetNextItemWidth( 70.0f );
			if ( ImGui::DragFloat( "##Time", &Milliseconds, 1.0f, 1.0f, 5000.0f, "%.0f ms" ) )
			{
				const FString Key = FString::Printf( "dur%d", Step );
				NSpriteSheetEdit::SetStepDuration( S.Document, ClipIndex, Step, Milliseconds / 1000.0f, *Key );
			}
			ImGui::SameLine();
			if ( ImGui::SmallButton( "^" ) && Step > 0 ) { MoveFrom = Step; MoveTo = Step - 1; }
			ImGui::SameLine();
			if ( ImGui::SmallButton( "v" ) && Step + 1 < Clip.Frames.Num() ) { MoveFrom = Step; MoveTo = Step + 1; }
			ImGui::SameLine();
			if ( ImGui::SmallButton( "+" ) ) Duplicate = Step;
			if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Duplicate this step" );
			ImGui::SameLine();
			if ( ImGui::SmallButton( "x" ) ) RemoveStep = Step;
			ImGui::PopID();
		}
		ImGui::EndChild();
		if ( MoveFrom != INDEX_NONE )
		{
			NSpriteSheetEdit::MoveStep( S.Document, ClipIndex, MoveFrom, MoveTo );
			S.SelectedStep = MoveTo;
		}
		if ( Duplicate != INDEX_NONE ) NSpriteSheetEdit::InsertStep( S.Document, ClipIndex, Duplicate + 1, Clip.Frames[Duplicate] );
		if ( RemoveStep != INDEX_NONE )
		{
			NSpriteSheetEdit::RemoveStep( S.Document, ClipIndex, RemoveStep );
			S.SelectedStep = NMath::Min( S.SelectedStep, S.Document.Get().Clips[ClipIndex].Frames.Num() - 1 );
		}

		// 出来事
		ImGui::SeparatorText( "Events" );
		const FSpriteClip& Current = S.Document.Get().Clips[ClipIndex];
		ImGui::SetNextItemWidth( 120.0f );
		NDebugUI::InputString( "##NewEvent", S.NewEventName );
		ImGui::SameLine();
		if ( ImGui::Button( "Add on selected step" ) )
		{
			S.SelectedEvent = NSpriteSheetEdit::AddEvent( S.Document, ClipIndex, S.SelectedStep == INDEX_NONE ? 0 : S.SelectedStep, S.NewEventName );
		}
		ImGui::PushTextWrapPos( 0.0f );
		ImGui::TextDisabled( "An event fires when the animation enters its step. Read it with ConsumeEvent( \"Name\" )." );
		ImGui::PopTextWrapPos();
		int32 RemoveEvent = INDEX_NONE;
		for ( int32 i = 0; i < Current.Events.Num(); ++i )
		{
			ImGui::PushID( 1000 + i );
			const FSpriteAnimEvent& Event = Current.Events[i];
			if ( ImGui::Selectable( "##Event", S.SelectedEvent == i, 0, ImVec2( 12.0f, 0.0f ) ) ) S.SelectedEvent = i;
			ImGui::SameLine();
			int32 Step = Event.Step;
			ImGui::SetNextItemWidth( 60.0f );
			if ( ImGui::DragInt( "##Step", &Step, 0.1f, 0, NMath::Max( Current.Frames.Num() - 1, 0 ), "step %d" ) )
			{
				const FString Key = FString::Printf( "evstep%d", i );
				NSpriteSheetEdit::SetEvent( S.Document, ClipIndex, i, Step, Event.Name, *Key );
			}
			ImGui::SameLine();
			// 名前は入力が終わったときに反映する
			if ( S.EventNameBufferIndex != i || !ImGui::IsAnyItemActive() ) { S.EventNameBuffer = Event.Name; }
			ImGui::SetNextItemWidth( 140.0f );
			FString Buffer = S.EventNameBufferIndex == i ? S.EventNameBuffer : Event.Name;
			if ( NDebugUI::InputString( "##EventName", Buffer ) )
			{
				S.EventNameBuffer = Buffer;
				S.EventNameBufferIndex = i;
			}
			if ( ImGui::IsItemDeactivatedAfterEdit() )
			{
				NSpriteSheetEdit::SetEvent( S.Document, ClipIndex, i, Event.Step, S.EventNameBuffer );
				S.EventNameBufferIndex = INDEX_NONE;
			}
			ImGui::SameLine();
			if ( ImGui::SmallButton( "x" ) ) RemoveEvent = i;
			ImGui::PopID();
		}
		if ( RemoveEvent != INDEX_NONE )
		{
			NSpriteSheetEdit::RemoveEvent( S.Document, ClipIndex, RemoveEvent );
			S.SelectedEvent = INDEX_NONE;
		}
	}
}
