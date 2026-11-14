#include "SpriteSheetEditorInternal.h"

namespace NSpriteSheetEditorUI
{
	bool IsOutside( const FSpriteFrame& Frame, int32 TexW, int32 TexH )
	{
		return Frame.Width <= 0 || Frame.Height <= 0 || Frame.X < 0 || Frame.Y < 0 || Frame.X + Frame.Width > TexW || Frame.Y + Frame.Height > TexH;
	}

	void BuildOverlay( const FSpriteSheetDefinition& Definition, int32 TexW, int32 TexH, TArray<FOverlayFrame>& Out )
	{
		Out.Reset();
		int32 Index = 0;
		for ( int32 GridIndex = 0; GridIndex < Definition.Grids.Num(); ++GridIndex )
		{
			TArray<FSpriteFrame> Frames;
			CSpriteSheet::ComputeGridFrames( Definition.Grids[GridIndex], TexW, TexH, Frames );
			for ( const FSpriteFrame& Frame : Frames )
			{
				FOverlayFrame Item;
				Item.Frame = Frame;
				Item.Index = Index++;
				Item.GridIndex = GridIndex;
				Item.bOutside = IsOutside( Frame, TexW, TexH );
				Out.Add( Item );
			}
		}
		for ( const FSpriteFrame& Frame : Definition.Frames )
		{
			FOverlayFrame Item;
			Item.Frame = Frame;
			Item.Index = Index++;
			Item.bFree = true;
			Item.bOutside = IsOutside( Frame, TexW, TexH );
			Out.Add( Item );
		}
	}

	ImU32 GridColor( int32 GridIndex, int32 Alpha )
	{
		static const ImU32 Hues[] = { IM_COL32( 0, 200, 255, 0 ), IM_COL32( 120, 255, 120, 0 ), IM_COL32( 255, 120, 255, 0 ), IM_COL32( 255, 170, 80, 0 ) };
		const ImU32 Base = Hues[( GridIndex < 0 ? 0 : GridIndex ) % 4];
		return ( Base & 0x00FFFFFF ) | ( static_cast<ImU32>( Alpha ) << 24 );
	}

