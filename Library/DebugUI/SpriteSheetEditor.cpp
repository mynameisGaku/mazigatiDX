#include "SpriteSheetEditor.h"

#include <algorithm>
#include <cmath>

#include "Library/Core/Log/Log.h"
#include "Library/DebugUI/EditorCommon.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Platform/FileSystem.h"

DEFINE_LOG_CATEGORY_STATIC( LogSpriteSheetEditor, Log )

namespace
{
	using FState = FSpriteSheetEditorState;

	constexpr const char* SheetExtension = ".sheet.json";

	CAssetManager* GetAssets() { return GetAppSubsystem<CAssetManager>(); }

	void SetStatus( FState& S, const FString& Text, bool bError = false )
	{
		S.StatusMessage = Text;
		S.bStatusIsError = bError;
		if ( bError ) LOG( LogSpriteSheetEditor, Warning, "%s", *Text );
	}

	//=========================================================================
	// ファイルの一覧
	//=========================================================================
	void RescanFiles( FState& S, bool bForce )
	{
		CAssetManager* Assets = GetAssets();
		if ( Assets == nullptr ) return;
		const double Now = ImGui::GetTime();
		if ( !bForce && Now - S.LastScanTime < 3.0 ) return;
		S.LastScanTime = Now;

		const FString Root = Assets->GetRootDirectory();
		S.TextureFiles = NFileSystem::ListFilesRecursive( Root, { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".dds" } );
		S.SheetFiles = NFileSystem::ListFilesRecursive( Root, { SheetExtension } );
	}

	//=========================================================================
	// 編集中の定義と、プレビュー用のスプライトシート・アニメーターを揃える
	//=========================================================================
	int32 TextureWidth( const FState& S ) { return S.Sheet.IsValid() && S.Sheet->GetTexture().IsValid() ? S.Sheet->GetTexture()->GetWidth() : 0; }
	int32 TextureHeight( const FState& S ) { return S.Sheet.IsValid() && S.Sheet->GetTexture().IsValid() ? S.Sheet->GetTexture()->GetHeight() : 0; }

	void RestartPreview( FState& S, bool bKeepTime )
	{
		const float32 Normalized = bKeepTime ? S.Preview.GetNormalizedTime() : 0.0f;
		S.Preview.Sheet = S.Sheet;
		S.PreviewClip = S.SelectedClip;
		const FSpriteClip* Clip = S.Sheet.IsValid() ? S.Sheet->GetClip( S.SelectedClip ) : nullptr;
		if ( Clip != nullptr && !Clip->Frames.IsEmpty() )
		{
			S.Preview.Speed = S.PreviewSpeed;
			S.Preview.PlayIndex( S.SelectedClip );
			if ( bKeepTime ) S.Preview.Seek( Normalized );
			if ( !S.bPlaying ) S.Preview.Pause();
		}
		else
		{
			S.Preview.ShowFrame( S.SelectedFrame >= 0 ? S.SelectedFrame : 0 );
		}
	}

	void SyncSheet( FState& S )
	{
		CAssetManager* Assets = GetAssets();
		const FSpriteSheetDefinition& Definition = S.Document.Get();
		if ( Assets == nullptr ) return;

		if ( S.SheetTexturePath != Definition.TexturePath )
		{
			// 画像が変わった（または初めて）: 作り直す。読めなかったときも、同じパスで何度も試さない
			S.SheetTexturePath = Definition.TexturePath;
			S.Sheet = Definition.TexturePath.IsEmpty() ? TSharedPtr<CSpriteSheet>() : Assets->CreateSpriteSheet( Definition, "editor" );
			S.SheetRevision = S.Document.GetRevision();
			RestartPreview( S, false );
			return;
		}
		if ( S.SheetRevision != S.Document.GetRevision() && S.Sheet.IsValid() )
		{
			S.Sheet->SetDefinition( Definition );
			S.SheetRevision = S.Document.GetRevision();
			// 選択が範囲外になっていたら直す
			if ( !Definition.Clips.IsValidIndex( S.SelectedClip ) ) S.SelectedClip = Definition.Clips.IsEmpty() ? INDEX_NONE : Definition.Clips.Num() - 1;
			if ( !Definition.Grids.IsValidIndex( S.SelectedGrid ) ) S.SelectedGrid = Definition.Grids.IsEmpty() ? INDEX_NONE : Definition.Grids.Num() - 1;
			if ( S.SelectedFrame >= Definition.GetNumFrames() ) S.SelectedFrame = INDEX_NONE;
			const FSpriteClip* Clip = Definition.Clips.IsValidIndex( S.SelectedClip ) ? &Definition.Clips[S.SelectedClip] : nullptr;
			if ( Clip == nullptr || S.SelectedStep >= Clip->Frames.Num() ) S.SelectedStep = INDEX_NONE;
			if ( Clip == nullptr || S.SelectedEvent >= Clip->Events.Num() ) S.SelectedEvent = INDEX_NONE;
			RestartPreview( S, true );
		}
		if ( S.PreviewClip != S.SelectedClip ) RestartPreview( S, false );
	}

