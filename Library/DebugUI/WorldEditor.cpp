#include "WorldEditor.h"

#include <algorithm>
#include <cmath>

#include "Library/Core/Log/Log.h"
#include "Library/DebugUI/EditorCommon.h"
#include "Library/DebugUI/WorldEditorInternal.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/World/Editor/EditorPicking.h"
#include "Library/Framework/World/WorldCollision.h"
#include "Library/Framework/World/WorldHierarchy.h"
#include "Library/Platform/FileSystem.h"
#include "VendorModules/ImGui/imgui_internal.h"   // NavMoveRequestCancel（Tab で、ImGui が入力欄へ移るのを止める）

DEFINE_LOG_CATEGORY_STATIC( LogWorldEditor, Log )

//=============================================================================
// 共通の補助
//=============================================================================
CAssetManager* NWorldEditorUI::GetAssets()
{
	return GetAppSubsystem<CAssetManager>();
}

void NWorldEditorUI::SetNextWindowRect( float32 X, float32 Y, float32 Width, float32 Height )
{
	const ImGuiViewport* Main = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos( ImVec2( Main->WorkPos.x + Main->WorkSize.x * X, Main->WorkPos.y + Main->WorkSize.y * Y ), ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( Main->WorkSize.x * Width, Main->WorkSize.y * Height ), ImGuiCond_FirstUseEver );
}

void NWorldEditorUI::SetStatus( FWorldEditorState& S, const FString& Text, bool bError )
{
	S.StatusMessage = Text;
	S.bStatusIsError = bError;
	if ( bError ) LOG( LogWorldEditor, Warning, "%s", *Text );
}

void NWorldEditorUI::RescanFiles( FWorldEditorState& S, bool bForce )
{
	CAssetManager* Assets = GetAssets();
	if ( Assets == nullptr ) return;
	const double Now = ImGui::GetTime();
	if ( !bForce && Now - S.LastScanTime < 3.0 ) return;
	S.LastScanTime = Now;

	const FString Root = Assets->GetRootDirectory();
	S.ModelFiles = NFileSystem::ListFilesRecursive( Root, NModelFormat::GetAllModelExtensions() );
	S.TextureFiles = NFileSystem::ListFilesRecursive( Root, { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".dds" } );
	S.SoundFiles = NFileSystem::ListFilesRecursive( Root, { ".wav", ".ogg", ".mp3" } );
	S.AnimationFiles = NFileSystem::ListFilesRecursive( Root, { ".modelanim.json" } );
	S.WorldFiles = NFileSystem::ListFilesRecursive( Root, { ".world" } );
	S.EffectFiles = NFileSystem::ListFilesRecursive( Root, { ".effect.json" } );
}

void NWorldEditorUI::RefreshIssues( FWorldEditorState& S )
{
	const uint32 Revision = S.Session.Document.GetRevision();
	if ( S.IssuesRevision == Revision ) return;
	S.IssuesRevision = Revision;
	S.Issues = NWorld::ValidateWorld( S.Session.Document.Get(), S.AssetInfo.Get(), S.Session.FilePath );
}

FEditorShapeContext NWorldEditorUI::MakeShapeContext( FWorldEditorState& S )
{
	FEditorShapeContext Context;
	Context.Assets = S.AssetInfo.Get();
	Context.Camera = &S.Session.Camera;
	Context.Viewport = S.View;
	return Context;
}

FVector3 NWorldEditorUI::GetViewCenterPlacement( FWorldEditorState& S )
{
	const FEditorRay Ray = S.Session.Camera.ScreenToRay( S.View, static_cast<float32>( S.View.Width ) * 0.5f, static_cast<float32>( S.View.Height ) * 0.5f );
	FEditorPlacementOptions Options;
	if ( S.Session.bSnap ) Options.SnapStep = S.Session.MoveSnap;
	return NWorldEditor::ComputePlacement( Ray, &S.Session.Document.Get(), MakeShapeContext( S ), Options ).Position;
}

void NWorldEditorUI::FrameAll( FWorldEditorState& S )
{
	CWorldEditorSession& Session = S.Session;
	TArray<int32> All;
	for ( const FWorldObject& Object : Session.Document.Get().Objects ) All.Add( Object.Id );
	FEditorShapeContext Context;   // カメラなし: 目印の大きさがカメラの距離で変わらない
	Context.Assets = S.AssetInfo.Get();
	FVector3 Center;
	float32 Radius = 0.0f;
	if ( NWorldEditor::ComputeBoundingSphere( Session.Document.Get(), All, Context, Center, Radius ) ) Session.Camera.Focus( Center, std::max( Radius, 2.0f ), S.View );
	else Session.Camera = FEditorCamera();
}