	//=========================================================================
	// キャンバス: 画像と、分割・番号・基準点
	//=========================================================================
	void DrawCanvas( FState& S )
	{
		const FSpriteSheetDefinition& Definition = S.Document.Get();
		const TSharedPtr<CTexture> Texture = S.Sheet.IsValid() ? S.Sheet->GetTexture() : TSharedPtr<CTexture>();
		const int32 TexW = TextureWidth( S );
		const int32 TexH = TextureHeight( S );

		// ツールバー
		ImGui::SetNextItemWidth( 110.0f );
		if ( ImGui::SliderFloat( "##Zoom", &S.Zoom, 0.25f, 32.0f, "zoom %.2fx", ImGuiSliderFlags_Logarithmic ) ) S.bFitRequested = false;
		ImGui::SameLine();
		if ( ImGui::SmallButton( "Fit" ) ) S.bFitRequested = true;
		ImGui::SameLine();
		if ( ImGui::SmallButton( "1:1" ) )
		{
			S.Zoom = 1.0f;
			S.bFitRequested = false;
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 90.0f );
		ImGui::Combo( "##Background", &S.Background, "Checker\0Black\0White\0Magenta\0" );
		ImGui::Checkbox( "Grid", &S.bShowGridLines );
		ImGui::SameLine();
		ImGui::Checkbox( "Numbers", &S.bShowIndices );
		ImGui::SameLine();
		ImGui::Checkbox( "Pivots", &S.bShowPivots );
		ImGui::SameLine();
		ImGui::Checkbox( "Sharp", &S.bNearest );

		const ImVec2 Origin = ImGui::GetCursorScreenPos();
		ImVec2 Size = ImGui::GetContentRegionAvail();
		if ( Size.x < 50.0f || Size.y < 50.0f ) return;

		ImGui::InvisibleButton( "##Canvas", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight );
		const bool bHovered = ImGui::IsItemHovered();
		const bool bActive = ImGui::IsItemActive();
		ImGuiIO& IO = ImGui::GetIO();
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImVec2 Max( Origin.x + Size.x, Origin.y + Size.y );
		DrawList->PushClipRect( Origin, Max, true );

		switch ( S.Background )
		{
		case 0: NDebugUI::DrawChecker( DrawList, Origin, Max ); break;
		case 1: DrawList->AddRectFilled( Origin, Max, IM_COL32( 0, 0, 0, 255 ) ); break;
		case 2: DrawList->AddRectFilled( Origin, Max, IM_COL32( 255, 255, 255, 255 ) ); break;
		default: DrawList->AddRectFilled( Origin, Max, IM_COL32( 255, 0, 255, 255 ) ); break;
		}

		if ( !Texture.IsValid() || !Texture->IsReady() || TexW <= 0 || TexH <= 0 )
		{
			DrawList->AddText( ImVec2( Origin.x + 16.0f, Origin.y + 16.0f ), IM_COL32( 230, 230, 230, 255 ),
							   Definition.TexturePath.IsEmpty() ? "No texture. Use File > New from texture, or File > Open." : "The texture could not be loaded (see the Output Log)." );
			DrawList->PopClipRect();
			return;
		}

		if ( S.bFitRequested )
		{
			S.Zoom = NMath::Clamp( NMath::Min( ( Size.x - 16.0f ) / static_cast<float>( TexW ), ( Size.y - 28.0f ) / static_cast<float>( TexH ) ), 0.25f, 32.0f );
			if ( S.Zoom >= 2.0f ) S.Zoom = NMath::Floor( S.Zoom );   // 2 倍以上の拡大は整数倍にして、ドットを揃える
			S.Pan = ImVec2( ( Size.x - static_cast<float>( TexW ) * S.Zoom ) * 0.5f, ( Size.y - static_cast<float>( TexH ) * S.Zoom ) * 0.5f );
			S.bFitRequested = false;
		}

		// 拡大（マウスの下の点が動かないように）と移動
		if ( bHovered && IO.MouseWheel != 0.0f )
		{
			const ImVec2 Mouse( IO.MousePos.x - Origin.x, IO.MousePos.y - Origin.y );
			const float Before = S.Zoom;
			S.Zoom = NMath::Clamp( S.Zoom * ( IO.MouseWheel > 0.0f ? 1.15f : 1.0f / 1.15f ), 0.25f, 32.0f );
			S.Pan.x = Mouse.x - ( Mouse.x - S.Pan.x ) * ( S.Zoom / Before );
			S.Pan.y = Mouse.y - ( Mouse.y - S.Pan.y ) * ( S.Zoom / Before );
		}
		const bool bPanning = bActive && ( ImGui::IsMouseDown( ImGuiMouseButton_Middle ) || ImGui::IsMouseDown( ImGuiMouseButton_Right ) || ( ImGui::IsKeyDown( ImGuiKey_Space ) && ImGui::IsMouseDown( ImGuiMouseButton_Left ) ) );
		if ( bPanning )
		{
			S.Pan.x += IO.MouseDelta.x;
			S.Pan.y += IO.MouseDelta.y;
		}

		const ImVec2 ImageMin( Origin.x + S.Pan.x, Origin.y + S.Pan.y );
		const ImVec2 ImageMax( ImageMin.x + static_cast<float>( TexW ) * S.Zoom, ImageMin.y + static_cast<float>( TexH ) * S.Zoom );
		if ( S.bNearest ) NDebugUI::BeginNearest( DrawList );
		DrawList->AddImage( NDebugUI::GetImGuiTexture( Texture->GetHandle() ), ImageMin, ImageMax );
		if ( S.bNearest ) NDebugUI::EndNearest( DrawList );
		DrawList->AddRect( ImageMin, ImageMax, IM_COL32( 255, 255, 255, 90 ) );

		// 枠
		TArray<FOverlayFrame> Overlay;
		BuildOverlay( Definition, TexW, TexH, Overlay );
		const ImVec2 Mouse( ( IO.MousePos.x - ImageMin.x ) / S.Zoom, ( IO.MousePos.y - ImageMin.y ) / S.Zoom );
		int32 HoverIndex = INDEX_NONE;
		float HoverArea = 1.0e30f;
		for ( const FOverlayFrame& Item : Overlay )
		{
			const FSpriteFrame& F = Item.Frame;
			if ( Mouse.x >= F.X && Mouse.x < F.X + F.Width && Mouse.y >= F.Y && Mouse.y < F.Y + F.Height && bHovered )
			{
				const float Area = static_cast<float>( F.Width ) * static_cast<float>( F.Height );
				if ( Area < HoverArea )
				{
					HoverArea = Area;
					HoverIndex = Item.Index;
				}
			}
		}

		const bool bShowLabels = Overlay.Num() < 4000;
		for ( int32 Pass = 0; Pass < 2; ++Pass )   // 選択中は最後（上）に描く
		{
			for ( const FOverlayFrame& Item : Overlay )
			{
				const bool bSelected = Item.Index == S.SelectedFrame;
				if ( ( Pass == 1 ) != bSelected ) continue;
				const FSpriteFrame& F = Item.Frame;
				const ImVec2 P0( ImageMin.x + static_cast<float>( F.X ) * S.Zoom, ImageMin.y + static_cast<float>( F.Y ) * S.Zoom );
				const ImVec2 P1( P0.x + static_cast<float>( NMath::Max( F.Width, 1 ) ) * S.Zoom, P0.y + static_cast<float>( NMath::Max( F.Height, 1 ) ) * S.Zoom );

				if ( bSelected )
				{
					DrawList->AddRectFilled( P0, P1, IM_COL32( 255, 160, 40, 55 ) );
					DrawList->AddRect( P0, P1, IM_COL32( 255, 160, 40, 255 ), 0.0f, 0, 2.0f );
				}
				else if ( Item.bOutside )
				{
					DrawList->AddRectFilled( P0, P1, IM_COL32( 255, 40, 40, 60 ) );
					DrawList->AddRect( P0, P1, IM_COL32( 255, 60, 60, 255 ) );
					DrawList->AddLine( P0, P1, IM_COL32( 255, 60, 60, 255 ) );
					DrawList->AddLine( ImVec2( P0.x, P1.y ), ImVec2( P1.x, P0.y ), IM_COL32( 255, 60, 60, 255 ) );
				}
				else if ( S.bShowGridLines || Item.bFree )
				{
					DrawList->AddRect( P0, P1, Item.bFree ? IM_COL32( 255, 235, 60, 220 ) : GridColor( Item.GridIndex, 200 ) );
				}
				if ( Item.Index == HoverIndex && !bSelected ) DrawList->AddRect( P0, P1, IM_COL32( 255, 255, 255, 255 ), 0.0f, 0, 2.0f );

				if ( S.bShowIndices && bShowLabels && ( P1.x - P0.x ) >= 22.0f && ( P1.y - P0.y ) >= 14.0f )
				{
					char Label[16];
					NCString::Format( Label, sizeof( Label ), "%d", Item.Index );
					DrawList->AddText( ImVec2( P0.x + 3.0f, P0.y + 2.0f ), IM_COL32( 0, 0, 0, 255 ), Label );
					DrawList->AddText( ImVec2( P0.x + 2.0f, P0.y + 1.0f ), IM_COL32( 255, 255, 255, 255 ), Label );
				}
				if ( S.bShowPivots && !Item.bOutside && ( P1.x - P0.x ) >= 12.0f )
				{
					const ImVec2 C( P0.x + F.PivotX * S.Zoom, P0.y + F.PivotY * S.Zoom );
					DrawList->AddLine( ImVec2( C.x - 4.0f, C.y ), ImVec2( C.x + 5.0f, C.y ), IM_COL32( 255, 60, 60, 255 ) );
					DrawList->AddLine( ImVec2( C.x, C.y - 4.0f ), ImVec2( C.x, C.y + 5.0f ), IM_COL32( 255, 60, 60, 255 ) );
				}
			}
		}

		// Shift + ドラッグで、自由な矩形を作る
		const bool bShift = IO.KeyShift;
		if ( bHovered && bShift && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
		{
			S.bDrawing = true;
			S.DrawStart = ImVec2( NMath::Floor( Mouse.x ), NMath::Floor( Mouse.y ) );
		}
		if ( S.bDrawing )
		{
			const ImVec2 End( NMath::Floor( Mouse.x ), NMath::Floor( Mouse.y ) );
			const int32 X0 = NMath::Clamp( static_cast<int32>( NMath::Min( S.DrawStart.x, End.x ) ), 0, TexW );
			const int32 Y0 = NMath::Clamp( static_cast<int32>( NMath::Min( S.DrawStart.y, End.y ) ), 0, TexH );
			const int32 X1 = NMath::Clamp( static_cast<int32>( NMath::Max( S.DrawStart.x, End.x ) ) + 1, 0, TexW );
			const int32 Y1 = NMath::Clamp( static_cast<int32>( NMath::Max( S.DrawStart.y, End.y ) ) + 1, 0, TexH );
			const ImVec2 P0( ImageMin.x + static_cast<float>( X0 ) * S.Zoom, ImageMin.y + static_cast<float>( Y0 ) * S.Zoom );
			const ImVec2 P1( ImageMin.x + static_cast<float>( X1 ) * S.Zoom, ImageMin.y + static_cast<float>( Y1 ) * S.Zoom );
			DrawList->AddRect( P0, P1, IM_COL32( 255, 235, 60, 255 ), 0.0f, 0, 2.0f );
			char Label[64];
			NCString::Format( Label, sizeof( Label ), "%d,%d  %dx%d", X0, Y0, X1 - X0, Y1 - Y0 );
			DrawList->AddText( ImVec2( P0.x, P0.y - 16.0f ), IM_COL32( 255, 235, 60, 255 ), Label );
			if ( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
			{
				S.bDrawing = false;
				if ( X1 - X0 >= 1 && Y1 - Y0 >= 1 )
				{
					FSpriteFrame Frame;
					Frame.X = X0;
					Frame.Y = Y0;
					Frame.Width = X1 - X0;
					Frame.Height = Y1 - Y0;
					Frame.PivotX = static_cast<float32>( Frame.Width ) * 0.5f;
					Frame.PivotY = static_cast<float32>( Frame.Height ) * 0.5f;
					S.SelectedFrame = NSpriteSheetEdit::AddFrame( S.Document, Frame );
				}
			}
		}
		else if ( bHovered && ImGui::IsMouseReleased( ImGuiMouseButton_Left ) && !bShift && !ImGui::IsKeyDown( ImGuiKey_Space ) && ImGui::GetMouseDragDelta( ImGuiMouseButton_Left ).x == 0.0f )
		{
			// クリック: 選択。Ctrl + クリックなら、選んでいるクリップの最後にそのコマを足す
			S.SelectedFrame = HoverIndex;
			if ( HoverIndex != INDEX_NONE && IO.KeyCtrl && Definition.Clips.IsValidIndex( S.SelectedClip ) )
			{
				NSpriteSheetEdit::AppendStep( S.Document, S.SelectedClip, HoverIndex );
				S.SelectedStep = Definition.Clips[S.SelectedClip].Frames.Num();
			}
		}

		// 下端の案内
		char Info[200];
		const int32 Px = static_cast<int32>( NMath::Floor( Mouse.x ) );
		const int32 Py = static_cast<int32>( NMath::Floor( Mouse.y ) );
		if ( bHovered && Px >= 0 && Py >= 0 && Px < TexW && Py < TexH )
		{
			NCString::Format( Info, sizeof( Info ), "pixel (%d, %d)   frame %s   |   click: select   Ctrl+click: add to clip   Shift+drag: new frame   wheel: zoom   middle-drag: pan", Px, Py,
						   HoverIndex == INDEX_NONE ? "-" : FString::FromInt( HoverIndex ).c_str() );
		}
		else
		{
			NCString::Format( Info, sizeof( Info ), "%d x %d px   %d frame(s)   |   click: select   Ctrl+click: add to clip   Shift+drag: new frame   wheel: zoom   middle-drag: pan", TexW, TexH, Overlay.Num() );
		}
		DrawList->AddRectFilled( ImVec2( Origin.x, Max.y - 20.0f ), Max, IM_COL32( 0, 0, 0, 150 ) );
		DrawList->AddText( ImVec2( Origin.x + 6.0f, Max.y - 18.0f ), IM_COL32( 230, 230, 230, 255 ), Info );
		DrawList->PopClipRect();
	}
}
