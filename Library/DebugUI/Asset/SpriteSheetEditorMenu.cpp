#include "SpriteSheetEditorInternal.h"

namespace NSpriteSheetEditorUI
{
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
			ImGui::TextUnformatted( "Choose a definition (*.gxsprite under the asset folder):" );
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
			ImGui::TextUnformatted( "Path under the asset folder (the .gxsprite extension is added):" );
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