void NWorldEditorUI::CreateEmptyAtView( FWorldEditorState& S )
{
	S.Session.CreateEmpty( GetViewCenterPlacement( S ) );
}

void NWorldEditorUI::CreateColliderAtView( FWorldEditorState& S, EWorldColliderShape Shape, bool bTrigger )
{
	S.Session.CreateCollider( Shape, bTrigger, GetViewCenterPlacement( S ) );
}

void NWorldEditorUI::CreateAssetAtView( FWorldEditorState& S, const FString& AssetPath )
{
	if ( S.Session.CreateAsset( AssetPath, GetViewCenterPlacement( S ), S.AssetInfo.Get() ) == WorldNoId ) SetStatus( S, "Cannot place '" + AssetPath + "': not a model, image or sound file", true );
}

//=============================================================================
// ファイル
//=============================================================================
namespace
{
	FString NormalizePath( const FString& Path ) { return Path.Replace( "\\", "/" ); }

	FString EnsureWorldExtension( const FString& Path )
	{
		FString Result = NormalizePath( Path.TrimStartAndEnd() );
		if ( !Result.IsEmpty() && !Result.EndsWith( ".world", ESearchCase::IgnoreCase ) ) Result += ".world";
		return Result;
	}
}

bool NDebugUI::OpenWorldFile( FWorldEditorState& S, const FString& AssetPath )
{
	CAssetManager* Assets = NWorldEditorUI::GetAssets();
	const FString Path = NormalizePath( AssetPath );
	if ( Assets == nullptr )
	{
		NWorldEditorUI::SetStatus( S, "The asset manager is not running", true );
		return false;
	}
	const TSharedPtr<CWorldAsset> Asset = Assets->LoadWorld( Path );
	if ( !Asset.IsValid() )
	{
		NWorldEditorUI::SetStatus( S, "Cannot open '" + Path + "' (see the log)", true );
		return false;
	}
	S.Session.Reset( Asset->GetDefinition(), Path );
	S.EffectPreviews.Empty();   // 同じ Id と部品でも、前のワールドで生まれた粒子は引き継がない
	S.Collapsed.Empty();
	S.LastClickedId = WorldNoId;
	if ( S.AssetInfo.IsValid() ) S.AssetInfo->Clear();
	NWorldEditorUI::FrameAll( S );
	for ( const FString& Warning : Asset->GetLoadWarnings() ) LOG( LogWorldEditor, Warning, "%s: %s", *Path, *Warning );
	NWorldEditorUI::SetStatus( S, FString::Printf( "Opened %s (%d objects%s)", *Path, S.Session.Document.Get().Objects.Num(), Asset->GetLoadWarnings().IsEmpty() ? "" : ", repaired on load: see the log" ) );
	return true;
}

void NDebugUI::NewWorld( FWorldEditorState& S )
{
	S.Session.Reset( FWorldDefinition(), FString() );
	S.EffectPreviews.Empty();   // 新しい文書では、前の再生と終了後の待ち時間を捨てる
	S.Collapsed.Empty();
	S.LastClickedId = WorldNoId;
	S.Session.Camera = FEditorCamera();
	NWorldEditorUI::SetStatus( S, "New world" );
}

bool NDebugUI::SaveWorldFile( FWorldEditorState& S, const FString& AssetPath )
{
	CAssetManager* Assets = NWorldEditorUI::GetAssets();
	const FString Path = EnsureWorldExtension( AssetPath.IsEmpty() ? S.Session.FilePath : AssetPath );
	if ( Path.IsEmpty() || Assets == nullptr ) return false;
	if ( S.Session.IsGizmoDragging() ) S.Session.EndGizmoDrag();
	if ( !Assets->SaveWorld( Path, S.Session.Document.Get() ) )
	{
		NWorldEditorUI::SetStatus( S, "Cannot save '" + Path + "' (see the log)", true );
		return false;
	}
	S.Session.FilePath = Path;
	S.Session.Document.MarkSaved();
	NWorldEditorUI::RescanFiles( S, true );
	const bool bHasErrors = NWorld::HasErrors( NWorld::ValidateWorld( S.Session.Document.Get(), S.AssetInfo.Get(), S.Session.FilePath ) );
	NWorldEditorUI::SetStatus( S, "Saved " + Path + ( bHasErrors ? " (the world has errors: see World Issues)" : "" ), false );
	return true;
}

