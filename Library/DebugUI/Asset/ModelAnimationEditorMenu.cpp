#include "ModelAnimationEditorInternal.h"

namespace NModelAnimationEditorUI
{
	//=========================================================================
	// メニュー
	//=========================================================================
	void DrawMenuBar( FState& S, bool* pOpen )
	{
		if ( !ImGui::BeginMenuBar() )
		{
			return;
		}
		bool bOpenNewPopup = false, bOpenOpenPopup = false, bOpenSaveAsPopup = false;
		const FModelAnimationDefinition& Definition = S.Document.Get();
		const bool bCanSave = !Definition.ModelPath.IsEmpty() || !Definition.Clips.IsEmpty();

		if ( ImGui::BeginMenu( "File" ) )
		{
			if ( ImGui::MenuItem( "New from model..." ) )
			{
				bOpenNewPopup = true;
			}
			if ( ImGui::MenuItem( "New motion pack (no model)" ) )
			{
				RequestAction( S, FState::EPending::NewMotionPack, FString() );
			}
			if ( ImGui::MenuItem( "Open..." ) )
			{
				bOpenOpenPopup = true;
			}
			ImGui::Separator();
			if ( ImGui::MenuItem( "Save", "Ctrl+S", false, bCanSave ) )
			{
				if ( S.FilePath.IsEmpty() )
				{
					bOpenSaveAsPopup = true;
				}
				else
				{
					SaveCurrent( S, S.FilePath );
				}
			}
			if ( ImGui::MenuItem( "Save as...", nullptr, false, bCanSave ) )
			{
				bOpenSaveAsPopup = true;
			}
			if ( ImGui::MenuItem( "Export used models to .mv1", nullptr, false, bCanSave ) )
			{
				if ( CAssetSubsystem* Assets = GetAssets() )
				{
					const FModelBatchExportResult Batch = Assets->ExportModelsUsedBy( Definition, false );
					SetStatus( S, ".mv1 export: " + Batch.GetSummary(), Batch.NumFailed > 0 );
					RescanFiles( S, true );
				}
			}
			if ( ImGui::MenuItem( "Reload from disk", nullptr, false, !S.FilePath.IsEmpty() ) )
			{
				RequestAction( S, FState::EPending::Open, S.FilePath );
			}
			ImGui::Separator();
			if ( ImGui::MenuItem( "Close window" ) && pOpen != nullptr )
			{
				*pOpen = false;
			}
			ImGui::EndMenu();
		}
		if ( ImGui::BeginMenu( "Edit" ) )
		{
			if ( ImGui::MenuItem( "Undo", "Ctrl+Z", false, S.Document.CanUndo() ) )
			{
				S.Document.Undo();
			}
			if ( ImGui::MenuItem( "Redo", "Ctrl+Y", false, S.Document.CanRedo() ) )
			{
				S.Document.Redo();
			}
			ImGui::EndMenu();
		}
		ImGui::TextDisabled( "  %s%s", S.FilePath.IsEmpty() ? "(unsaved)" : UI_TEXT( S.FilePath ), S.Document.IsDirty() ? " *" : "" );
		ImGui::EndMenuBar();

		if ( bOpenNewPopup )
		{
			RescanFiles( S, true );
			ImGui::OpenPopup( "New model animation set" );
		}
		if ( bOpenOpenPopup )
		{
			RescanFiles( S, true );
			ImGui::OpenPopup( "Open model animation set" );
		}
		if ( bOpenSaveAsPopup )
		{
			if ( S.SaveAsPath.IsEmpty() )
			{
				FString Base = Definition.ModelPath;
				if ( Base.IsEmpty() && !Definition.Clips.IsEmpty() )
				{
					Base = Definition.Clips[0].SourcePath;
				}
				if ( Base.IsEmpty() )
				{
					Base = "Motions/MotionPack";
				}
				const int32 Dot = Base.FindLast( "." );
				if ( Dot > 0 )
				{
					Base = Base.Left( Dot );
				}
				S.SaveAsPath = Base + DefinitionExtension;
			}
			ImGui::OpenPopup( "Save model animation set as" );
		}
		if ( S.bRequestOpenUnsavedPopup )
		{
			ImGui::OpenPopup( "Unsaved changes" );
			S.bRequestOpenUnsavedPopup = false;
		}

		const ImVec2 PopupSize( 460.0f, 0.0f );
		ImGui::SetNextWindowSize( PopupSize );
		if ( ImGui::BeginPopupModal( "New model animation set", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::TextUnformatted( "Choose the model (or an animation-only file for a motion pack):" );
			NDebugUI::FileCombo( "##NewModel", S.NewModelSelection, S.ModelFiles, "(choose a model)" );
			ImGui::BeginDisabled( S.NewModelSelection.IsEmpty() );
			if ( ImGui::Button( "Create" ) )
			{
				RequestAction( S, FState::EPending::New, S.NewModelSelection );
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) )
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		ImGui::SetNextWindowSize( PopupSize );
		if ( ImGui::BeginPopupModal( "Open model animation set", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::TextUnformatted( "Choose a definition (*.gxmodelanim under the asset folder):" );
			NDebugUI::FileCombo( "##OpenDefinition", S.OpenSelection, S.DefinitionFiles, "(choose a file)" );
			ImGui::BeginDisabled( S.OpenSelection.IsEmpty() );
			if ( ImGui::Button( "Open" ) )
			{
				RequestAction( S, FState::EPending::Open, S.OpenSelection );
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) )
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		ImGui::SetNextWindowSize( PopupSize );
		if ( ImGui::BeginPopupModal( "Save model animation set as", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::TextUnformatted( "Path under the asset folder (the .gxmodelanim extension is added):" );
			NDebugUI::InputString( "##SaveAs", S.SaveAsPath );
			ImGui::Checkbox( "Also output referenced models as .mv1", &S.bExportMV1OnSave );
			if ( ImGui::Button( "Save" ) && !S.SaveAsPath.IsEmpty() )
			{
				if ( SaveCurrent( S, S.SaveAsPath ) )
				{
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) )
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		if ( ImGui::BeginPopupModal( "Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::TextUnformatted( "The model animation set has unsaved changes." );
			const auto Continue = [&]()
			{
				const FState::EPending Action = S.Pending;
				const FString Path = S.PendingPath;
				S.Pending = FState::EPending::None;
				if ( Action == FState::EPending::Open )
				{
					NDebugUI::OpenModelAnimationFile( S, Path );
				}
				else if ( Action == FState::EPending::New )
				{
					NDebugUI::NewModelAnimationFromModel( S, Path );
				}
				else if ( Action == FState::EPending::NewMotionPack )
				{
					NDebugUI::NewMotionPack( S, Path );
				}
				else if ( Action == FState::EPending::Close && pOpen != nullptr )
				{
					*pOpen = false;
				}
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
					SetStatus( S, "Use File > Save as... first (this set has no file yet).", true );
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