	//=========================================================================
	// 開く・保存
	//=========================================================================
	FString NormalizePath( const FString& Path ) { return Path.Replace( "\\", "/" ); }

	void ResetSelection( FState& S )
	{
		S.SelectedGrid = S.Document.Get().Grids.IsEmpty() ? INDEX_NONE : 0;
		S.SelectedFrame = INDEX_NONE;
		S.SelectedClip = S.Document.Get().Clips.IsEmpty() ? INDEX_NONE : 0;
		S.SelectedStep = INDEX_NONE;
		S.SelectedEvent = INDEX_NONE;
		S.SheetTexturePath = "?";   // 必ず作り直す
		S.bFitRequested = true;
	}

	bool SaveCurrent( FState& S, const FString& AssetPath )
	{
		CAssetManager* Assets = GetAssets();
		if ( Assets == nullptr || AssetPath.IsEmpty() ) return false;

		FString Path = NormalizePath( AssetPath );
		if ( !Path.EndsWith( SheetExtension, ESearchCase::IgnoreCase ) ) Path += SheetExtension;
		if ( !Assets->SaveSpriteSheetDefinition( Path, S.Document.Get() ) )
		{
			SetStatus( S, "Failed to save '" + Path + "' (see the log).", true );
			return false;
		}
		S.FilePath = Path;
		S.Document.MarkSaved();
		SetStatus( S, "Saved " + Path );
		RescanFiles( S, true );
		return true;
	}

	void RequestAction( FState& S, FState::EPending Action, const FString& Path )
	{
		if ( S.Document.IsDirty() )
		{
			S.Pending = Action;
			S.PendingPath = Path;
			S.bRequestOpenUnsavedPopup = true;
			return;
		}
		if ( Action == FState::EPending::Open ) NDebugUI::OpenSpriteSheetFile( S, Path );
		else if ( Action == FState::EPending::New ) NDebugUI::NewSpriteSheetFromTexture( S, Path );
	}