bool NWorldEditorUI::MakePrefabFromSelection( FWorldEditorState& S, const FString& Path )
{
	CWorldEditorSession& Session = S.Session;
	CAssetManager* Assets = GetAssets();
	const FString File = EnsureWorldExtension( Path );
	if ( File.IsEmpty() || Assets == nullptr || S.Session.GetSelection().IsEmpty() ) return false;
	if ( NWorld::NormalizeAssetPath( File ) == NWorld::NormalizeAssetPath( Session.FilePath ) )
	{
		SetStatus( S, "Choose another file: a world cannot place itself as a prefab", true );
		return false;
	}
	const int32 Primary = Session.GetPrimary() != WorldNoId ? Session.GetPrimary() : Session.GetSelection()[0];
	const FVector3 Origin = NWorld::ComputeWorldTransform( Session.Document.Get(), Primary ).Position;
	const FString Name = NWorldEditor::GetAssetDisplayName( File );
	const FWorldDefinition Contents = NWorldEdit::ExtractSubWorld( Session.Document.Get(), Session.GetSelection(), Origin, Name );
	if ( Contents.Objects.IsEmpty() )
	{
		SetStatus( S, "Nothing to put in a prefab", true );
		return false;
	}
	if ( !Assets->SaveWorld( File, Contents ) )
	{
		SetStatus( S, "Cannot save '" + File + "' (see the log)", true );
		return false;
	}
	if ( S.AssetInfo.IsValid() ) S.AssetInfo->Clear();   // 同じファイルの古い中身を、捨てる
	const int32 Id = Session.ReplaceSelectionWithPrefab( File, Origin, Name );
	RescanFiles( S, true );
	if ( Id == WorldNoId )
	{
		SetStatus( S, "Saved " + File + ", but the selection could not be replaced", true );
		return false;
	}
	SetStatus( S, FString::Printf( "Saved %d object(s) as %s and replaced them by an instance of it", Contents.Objects.Num(), *File ) );
	return true;
}

void NDebugUI::ReleaseWorldEditor( FWorldEditorState& S )
{
	NWorldEditorUI::ReleaseViewport( S );
	S.MeshViews.Empty();   // 頂点バッファを、画像と同じく、アセットの片付けの前に手放す
	S.EffectPreviews.Empty();   // 粒子の画像も同じ
	S.AssetInfo.Reset();
}

//=============================================================================
// 操作の予約（未保存の変更があれば、確認してから）
//=============================================================================
namespace
{
	void ExecutePending( FWorldEditorState& S )
	{
		const FWorldEditorState::EPending Action = S.Pending;
		const FString Path = S.PendingPath;
		S.Pending = FWorldEditorState::EPending::None;
		switch ( Action )
		{
		case FWorldEditorState::EPending::New: NDebugUI::NewWorld( S ); break;
		case FWorldEditorState::EPending::Open: NDebugUI::OpenWorldFile( S, Path ); break;
		case FWorldEditorState::EPending::Close: S.bCloseRequested = true; break;
		case FWorldEditorState::EPending::None: break;
		}
	}

	void RequestAction( FWorldEditorState& S, FWorldEditorState::EPending Action, const FString& Path = FString() )
	{
		S.Pending = Action;
		S.PendingPath = Path;
		if ( S.Session.Document.IsDirty() ) S.bRequestOpenUnsavedPopup = true;
		else ExecutePending( S );
	}

	void RequestSave( FWorldEditorState& S )
	{
		if ( S.Session.FilePath.IsEmpty() )
		{
			S.bRequestOpenSaveAsPopup = true;
			return;
		}
		NDebugUI::SaveWorldFile( S );
	}