	//=========================================================================
	// 画像の上に重ねる枠
	//=========================================================================
	struct FOverlayFrame
	{
		FSpriteFrame Frame;
		int32 Index = 0;
		int32 GridIndex = INDEX_NONE;   // 格子のコマなら、その格子の番号
		bool bFree = false;
		bool bOutside = false;
	};

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
			S.Zoom = std::clamp( std::min( ( Size.x - 16.0f ) / static_cast<float>( TexW ), ( Size.y - 28.0f ) / static_cast<float>( TexH ) ), 0.25f, 32.0f );
			if ( S.Zoom >= 2.0f ) S.Zoom = std::floor( S.Zoom );   // 2 倍以上の拡大は整数倍にして、ドットを揃える
			S.Pan = ImVec2( ( Size.x - static_cast<float>( TexW ) * S.Zoom ) * 0.5f, ( Size.y - static_cast<float>( TexH ) * S.Zoom ) * 0.5f );
			S.bFitRequested = false;
		}

		// 拡大（マウスの下の点が動かないように）と移動
		if ( bHovered && IO.MouseWheel != 0.0f )
		{
			const ImVec2 Mouse( IO.MousePos.x - Origin.x, IO.MousePos.y - Origin.y );
			const float Before = S.Zoom;
			S.Zoom = std::clamp( S.Zoom * ( IO.MouseWheel > 0.0f ? 1.15f : 1.0f / 1.15f ), 0.25f, 32.0f );
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
				const ImVec2 P1( P0.x + static_cast<float>( std::max( F.Width, 1 ) ) * S.Zoom, P0.y + static_cast<float>( std::max( F.Height, 1 ) ) * S.Zoom );

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
					std::snprintf( Label, sizeof( Label ), "%d", Item.Index );
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
			S.DrawStart = ImVec2( std::floor( Mouse.x ), std::floor( Mouse.y ) );
		}
		if ( S.bDrawing )
		{
			const ImVec2 End( std::floor( Mouse.x ), std::floor( Mouse.y ) );
			const int32 X0 = std::clamp( static_cast<int32>( std::min( S.DrawStart.x, End.x ) ), 0, TexW );
			const int32 Y0 = std::clamp( static_cast<int32>( std::min( S.DrawStart.y, End.y ) ), 0, TexH );
			const int32 X1 = std::clamp( static_cast<int32>( std::max( S.DrawStart.x, End.x ) ) + 1, 0, TexW );
			const int32 Y1 = std::clamp( static_cast<int32>( std::max( S.DrawStart.y, End.y ) ) + 1, 0, TexH );
			const ImVec2 P0( ImageMin.x + static_cast<float>( X0 ) * S.Zoom, ImageMin.y + static_cast<float>( Y0 ) * S.Zoom );
			const ImVec2 P1( ImageMin.x + static_cast<float>( X1 ) * S.Zoom, ImageMin.y + static_cast<float>( Y1 ) * S.Zoom );
			DrawList->AddRect( P0, P1, IM_COL32( 255, 235, 60, 255 ), 0.0f, 0, 2.0f );
			char Label[64];
			std::snprintf( Label, sizeof( Label ), "%d,%d  %dx%d", X0, Y0, X1 - X0, Y1 - Y0 );
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
		const int32 Px = static_cast<int32>( std::floor( Mouse.x ) );
		const int32 Py = static_cast<int32>( std::floor( Mouse.y ) );
		if ( bHovered && Px >= 0 && Py >= 0 && Px < TexW && Py < TexH )
		{
			std::snprintf( Info, sizeof( Info ), "pixel (%d, %d)   frame %s   |   click: select   Ctrl+click: add to clip   Shift+drag: new frame   wheel: zoom   middle-drag: pan", Px, Py,
						   HoverIndex == INDEX_NONE ? "-" : FString::FromInt( HoverIndex ).c_str() );
		}
		else
		{
			std::snprintf( Info, sizeof( Info ), "%d x %d px   %d frame(s)   |   click: select   Ctrl+click: add to clip   Shift+drag: new frame   wheel: zoom   middle-drag: pan", TexW, TexH, Overlay.Num() );
		}
		DrawList->AddRectFilled( ImVec2( Origin.x, Max.y - 20.0f ), Max, IM_COL32( 0, 0, 0, 150 ) );
		DrawList->AddText( ImVec2( Origin.x + 6.0f, Max.y - 18.0f ), IM_COL32( 230, 230, 230, 255 ), Info );
		DrawList->PopClipRect();
	}

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
			S.SelectedGrid = std::min( S.SelectedGrid, S.Document.Get().Grids.Num() - 1 );
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
			std::snprintf( Label, sizeof( Label ), "Grid %d:  %d x %d,  frames %d..%d###grid%d", i, Grid.Columns, Grid.Rows, Start, Start + Grid.GetNumFrames() - 1, i );
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
		bChanged |= ImGui::DragInt( "Frames used", &Grid.NumFrames, 0.1f, 0, std::max( Grid.Columns * Grid.Rows, 1 ), Grid.NumFrames == 0 ? "all" : "%d" );
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
			Grid.Columns = std::max( Grid.Columns, 1 );
			Grid.Rows = std::max( Grid.Rows, 1 );
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
				std::snprintf( Label, sizeof( Label ), "%3d  (%d,%d) %dx%d  %s%s###frame%d", Item.Index, Item.Frame.X, Item.Frame.Y, Item.Frame.Width, Item.Frame.Height,
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
			S.SelectedClip = std::min( S.SelectedClip, S.Document.Get().Clips.Num() - 1 );
			S.SelectedStep = INDEX_NONE;
			S.SelectedEvent = INDEX_NONE;
		}
		ImGui::EndDisabled();

		ImGui::BeginChild( "##ClipList", ImVec2( 0.0f, 110.0f ), ImGuiChildFlags_Borders );
		for ( int32 i = 0; i < Definition.Clips.Num(); ++i )
		{
			const FSpriteClip& Clip = Definition.Clips[i];
			char Label[160];
			std::snprintf( Label, sizeof( Label ), "%s   (%d frame(s), %.2f s, %s)###clip%d", *Clip.Name.ToUtf8(), Clip.Frames.Num(), Clip.GetTotalDuration(), ToString( Clip.Loop ), i );
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
		ImGui::DragInt( "##First", &S.RangeFirst, 0.1f, 0, std::max( Definition.GetNumFrames() - 1, 0 ) );
		ImGui::SameLine();
		ImGui::TextUnformatted( "to" );
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 56.0f );
		ImGui::DragInt( "##Last", &S.RangeLast, 0.1f, 0, std::max( Definition.GetNumFrames() - 1, 0 ) );
		ImGui::SameLine();
		if ( ImGui::Button( "Set range" ) )
		{
			NSpriteSheetEdit::SetStepRange( S.Document, ClipIndex, std::clamp( S.RangeFirst, 0, std::max( Definition.GetNumFrames() - 1, 0 ) ),
											std::clamp( S.RangeLast, 0, std::max( Definition.GetNumFrames() - 1, 0 ) ) );
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
			std::snprintf( Label, sizeof( Label ), "%2d", Step );
			if ( ImGui::Selectable( Label, bSelected, 0, ImVec2( 26.0f, 24.0f ) ) )
			{
				S.SelectedStep = Step;
				if ( Clip.Frames[Step] >= 0 ) S.SelectedFrame = Clip.Frames[Step];
			}
			ImGui::SameLine();
			const FSpriteFrame* Frame = S.Sheet.IsValid() ? S.Sheet->GetFrame( Clip.Frames[Step] ) : nullptr;
			if ( Frame != nullptr && Frame->Width > 0 )
			{
				const float Scale = std::min( 24.0f / static_cast<float>( Frame->Width ), 24.0f / static_cast<float>( Frame->Height ) );
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
			if ( ImGui::DragInt( "##Frame", &FrameNumber, 0.1f, 0, std::max( Definition.GetNumFrames() - 1, 0 ) ) ) NSpriteSheetEdit::SetStepFrame( S.Document, ClipIndex, Step, FrameNumber );
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
			S.SelectedStep = std::min( S.SelectedStep, S.Document.Get().Clips[ClipIndex].Frames.Num() - 1 );
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
			if ( ImGui::DragInt( "##Step", &Step, 0.1f, 0, std::max( Current.Frames.Num() - 1, 0 ), "step %d" ) )
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
			const float Above = std::max( Frame->PivotY, 1.0f ), Below = std::max( static_cast<float>( Frame->Height ) - Frame->PivotY, 1.0f );
			const float Side = std::max( std::max( Frame->PivotX, static_cast<float>( Frame->Width ) - Frame->PivotX ), 1.0f );
			const float Fit = std::min( std::min( ( Center.y - Origin.y - 6.0f ) / Above, ( Max.y - Center.y - 6.0f ) / Below ), ( Size.x * 0.5f - 6.0f ) / Side );
			const float Scale = std::max( std::min( S.PreviewScale, Fit ), 0.5f );
			const float Left = S.bPreviewFlipX ? Center.x - ( static_cast<float>( Frame->Width ) - Frame->PivotX ) * Scale : Center.x - Frame->PivotX * Scale;
			const ImVec2 P0( Left, Center.y - Frame->PivotY * Scale );
			const ImVec2 P1( P0.x + static_cast<float>( Frame->Width ) * Scale, P0.y + static_cast<float>( Frame->Height ) * Scale );
			ImVec2 Uv0( static_cast<float>( Frame->X ) / static_cast<float>( TexW ), static_cast<float>( Frame->Y ) / static_cast<float>( TexH ) );
			ImVec2 Uv1( static_cast<float>( Frame->X + Frame->Width ) / static_cast<float>( TexW ), static_cast<float>( Frame->Y + Frame->Height ) / static_cast<float>( TexH ) );
			if ( S.bPreviewFlipX ) std::swap( Uv0.x, Uv1.x );
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
			DrawList->AddText( ImVec2( Max.x - 150.0f, Y ), IM_COL32( 255, 220, 80, static_cast<int>( 255.0f * std::min( Entry.Value * 2.0f, 1.0f ) ) ), UI_TEXT( "event: " + Entry.Key ) );
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
		const float32 Total = std::max( Clip.GetTotalDuration(), 0.001f );
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
			std::snprintf( Label, sizeof( Label ), "%.2f", T );
			DrawList->AddText( ImVec2( X + 2.0f, Origin.y ), IM_COL32( 160, 160, 170, 255 ), Label );
		}

		// コマのブロック
		const float BlockTop = Origin.y + RulerHeight + EventHeight;
		float Cumulative = 0.0f;
		float StepStartX[256] = {};
		const int32 NumDrawn = std::min( Clip.Frames.Num(), 256 );
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
				const float Scale = std::min( ( X1 - X0 - 6.0f ) / static_cast<float>( Frame->Width ), 34.0f / static_cast<float>( Frame->Height ) );
				const ImVec2 P0( X0 + 3.0f, BlockTop + 3.0f );
				const ImVec2 P1( P0.x + static_cast<float>( Frame->Width ) * Scale, P0.y + static_cast<float>( Frame->Height ) * Scale );
				NDebugUI::BeginNearest( DrawList );
				DrawList->AddImage( TextureId, P0, P1, ImVec2( static_cast<float>( Frame->X ) / static_cast<float>( TexW ), static_cast<float>( Frame->Y ) / static_cast<float>( TexH ) ),
									ImVec2( static_cast<float>( Frame->X + Frame->Width ) / static_cast<float>( TexW ), static_cast<float>( Frame->Y + Frame->Height ) / static_cast<float>( TexH ) ) );
				NDebugUI::EndNearest( DrawList );
			}
			char Label[32];
			std::snprintf( Label, sizeof( Label ), "#%d", Clip.Frames[i] );
			DrawList->AddText( ImVec2( X0 + 3.0f, BlockTop + BlockHeight - 16.0f ), IM_COL32( 230, 230, 230, 255 ), Label );

			// ブロックの右端をドラッグして、表示時間を変える
			ImGui::SetCursorScreenPos( ImVec2( X1 - 5.0f, BlockTop ) );
			ImGui::PushID( 5000 + i );
			ImGui::InvisibleButton( "##Resize", ImVec2( 8.0f, BlockHeight ) );
			if ( ImGui::IsItemHovered() || ImGui::IsItemActive() ) ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
			if ( ImGui::IsItemActive() && IO.MouseDelta.x != 0.0f )
			{
				const float32 NewDuration = std::max( Duration + IO.MouseDelta.x / PixelsPerSecond, 0.005f );
				const FString Key = FString::Printf( "tldur%d", i );
				NSpriteSheetEdit::SetStepDuration( S.Document, ClipIndex, i, NewDuration, *Key );
				ResizingStep = i;
			}
			ImGui::PopID();
			// ブロックそのものをクリックして選ぶ
			ImGui::SetCursorScreenPos( ImVec2( X0, BlockTop ) );
			ImGui::PushID( 6000 + i );
			ImGui::InvisibleButton( "##Block", ImVec2( std::max( X1 - X0 - 6.0f, 4.0f ), BlockHeight ) );
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
					const float D = std::fabs( IO.MousePos.x - StepStartX[s] );
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
			S.Preview.Seek( std::clamp( ( IO.MousePos.x - Origin.x - 4.0f ) / PixelsPerSecond / Total, 0.0f, 1.0f ) );
		}
		(void)bAreaHovered;
		ImGui::SetCursorScreenPos( ImVec2( Origin.x, Origin.y + TotalHeight + 4.0f ) );
		ImGui::PushTextWrapPos( 0.0f );
		ImGui::TextDisabled( "Drag a block's right edge: change its time.  Drag a flag: move the event.  Right-click a flag: delete.  Drag the ruler: scrub." );
		ImGui::PopTextWrapPos();
	}

	//=========================================================================
	// メニュー・ダイアログ
	//=========================================================================
	void DrawMenuBar( FState& S, bool* pOpen )
	{
		if ( !ImGui::BeginMenuBar() ) return;
		bool bOpenNewPopup = false, bOpenOpenPopup = false, bOpenSaveAsPopup = false;

		if ( ImGui::BeginMenu( "File" ) )
		{
			if ( ImGui::MenuItem( "New from texture..." ) ) bOpenNewPopup = true;
			if ( ImGui::MenuItem( "Open..." ) ) bOpenOpenPopup = true;
			ImGui::Separator();
			if ( ImGui::MenuItem( "Save", "Ctrl+S", false, !S.Document.Get().TexturePath.IsEmpty() ) )
			{
				if ( S.FilePath.IsEmpty() ) bOpenSaveAsPopup = true;
				else SaveCurrent( S, S.FilePath );
			}
			if ( ImGui::MenuItem( "Save as...", nullptr, false, !S.Document.Get().TexturePath.IsEmpty() ) ) bOpenSaveAsPopup = true;
			if ( ImGui::MenuItem( "Reload from disk", nullptr, false, !S.FilePath.IsEmpty() ) ) RequestAction( S, FState::EPending::Open, S.FilePath );
			ImGui::Separator();
			if ( ImGui::MenuItem( "Close window" ) && pOpen != nullptr ) *pOpen = false;
			ImGui::EndMenu();
		}
		if ( ImGui::BeginMenu( "Edit" ) )
		{
			if ( ImGui::MenuItem( "Undo", "Ctrl+Z", false, S.Document.CanUndo() ) ) S.Document.Undo();
			if ( ImGui::MenuItem( "Redo", "Ctrl+Y", false, S.Document.CanRedo() ) ) S.Document.Redo();
			ImGui::EndMenu();
		}
		ImGui::TextDisabled( "  %s%s", S.FilePath.IsEmpty() ? "(unsaved)" : UI_TEXT( S.FilePath ), S.Document.IsDirty() ? " *" : "" );
		ImGui::EndMenuBar();

		if ( bOpenNewPopup )
		{
			RescanFiles( S, true );
			ImGui::OpenPopup( "New sprite sheet" );
		}
		if ( bOpenOpenPopup )
		{
			RescanFiles( S, true );
			ImGui::OpenPopup( "Open sprite sheet" );
		}
		if ( bOpenSaveAsPopup )
		{
			if ( S.SaveAsPath.IsEmpty() )
			{
				FString Base = S.Document.Get().TexturePath;
				const int32 Dot = Base.FindLast( "." );
				if ( Dot > 0 ) Base = Base.Left( Dot );
				S.SaveAsPath = Base + SheetExtension;
			}
			ImGui::OpenPopup( "Save sprite sheet as" );
		}
		if ( S.bRequestOpenUnsavedPopup )
		{
			ImGui::OpenPopup( "Unsaved changes" );
			S.bRequestOpenUnsavedPopup = false;
		}

		const ImVec2 PopupSize( 460.0f, 0.0f );
		ImGui::SetNextWindowSize( PopupSize );
		if ( ImGui::BeginPopupModal( "New sprite sheet", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::TextUnformatted( "Choose the texture (an image under the asset folder):" );
			NDebugUI::FileCombo( "##NewTexture", S.NewTextureSelection, S.TextureFiles, "(choose a texture)" );
			ImGui::BeginDisabled( S.NewTextureSelection.IsEmpty() );
			if ( ImGui::Button( "Create" ) )
			{
				RequestAction( S, FState::EPending::New, S.NewTextureSelection );
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) ) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::SetNextWindowSize( PopupSize );
		if ( ImGui::BeginPopupModal( "Open sprite sheet", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::TextUnformatted( "Choose a definition (*.sheet.json under the asset folder):" );
			NDebugUI::FileCombo( "##OpenSheet", S.OpenSelection, S.SheetFiles, "(choose a file)" );
			ImGui::BeginDisabled( S.OpenSelection.IsEmpty() );
			if ( ImGui::Button( "Open" ) )
			{
				RequestAction( S, FState::EPending::Open, S.OpenSelection );
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) ) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::SetNextWindowSize( PopupSize );
		if ( ImGui::BeginPopupModal( "Save sprite sheet as", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::TextUnformatted( "Path under the asset folder (the .sheet.json extension is added):" );
			NDebugUI::InputString( "##SaveAs", S.SaveAsPath );
			if ( ImGui::Button( "Save" ) && !S.SaveAsPath.IsEmpty() )
			{
				if ( SaveCurrent( S, S.SaveAsPath ) ) ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) ) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if ( ImGui::BeginPopupModal( "Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::TextUnformatted( "The sprite sheet has unsaved changes." );
			const auto Continue = [&]()
			{
				const FState::EPending Action = S.Pending;
				const FString Path = S.PendingPath;
				S.Pending = FState::EPending::None;
				if ( Action == FState::EPending::Open ) NDebugUI::OpenSpriteSheetFile( S, Path );
				else if ( Action == FState::EPending::New ) NDebugUI::NewSpriteSheetFromTexture( S, Path );
				else if ( Action == FState::EPending::Close && pOpen != nullptr ) *pOpen = false;
			};
			if ( ImGui::Button( "Save and continue" ) )
			{
				if ( !S.FilePath.IsEmpty() && SaveCurrent( S, S.FilePath ) )
				{
					Continue();
					ImGui::CloseCurrentPopup();
				}
				else if ( S.FilePath.IsEmpty() )
				{
					SetStatus( S, "Use File > Save as... first (this sprite sheet has no file yet).", true );
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Discard changes" ) )
			{
				S.Document.MarkSaved();
				Continue();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) )
			{
				S.Pending = FState::EPending::None;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
}

//=============================================================================
// 公開
//=============================================================================
bool NDebugUI::OpenSpriteSheetFile( FSpriteSheetEditorState& State, const FString& AssetPath )
{
	CAssetManager* Assets = GetAssets();
	if ( Assets == nullptr ) return false;

	const TOptional<FString> Full = Assets->ResolvePath( AssetPath );
	if ( !Full.IsSet() )
	{
		SetStatus( State, "File not found: " + AssetPath, true );
		return false;
	}
	NJson::CJsonObject Json;
	FSpriteSheetDefinition Definition;
	if ( !Json.LoadFromFile( Full.Get( FString() ) ) || !FSpriteSheetDefinition::FromJson( Json.GetRoot(), Definition ) )
	{
		SetStatus( State, "Not a valid sprite sheet definition: " + AssetPath, true );
		return false;
	}

	State.Document.Reset( Definition );
	State.FilePath = NormalizePath( AssetPath );
	ResetSelection( State );
	SetStatus( State, "Opened " + State.FilePath );
	return true;
}

bool NDebugUI::NewSpriteSheetFromTexture( FSpriteSheetEditorState& State, const FString& TexturePath )
{
	FSpriteSheetDefinition Definition;
	Definition.TexturePath = NormalizePath( TexturePath );
	Definition.Grids.Add( FSpriteSheetGrid() );
	State.Document.Reset( Definition );
	State.FilePath.Empty();
	State.SaveAsPath.Empty();
	ResetSelection( State );
	SetStatus( State, "New sprite sheet for " + Definition.TexturePath + ". Set up the grid, add clips, then File > Save as." );
	return true;
}

void NDebugUI::DrawSpriteSheetEditor( FSpriteSheetEditorState& S, bool* pOpen )
{
	const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos( ImVec2( MainViewport->Pos.x + 16.0f, MainViewport->Pos.y + 38.0f ), ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( std::min( 1320.0f, MainViewport->Size.x - 32.0f ), std::min( 780.0f, MainViewport->Size.y - 50.0f ) ), ImGuiCond_FirstUseEver );
	char Title[256];
	std::snprintf( Title, sizeof( Title ), "Sprite Sheet Editor%s###SpriteSheetEditor", S.Document.IsDirty() ? " *" : "" );
	if ( !ImGui::Begin( Title, pOpen, ImGuiWindowFlags_MenuBar ) )
	{
		ImGui::End();
		return;
	}

	RescanFiles( S, false );
	SyncSheet( S );
	DrawMenuBar( S, pOpen );

	// ショートカット
	ImGuiIO& IO = ImGui::GetIO();
	if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) && !IO.WantTextInput )
	{
		if ( ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiKey_Z ) ) S.Document.Undo();
		if ( ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiKey_Y ) || ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z ) ) S.Document.Redo();
		if ( ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiKey_S ) && !S.FilePath.IsEmpty() ) SaveCurrent( S, S.FilePath );
	}

	const float FooterHeight = ImGui::GetFrameHeightWithSpacing() + 2.0f;
	const float LeftWidth = 360.0f;
	const float RightWidth = 430.0f;

	// 左
	ImGui::BeginChild( "##Left", ImVec2( LeftWidth, -FooterHeight ), ImGuiChildFlags_Borders );
	if ( ImGui::BeginTabBar( "##LeftTabs" ) )
	{
		const auto TabFlags = [&S]( int32 Index ) { return S.ForceLeftTab == Index ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
		if ( ImGui::BeginTabItem( "Source", nullptr, TabFlags( 0 ) ) ) { DrawSourceTab( S ); ImGui::EndTabItem(); }
		if ( ImGui::BeginTabItem( "Grid", nullptr, TabFlags( 1 ) ) ) { DrawGridTab( S ); ImGui::EndTabItem(); }
		if ( ImGui::BeginTabItem( "Frames", nullptr, TabFlags( 2 ) ) ) { DrawFramesTab( S ); ImGui::EndTabItem(); }
		if ( ImGui::BeginTabItem( "Clips", nullptr, TabFlags( 3 ) ) ) { DrawClipsTab( S ); ImGui::EndTabItem(); }
		ImGui::EndTabBar();
		S.ForceLeftTab = INDEX_NONE;
	}
	ImGui::EndChild();

	// 中央（画像）
	ImGui::SameLine();
	ImGui::BeginChild( "##Center", ImVec2( -RightWidth - ImGui::GetStyle().ItemSpacing.x, -FooterHeight ), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	DrawCanvas( S );
	ImGui::EndChild();

	// 右（プレビュー・タイムライン）
	ImGui::SameLine();
	ImGui::BeginChild( "##Right", ImVec2( 0.0f, -FooterHeight ), ImGuiChildFlags_Borders );
	DrawPreview( S, IO.DeltaTime );
	DrawTimeline( S );
	ImGui::EndChild();

	// 下
	if ( S.bStatusIsError ) ImGui::TextColored( ImVec4( 1.0f, 0.5f, 0.4f, 1.0f ), "%s", UI_TEXT( S.StatusMessage ) );
	else ImGui::TextDisabled( "%s", UI_TEXT( S.StatusMessage ) );

	// どの項目も操作されていなければ、連続した編集を区切る
	if ( !ImGui::IsAnyItemActive() ) S.Document.EndEditSession();
	ImGui::End();
}