	//=========================================================================
	// メニューとツールバー
	//=========================================================================
	void DrawMenuBar( FWorldEditorState& S )
	{
		CWorldEditorSession& Session = S.Session;
		if ( !ImGui::BeginMenuBar() ) return;

		if ( ImGui::BeginMenu( "File" ) )
		{
			if ( ImGui::MenuItem( "New", "Ctrl+N" ) ) RequestAction( S, FWorldEditorState::EPending::New );
			if ( ImGui::MenuItem( "Open...", "Ctrl+O" ) ) S.bRequestOpenOpenPopup = true;
			if ( ImGui::MenuItem( "Save", "Ctrl+S", false, NWorldEditorUI::GetAssets() != nullptr ) ) RequestSave( S );
			if ( ImGui::MenuItem( "Save As...", nullptr, false, NWorldEditorUI::GetAssets() != nullptr ) ) S.bRequestOpenSaveAsPopup = true;
			ImGui::Separator();
			if ( ImGui::MenuItem( "Close editor" ) ) RequestAction( S, FWorldEditorState::EPending::Close );
			ImGui::EndMenu();
		}
		if ( ImGui::BeginMenu( "Edit" ) )
		{
			if ( ImGui::MenuItem( "Undo", "Ctrl+Z", false, Session.Document.CanUndo() && !Session.IsGizmoDragging() ) ) Session.Undo();
			if ( ImGui::MenuItem( "Redo", "Ctrl+Y", false, Session.Document.CanRedo() && !Session.IsGizmoDragging() ) ) Session.Redo();
			ImGui::Separator();
			const bool bHasSelection = !Session.GetSelection().IsEmpty();
			if ( ImGui::MenuItem( "Duplicate", "Ctrl+D", false, bHasSelection ) ) Session.DuplicateSelection();
			if ( ImGui::MenuItem( "Copy", "Ctrl+C", false, bHasSelection ) ) Session.CopySelection();
			if ( ImGui::MenuItem( "Paste", "Ctrl+V", false, Session.CanPaste() ) ) Session.Paste();
			if ( ImGui::MenuItem( "Delete", "Del", false, bHasSelection ) ) Session.DeleteSelection();
			ImGui::Separator();
			if ( ImGui::MenuItem( "Select All", "Ctrl+A" ) ) Session.SelectAll();
			if ( ImGui::MenuItem( "Group", "Ctrl+G", false, bHasSelection ) ) Session.GroupSelection();
			if ( ImGui::MenuItem( "Ungroup", "Ctrl+Shift+G", false, bHasSelection ) ) Session.UngroupSelection();
			if ( ImGui::MenuItem( "Focus Selection", "F", false, bHasSelection ) ) Session.FocusSelection( S.View, S.AssetInfo.Get() );
			ImGui::Separator();
			if ( ImGui::MenuItem( "Make Prefab from Selection...", nullptr, false, bHasSelection && !Session.IsMeshEditing() ) ) S.bRequestOpenMakePrefabPopup = true;
			if ( ImGui::MenuItem( "Unpack Prefab", nullptr, false, bHasSelection && S.AssetInfo.IsValid() && !Session.IsMeshEditing() ) )
			{
				const int32 NumUnpacked = Session.UnpackPrefabs( Session.GetSelection(), *S.AssetInfo );
				NWorldEditorUI::SetStatus( S, NumUnpacked > 0 ? FString::Printf( "Unpacked %d prefab(s) into this world", NumUnpacked ) : FString( "Nothing to unpack: select objects with a Prefab component whose world can be read" ), NumUnpacked == 0 );
			}
			ImGui::Separator();
			if ( ImGui::MenuItem( Session.IsMeshEditing() ? "Finish Editing Mesh" : "Edit Mesh", "Tab", false, bHasSelection || Session.IsMeshEditing() ) ) NWorldEditorUI::ToggleMeshEdit( S );
			if ( ImGui::MenuItem( "Convert Models to Meshes", nullptr, false, bHasSelection && S.AssetInfo.IsValid() && !Session.IsMeshEditing() ) )
			{
				const int32 NumConverted = Session.ConvertSelectionToMeshes( *S.AssetInfo );
				NWorldEditorUI::SetStatus( S, NumConverted > 0 ? FString::Printf( "Converted %d model(s) to editable meshes (the images and colours are not carried over)", NumConverted ) : FString( "Nothing to convert: select objects with a Model whose triangles can be read" ), NumConverted == 0 );
			}
			ImGui::EndMenu();
		}
		if ( ImGui::BeginMenu( "Create" ) )
		{
			if ( ImGui::MenuItem( "Empty Object" ) ) NWorldEditorUI::CreateEmptyAtView( S );
			ImGui::Separator();
			if ( ImGui::MenuItem( "Box Collider" ) ) NWorldEditorUI::CreateColliderAtView( S, EWorldColliderShape::Box, false );
			if ( ImGui::MenuItem( "Sphere Collider" ) ) NWorldEditorUI::CreateColliderAtView( S, EWorldColliderShape::Sphere, false );
			if ( ImGui::MenuItem( "Capsule Collider" ) ) NWorldEditorUI::CreateColliderAtView( S, EWorldColliderShape::Capsule, false );
			if ( ImGui::MenuItem( "Trigger Box" ) ) NWorldEditorUI::CreateColliderAtView( S, EWorldColliderShape::Box, true );
			ImGui::Separator();
			if ( ImGui::BeginMenu( "Mesh" ) )
			{
				for ( int32 i = 0; i < NEditMesh::NumPrimitives; ++i )
				{
					if ( ImGui::MenuItem( NEditMesh::ToString( static_cast<EEditMeshPrimitive>( i ) ) ) ) NWorldEditorUI::CreateMeshAtView( S, static_cast<EEditMeshPrimitive>( i ) );
				}
				ImGui::EndMenu();
			}
			ImGui::Separator();
			ImGui::TextDisabled( "Models, images, sounds, effects and worlds: drag them from World Assets" );
			ImGui::EndMenu();
		}
		if ( ImGui::BeginMenu( "View" ) )
		{
			ImGui::MenuItem( "Outliner", nullptr, &S.bShowOutliner );
			ImGui::MenuItem( "Inspector", nullptr, &S.bShowInspector );
			ImGui::MenuItem( "Assets", nullptr, &S.bShowAssets );
			ImGui::MenuItem( "Issues", nullptr, &S.bShowIssues );
			ImGui::Separator();
			ImGui::MenuItem( "Grid", nullptr, &S.bShowGrid );
			ImGui::MenuItem( "Names", nullptr, &S.bShowNames );
			ImGui::MenuItem( "Gizmo", nullptr, &S.bShowGizmo );
			ImGui::MenuItem( "Play effects", nullptr, &S.bPlayEffects );
			if ( ImGui::MenuItem( "Restart effects", nullptr, false, S.bPlayEffects ) ) ++S.EffectRestartSerial;
			if ( ImGui::BeginMenu( "Colliders" ) )
			{
				if ( ImGui::MenuItem( "Off", nullptr, S.ColliderView == FWorldEditorState::EColliderView::Off ) ) S.ColliderView = FWorldEditorState::EColliderView::Off;
				if ( ImGui::MenuItem( "Selected only", nullptr, S.ColliderView == FWorldEditorState::EColliderView::Selected ) ) S.ColliderView = FWorldEditorState::EColliderView::Selected;
				if ( ImGui::MenuItem( "All", nullptr, S.ColliderView == FWorldEditorState::EColliderView::All ) ) S.ColliderView = FWorldEditorState::EColliderView::All;
				ImGui::EndMenu();
			}
			ImGui::Separator();
			if ( ImGui::MenuItem( "Frame All" ) ) NWorldEditorUI::FrameAll( S );
			if ( ImGui::MenuItem( "Reload Assets" ) )
			{
				if ( S.AssetInfo.IsValid() ) S.AssetInfo->Clear();
				NWorldEditorUI::RescanFiles( S, true );
				S.IssuesRevision = ~0u;
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	void DrawToolbar( FWorldEditorState& S )
	{
		CWorldEditorSession& Session = S.Session;
		const auto ModeButton = [&]( const char* Label, EEditorGizmoMode Mode, const char* Tip )
		{
			const bool bActive = Session.GizmoMode == Mode;
			if ( bActive ) ImGui::PushStyleColor( ImGuiCol_Button, ImGui::GetStyleColorVec4( ImGuiCol_ButtonActive ) );
			if ( ImGui::Button( Label ) ) Session.GizmoMode = Mode;
			if ( bActive ) ImGui::PopStyleColor();
			if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "%s", Tip );
			ImGui::SameLine();
		};
		ModeButton( "Move", EEditorGizmoMode::Translate, "Move  [W]" );
		ModeButton( "Rotate", EEditorGizmoMode::Rotate, "Rotate  [E]" );
		ModeButton( "Scale", EEditorGizmoMode::Scale, "Scale  [R]" );

		const bool bLocal = Session.GizmoSpace == EEditorGizmoSpace::Local;
		if ( ImGui::Button( bLocal ? "Local" : "World" ) ) Session.GizmoSpace = bLocal ? EEditorGizmoSpace::World : EEditorGizmoSpace::Local;
		if ( ImGui::IsItemHovered() ) ImGui::SetTooltip( "Gizmo axes: world or the object's own  [X]" );
		ImGui::SameLine();

		ImGui::Checkbox( "Snap", &Session.bSnap );
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 70.0f );
		switch ( Session.GizmoMode )
		{
		case EEditorGizmoMode::Translate: ImGui::DragFloat( "##MoveSnap", &Session.MoveSnap, 0.05f, 0.01f, 100.0f, "%.2f m" ); break;
		case EEditorGizmoMode::Rotate: ImGui::DragFloat( "##RotateSnap", &Session.RotateSnapDegrees, 0.5f, 0.5f, 180.0f, "%.1f deg" ); break;
		case EEditorGizmoMode::Scale: ImGui::DragFloat( "##ScaleSnap", &Session.ScaleSnap, 0.01f, 0.01f, 10.0f, "%.2f" ); break;
		}
		ImGui::SameLine();
		ImGui::TextDisabled( "|" );
		ImGui::SameLine();

		ImGui::BeginDisabled( !Session.Document.CanUndo() || Session.IsGizmoDragging() );
		if ( ImGui::Button( "Undo" ) ) Session.Undo();
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled( !Session.Document.CanRedo() || Session.IsGizmoDragging() );
		if ( ImGui::Button( "Redo" ) ) Session.Redo();
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled( Session.GetSelection().IsEmpty() );
		if ( ImGui::Button( "Focus" ) ) Session.FocusSelection( S.View, S.AssetInfo.Get() );
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 110.0f );
		int32 ColliderView = static_cast<int32>( S.ColliderView );
		if ( ImGui::Combo( "##Colliders", &ColliderView, "Colliders: off\0Colliders: selected\0Colliders: all\0" ) ) S.ColliderView = static_cast<FWorldEditorState::EColliderView>( ColliderView );
	}

	void DrawStatusBar( FWorldEditorState& S )
	{
		const CWorldEditorSession& Session = S.Session;
		ImGui::Separator();
		const FString File = Session.FilePath.IsEmpty() ? FString( "(unsaved)" ) : Session.FilePath;
		ImGui::Text( "%s%s", UI_TEXT( File ), Session.Document.IsDirty() ? " *" : "" );
		ImGui::SameLine();
		ImGui::TextDisabled( "| %d objects | %d selected", Session.Document.Get().Objects.Num(), Session.GetSelection().Num() );
		if ( !S.StatusMessage.IsEmpty() )
		{
			ImGui::SameLine();
			ImGui::TextColored( S.bStatusIsError ? ImVec4( 1.0f, 0.5f, 0.4f, 1.0f ) : ImVec4( 0.6f, 0.9f, 0.6f, 1.0f ), "| %s", UI_TEXT( S.StatusMessage ) );
		}
	}

	//=========================================================================
	// ダイアログ
	//=========================================================================
	void DrawPopups( FWorldEditorState& S )
	{
		if ( S.bRequestOpenUnsavedPopup )
		{
			S.bRequestOpenUnsavedPopup = false;
			ImGui::OpenPopup( "Unsaved changes##World" );
		}
		if ( S.bRequestOpenOpenPopup )
		{
			S.bRequestOpenOpenPopup = false;
			S.OpenSelection = FString();
			NWorldEditorUI::RescanFiles( S, true );
			ImGui::OpenPopup( "Open world##World" );
		}
		if ( S.bRequestOpenSaveAsPopup )
		{
			S.bRequestOpenSaveAsPopup = false;
			if ( S.SaveAsPath.IsEmpty() ) S.SaveAsPath = S.Session.FilePath.IsEmpty() ? FString( "Levels/NewWorld.world" ) : S.Session.FilePath;
			ImGui::OpenPopup( "Save world as##World" );
		}

		if ( ImGui::BeginPopupModal( "Unsaved changes##World", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::TextUnformatted( "The world has unsaved changes." );
			if ( ImGui::Button( "Save" ) )
			{
				if ( S.Session.FilePath.IsEmpty() )
				{
					// 保存先を決めてから（保存したら、予約した操作を続ける）
					ImGui::CloseCurrentPopup();
					S.bRequestOpenSaveAsPopup = true;
				}
				else if ( NDebugUI::SaveWorldFile( S ) )
				{
					ImGui::CloseCurrentPopup();
					ExecutePending( S );
				}
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Discard" ) )
			{
				ImGui::CloseCurrentPopup();
				S.Session.Document.MarkSaved();
				ExecutePending( S );
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) )
			{
				S.Pending = FWorldEditorState::EPending::None;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::SetNextWindowSize( ImVec2( 460.0f, 360.0f ), ImGuiCond_Appearing );
		if ( ImGui::BeginPopupModal( "Open world##World", nullptr, 0 ) )
		{
			ImGui::TextUnformatted( "World files under the asset folder:" );
			ImGui::BeginChild( "##Files", ImVec2( 0.0f, -ImGui::GetFrameHeightWithSpacing() * 2.0f ), ImGuiChildFlags_Borders );
			if ( S.WorldFiles.IsEmpty() ) ImGui::TextDisabled( "(none)" );
			for ( const FString& File : S.WorldFiles )
			{
				if ( ImGui::Selectable( UI_TEXT( File ), File == S.OpenSelection, ImGuiSelectableFlags_AllowDoubleClick ) )
				{
					S.OpenSelection = File;
					if ( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
					{
						ImGui::CloseCurrentPopup();
						RequestAction( S, FWorldEditorState::EPending::Open, File );
					}
				}
			}
			ImGui::EndChild();
			NDebugUI::InputStringWithHint( "##OpenPath", "or type a path", S.OpenSelection );
			ImGui::BeginDisabled( S.OpenSelection.IsEmpty() );
			if ( ImGui::Button( "Open" ) )
			{
				ImGui::CloseCurrentPopup();
				RequestAction( S, FWorldEditorState::EPending::Open, S.OpenSelection );
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) ) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		if ( S.bRequestOpenMakePrefabPopup )
		{
			S.bRequestOpenMakePrefabPopup = false;
			if ( S.MakePrefabPath.IsEmpty() ) S.MakePrefabPath = "Prefabs/NewPrefab.world";
			ImGui::OpenPopup( "Make prefab##World" );
		}
		if ( ImGui::BeginPopupModal( "Make prefab##World", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::TextUnformatted( "The selected objects (and what is under them) are saved as a world file," );
			ImGui::TextUnformatted( "and replaced by one object that places it. Path under the asset folder:" );
			ImGui::SetNextItemWidth( 360.0f );
			NDebugUI::InputString( "##MakePrefabPath", S.MakePrefabPath );
			ImGui::BeginDisabled( S.MakePrefabPath.TrimStartAndEnd().IsEmpty() );
			if ( ImGui::Button( "Make" ) && NWorldEditorUI::MakePrefabFromSelection( S, S.MakePrefabPath ) )
			{
				S.MakePrefabPath = FString();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) ) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		if ( ImGui::BeginPopupModal( "Save world as##World", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			ImGui::TextUnformatted( "Path under the asset folder (.world is added):" );
			ImGui::SetNextItemWidth( 360.0f );
			NDebugUI::InputString( "##SaveAsPath", S.SaveAsPath );
			ImGui::BeginDisabled( S.SaveAsPath.TrimStartAndEnd().IsEmpty() );
			if ( ImGui::Button( "Save" ) && NDebugUI::SaveWorldFile( S, S.SaveAsPath ) )
			{
				S.SaveAsPath = FString();
				ImGui::CloseCurrentPopup();
				if ( S.Pending != FWorldEditorState::EPending::None ) ExecutePending( S );
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) )
			{
				S.Pending = FWorldEditorState::EPending::None;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	//=========================================================================
	// ショートカット
	//=========================================================================
	void HandleShortcuts( FWorldEditorState& S )
	{
		ImGuiIO& IO = ImGui::GetIO();
		if ( !S.bAnyPanelFocused || IO.WantTextInput ) return;
		CWorldEditorSession& Session = S.Session;
		const bool bCtrl = IO.KeyCtrl;
		const bool bShift = IO.KeyShift;
		const bool bFlying = S.MouseMode == FWorldEditorState::EMouseMode::Look;
		const auto Pressed = [&]( ImGuiKey Key ) { return ImGui::IsKeyPressed( Key, false ); };

		if ( bCtrl )
		{
			if ( Pressed( ImGuiKey_Z ) )
			{
				if ( bShift ) Session.Redo();
				else Session.Undo();
			}
			else if ( Pressed( ImGuiKey_Y ) ) Session.Redo();
			else if ( Pressed( ImGuiKey_S ) ) RequestSave( S );
			else if ( Pressed( ImGuiKey_N ) ) RequestAction( S, FWorldEditorState::EPending::New );
			else if ( Pressed( ImGuiKey_O ) ) S.bRequestOpenOpenPopup = true;
			else if ( Pressed( ImGuiKey_D ) ) Session.DuplicateSelection();
			else if ( Pressed( ImGuiKey_C ) ) Session.CopySelection();
			else if ( Pressed( ImGuiKey_V ) ) Session.Paste();
			else if ( Pressed( ImGuiKey_A ) )
			{
				if ( Session.IsMeshEditing() ) Session.MeshSelectAll();
				else Session.SelectAll();
			}
			else if ( Pressed( ImGuiKey_I ) && Session.IsMeshEditing() ) Session.MeshInvertSelection();
			else if ( Pressed( ImGuiKey_G ) )
			{
				if ( bShift ) Session.UngroupSelection();
				else Session.GroupSelection();
			}
			return;
		}

		if ( Pressed( ImGuiKey_Delete ) )
		{
			if ( Session.IsMeshEditing() ) Session.MeshDeleteSelection();
			else Session.DeleteSelection();
		}
		if ( Pressed( ImGuiKey_Escape ) )
		{
			if ( Session.IsGizmoDragging() ) Session.CancelGizmoDrag();
			else if ( Session.IsMeshEditing() )
			{
				// 選んだ要素があれば外し、無ければ編集を終える
				if ( !Session.GetMeshSelection().IsEmpty() ) Session.MeshSelectNone();
				else Session.EndMeshEdit();
			}
			else Session.ClearSelection();
		}
		if ( bFlying ) return;   // 右ボタンで飛んでいる間の W A S D Q E は、移動に使う
		if ( Pressed( ImGuiKey_Tab ) )
		{
			NWorldEditorUI::ToggleMeshEdit( S );
			// ImGui は、Tab を「次の入力欄へ」と受け取り、最初の入力欄（Snap など）を選んでしまう。次のフレームの頭で反映される前に、その依頼を取り消す
			ImGui::NavMoveRequestCancel();
		}
		if ( Session.IsMeshEditing() )
		{
			if ( Pressed( ImGuiKey_1 ) ) Session.SetMeshElement( NEditMesh::EEditMeshElement::Vertex );
			if ( Pressed( ImGuiKey_2 ) ) Session.SetMeshElement( NEditMesh::EEditMeshElement::Edge );
			if ( Pressed( ImGuiKey_3 ) ) Session.SetMeshElement( NEditMesh::EEditMeshElement::Face );
			if ( Pressed( ImGuiKey_L ) ) Session.MeshSelectLinked();
		}
		if ( Pressed( ImGuiKey_W ) ) Session.GizmoMode = EEditorGizmoMode::Translate;
		if ( Pressed( ImGuiKey_E ) ) Session.GizmoMode = EEditorGizmoMode::Rotate;
		if ( Pressed( ImGuiKey_R ) ) Session.GizmoMode = EEditorGizmoMode::Scale;
		if ( Pressed( ImGuiKey_X ) ) Session.GizmoSpace = Session.GizmoSpace == EEditorGizmoSpace::World ? EEditorGizmoSpace::Local : EEditorGizmoSpace::World;
		if ( Pressed( ImGuiKey_F ) ) Session.FocusSelection( S.View, S.AssetInfo.Get() );
	}
}

//=============================================================================
// 窓
//=============================================================================
void NWorldEditorUI::RequestOpenWorld( FWorldEditorState& S, const FString& Path )
{
	if ( Path.IsEmpty() ) return;
	RequestAction( S, FWorldEditorState::EPending::Open, Path );
}

void NDebugUI::DrawWorldEditor( FWorldEditorState& S, bool* pOpen )
{
	CAssetManager* Assets = NWorldEditorUI::GetAssets();
	if ( Assets != nullptr && !S.AssetInfo.IsValid() ) S.AssetInfo = MakeUnique<FAssetEditorInfo>( *Assets );
	S.Session.SyncSelection();
	NWorldEditorUI::RescanFiles( S, false );
	NWorldEditorUI::RefreshIssues( S );
	S.bAnyPanelFocused = false;
	S.bCloseRequested = false;

	// × ボタンで閉じようとしたとき、未保存の変更があれば確認する
	const bool bWasOpen = pOpen == nullptr || *pOpen;

	NWorldEditorUI::SetNextWindowRect( 0.01f, 0.04f, 0.58f, 0.62f );
	if ( ImGui::Begin( "World Editor", pOpen, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse ) )
	{
		if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) ) S.bAnyPanelFocused = true;
		DrawMenuBar( S );
		DrawToolbar( S );
		NWorldEditorUI::DrawMeshToolbar( S );
		NWorldEditorUI::DrawViewport( S );
		DrawStatusBar( S );
	}
	ImGui::End();

	if ( S.bShowOutliner ) NWorldEditorUI::DrawOutliner( S );
	if ( S.bShowInspector ) NWorldEditorUI::DrawInspector( S );
	if ( S.bShowAssets ) NWorldEditorUI::DrawAssetBrowser( S );
	if ( S.bShowIssues ) NWorldEditorUI::DrawIssuesPanel( S );

	if ( bWasOpen && pOpen != nullptr && !*pOpen && S.Session.Document.IsDirty() )
	{
		*pOpen = true;
		RequestAction( S, FWorldEditorState::EPending::Close );
	}
	DrawPopups( S );
	HandleShortcuts( S );
	if ( S.bCloseRequested && pOpen != nullptr ) *pOpen = false;

	// どの項目も操作されていないフレームで、連続した変更のまとまりを区切る
	if ( !ImGui::IsAnyItemActive() && !S.Session.IsGizmoDragging() ) S.Session.Document.EndEditSession();
}
