#include "Library/Core/Collection/Map.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/Common/EditorCommon.h"
#include "Library/DebugUI/Asset/AssetBrowser.h"
#include "Library/DebugUI/Common/ImGuiTexture.h"
#include "Library/DebugUI/World/WorldEditorInternal.h"
#include "Library/DebugUI/World/WorldEditorModelMaterialPanel.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/World/Editor/EditorOutliner.h"
#include "Library/Framework/World/Runtime/WorldCollision.h"
#include "Library/Framework/World/Definition/WorldHierarchy.h"

//=============================================================================
// ワールドエディタのパネル（アセット・問題と設定）と、パネルの共通の始め方（アウトライナーは WorldEditorOutliner.cpp、インスペクターは WorldEditorInspector.cpp）
//   編集は NWorldEdit / CWorldEditorSession を呼ぶだけ。連続した変更（ドラッグ・文字入力）は、項目ごとに別のキーを渡して、1 回の編集にまとめる。
//=============================================================================
bool NWorldEditorUI::BeginPanel( FWorldEditorState& S, const char* Title, bool* pOpen, float32 X, float32 Y, float32 Width, float32 Height )
{
	NWorldEditorUI::SetNextWindowRect( X, Y, Width, Height );
	if ( !ImGui::Begin( Title, pOpen ) )
	{
		ImGui::End();
		return false;
	}
	if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) )
	{
		S.bAnyPanelFocused = true;
	}
	return true;
}

FString NWorldEditorUI::ObjectLabel( const FWorldObject& Object )
{
	return Object.Name.IsEmpty() ? FString::Printf( "(unnamed) #%d", Object.Id ) : FString::Printf( "%s  #%d", *Object.Name, Object.Id );
}

void NWorldEditorUI::SectionTitle( const char* Text )
{
	ImGui::Spacing();
	ImGui::TextColored( ImVec4( 0.65f, 0.78f, 1.0f, 1.0f ), "%s", Text );
	ImGui::Separator();
}

//=============================================================================
// アセット
//=============================================================================
namespace
{
	struct FAssetRow
	{
		int32 Kind = 0;   // 1 モデル / 2 画像 / 3 音
		const FString* Path = nullptr;
	};
}

void NWorldEditorUI::DrawAssetBrowser( FWorldEditorState& S )
{
	if ( !BeginPanel( S, "World Assets", &S.bShowAssets, 0.01f, 0.67f, 0.29f, 0.31f ) )
	{
		return;
	}

	ImGui::SetNextItemWidth( 200.0f );
	NDebugUI::InputStringWithHint( "##AssetFilter", "filter", S.AssetFilter );
	ImGui::SameLine();
	ImGui::RadioButton( "All", &S.AssetKind, 0 );
	ImGui::SameLine();
	ImGui::RadioButton( "Models", &S.AssetKind, 1 );
	ImGui::SameLine();
	ImGui::RadioButton( "Images", &S.AssetKind, 2 );
	ImGui::SameLine();
	ImGui::RadioButton( "Sounds", &S.AssetKind, 3 );
	ImGui::SameLine();
	ImGui::RadioButton( "Worlds", &S.AssetKind, 4 );
	ImGui::SameLine();
	ImGui::RadioButton( "Effects", &S.AssetKind, 5 );
	ImGui::SameLine();
	if ( ImGui::Button( "Rescan" ) )
	{
		RescanFiles( S, true );
		S.ModelPreviews.Empty(); // asset cacheを消す前に専用model instanceを解放する
		if ( S.AssetInfo.IsValid() )
		{
			S.AssetInfo->Clear();
		}
		S.IssuesRevision = ~0u;
	}
	ImGui::TextDisabled( "Drag into the view to place.  Double-click to place in front of the camera." );

	TArray<FAssetRow> Rows;
	const auto AddAll = [&]( const TArray<FString>& Files, int32 Kind )
	{
		if ( S.AssetKind != 0 && S.AssetKind != Kind )
		{
			return;
		}
		for ( const FString& File : Files )
		{
			if ( !S.AssetFilter.IsEmpty() && !File.Contains( S.AssetFilter, ESearchCase::IgnoreCase ) )
			{
				continue;
			}
			if ( Kind == 4 && NWorld::NormalizeAssetPath( File ) == NWorld::NormalizeAssetPath( S.Session.FilePath ) )
			{
				continue;   // 開いているワールド自身は、置けない（循環）
			}
			FAssetRow Row;
			Row.Kind = Kind;
			Row.Path = &File;
			Rows.Add( Row );
		}
	};
	AddAll( S.ModelFiles, 1 );
	AddAll( S.TextureFiles, 2 );
	AddAll( S.SoundFiles, 3 );
	AddAll( S.WorldFiles, 4 );
	AddAll( S.EffectFiles, 5 );

	ImGui::BeginChild( "##AssetList", ImVec2( 0.0f, 0.0f ), ImGuiChildFlags_Borders );
	if ( Rows.IsEmpty() )
	{
		ImGui::TextDisabled( "(no files)" );
	}
	ImGuiListClipper Clipper;
	Clipper.Begin( Rows.Num(), 22.0f );
	while ( Clipper.Step() )
	{
		for ( int32 i = Clipper.DisplayStart; i < Clipper.DisplayEnd; ++i )
		{
			const FAssetRow& Row = Rows[i];
			ImGui::PushID( i );
			static const char* const KindLetters[] = { "[?]", "[M]", "[I]", "[S]", "[W]", "[E]" };
			ImGui::TextDisabled( "%s", KindLetters[NMath::Clamp( Row.Kind, 0, 5 )] );
			ImGui::SameLine();
			if ( Row.Kind == 2 && S.AssetInfo.IsValid() )
			{
				if ( const TSharedPtr<CTexture> Texture = S.AssetInfo->GetTexture( *Row.Path ); Texture.IsValid() && Texture->GetHandle() >= 0 )
				{
					ImGui::Image( NDebugUI::GetImGuiTexture( Texture->GetHandle() ), ImVec2( 18.0f, 18.0f ) );
					ImGui::SameLine();
				}
			}
			ImGui::Selectable( UI_TEXT( *Row.Path ), false, ImGuiSelectableFlags_AllowDoubleClick );
			if ( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
			{
				CreateAssetAtView( S, *Row.Path );
			}
			if ( ImGui::BeginDragDropSource( ImGuiDragDropFlags_None ) )
			{
				ImGui::SetDragDropPayload( NWorldEditorUI::AssetPayloadType, **Row.Path, static_cast<size_t>( Row.Path->Len() ) + 1 );
				ImGui::Text( "%s", UI_TEXT( NWorldEditor::GetAssetDisplayName( *Row.Path ) ) );
				ImGui::EndDragDropSource();
			}
			ImGui::PopID();
		}
	}
	ImGui::EndChild();
	ImGui::End();
}

//=============================================================================
// 問題と設定
//=============================================================================
namespace
{
	using FState = FWorldEditorState;
	using NWorldEditorUI::SectionTitle;

	void DrawIssuesTab( FState& S )
	{
		CWorldEditorSession& Session = S.Session;
		int32 NumErrors = 0, NumWarnings = 0;
		for ( const FWorldIssue& Issue : S.Issues )
		{
			( Issue.Severity == EWorldIssueSeverity::Error ? NumErrors : NumWarnings )++;
		}
		ImGui::TextColored( NumErrors > 0 ? ImVec4( 1.0f, 0.5f, 0.4f, 1.0f ) : ImVec4( 0.6f, 0.9f, 0.6f, 1.0f ), "%d error(s)", NumErrors );
		ImGui::SameLine();
		ImGui::TextColored( NumWarnings > 0 ? ImVec4( 1.0f, 0.85f, 0.4f, 1.0f ) : ImVec4( 0.6f, 0.9f, 0.6f, 1.0f ), "%d warning(s)", NumWarnings );
		ImGui::SameLine();
		ImGui::Checkbox( "Show warnings", &S.bIssuesShowWarnings );

		ImGui::BeginChild( "##Issues", ImVec2( 0.0f, 0.0f ), ImGuiChildFlags_Borders );
		if ( S.Issues.IsEmpty() )
		{
			ImGui::TextDisabled( "No problems found." );
		}
		for ( int32 i = 0; i < S.Issues.Num(); ++i )
		{
			const FWorldIssue& Issue = S.Issues[i];
			if ( Issue.Severity == EWorldIssueSeverity::Warning && !S.bIssuesShowWarnings )
			{
				continue;
			}
			ImGui::PushID( i );
			ImGui::PushStyleColor( ImGuiCol_Text, Issue.Severity == EWorldIssueSeverity::Error ? ImVec4( 1.0f, 0.55f, 0.45f, 1.0f ) : ImVec4( 1.0f, 0.85f, 0.4f, 1.0f ) );
			if ( ImGui::Selectable( UI_TEXT( Issue.ToDisplayString() ) ) && Issue.ObjectId != WorldNoId )
			{
				Session.Select( Issue.ObjectId );
				Session.FocusSelection( S.View, S.AssetInfo.Get() );
			}
			ImGui::PopStyleColor();
			ImGui::PopID();
		}
		ImGui::EndChild();
	}

	void DrawSettingsTab( FState& S )
	{
		CWorldEditorSession& Session = S.Session;
		const FWorldDefinition& World = Session.Document.Get();

		FString Name = World.Name;
		ImGui::SetNextItemWidth( 220.0f );
		if ( NDebugUI::InputStringWithHint( "##WorldName", "world name", Name ) )
		{
			Session.Document.BeginEdit( "World.Name" ).Name = Name;
		}
		ImGui::SameLine();
		ImGui::TextDisabled( "format v%d  next id %d", World.Version, World.NextId );

		// 集計: 何が剛体になるか、なぜか
		SectionTitle( "Collision summary" );
		const FWorldCollisionSummary Summary = NWorld::SummarizeCollision( World );
		ImGui::Text( "%d object(s) placed, %d with a collider shape", Summary.NumPlaced, Summary.NumWithCollider );
		ImGui::Text( "collides: %d  (triggers %d, meshes %d)     off: %d", Summary.NumEnabled, Summary.NumTriggers, Summary.NumMeshes, Summary.NumDisabled );
		ImGui::TextDisabled( "decided by: the object itself %d, a tag rule %d, the default %d", Summary.NumByOverride, Summary.NumByRule, Summary.NumByDefault );

		SectionTitle( "Collision rules by tag" );
		ImGui::TextWrapped( "Whether an object with a Collider gets a physics body: the object's own On / Off first, then the first rule below whose tag the object has, then the default (on)." );
		const TArray<FWorldCollisionRule> Rules = World.Settings.CollisionRules;
		for ( int32 i = 0; i < Rules.Num(); ++i )
		{
			ImGui::PushID( i );
			int32 NumTagged = 0;
			for ( const FWorldObject& Object : World.Objects )
			{
				NumTagged += Object.HasTag( Rules[i].Tag ) ? 1 : 0;
			}

			ImGui::BeginDisabled( i == 0 );
			if ( ImGui::ArrowButton( "##Up", ImGuiDir_Up ) )
			{
				NWorldEdit::MoveCollisionRule( Session.Document, i, i - 1 );
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled( i == Rules.Num() - 1 );
			if ( ImGui::ArrowButton( "##Down", ImGuiDir_Down ) )
			{
				NWorldEdit::MoveCollisionRule( Session.Document, i, i + 1 );
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			FString Tag = Rules[i].Tag;
			ImGui::SetNextItemWidth( 130.0f );
			if ( NDebugUI::InputString( "##Tag", Tag ) )
			{
				Session.Document.BeginEdit( *FString::Printf( "Rule.Tag.%d", i ) ).Settings.CollisionRules[i].Tag = Tag;
			}
			ImGui::SameLine();
			int32 Collide = Rules[i].bCollide ? 1 : 0;
			ImGui::SetNextItemWidth( 120.0f );
			if ( ImGui::Combo( "##Collide", &Collide, "never collide\0collide\0" ) )
			{
				Session.Document.BeginEdit().Settings.CollisionRules[i].bCollide = Collide != 0;
			}
			ImGui::SameLine();
			if ( ImGui::SmallButton( "x" ) )
			{
				NWorldEdit::RemoveCollisionRule( Session.Document, i );
				ImGui::PopID();
				break;
			}
			ImGui::SameLine();
			ImGui::TextDisabled( "%d tagged, decides %d", NumTagged, Summary.NumDecidedByRule.IsValidIndex( i ) ? Summary.NumDecidedByRule[i] : 0 );
			ImGui::SameLine();
			ImGui::BeginDisabled( NumTagged == 0 );
			if ( ImGui::SmallButton( "Select" ) )
			{
				Session.SelectByTag( Rules[i].Tag );
			}
			ImGui::EndDisabled();
			ImGui::PopID();
		}
		if ( Rules.IsEmpty() )
		{
			ImGui::TextDisabled( "(no rules)" );
		}

		ImGui::SetNextItemWidth( 130.0f );
		const bool bEnter = NDebugUI::InputStringWithHint( "##NewRule", "tag", S.NewRuleTag, ImGuiInputTextFlags_EnterReturnsTrue );
		ImGui::SameLine();
		if ( ( ImGui::Button( "Add rule" ) || bEnter ) && !S.NewRuleTag.TrimStartAndEnd().IsEmpty() )
		{
			NWorldEdit::SetCollisionRule( Session.Document, S.NewRuleTag, false );
			S.NewRuleTag = FString();
		}
	}
}

void NWorldEditorUI::DrawIssuesPanel( FWorldEditorState& S )
{
	if ( !BeginPanel( S, "World Issues", &S.bShowIssues, 0.31f, 0.67f, 0.28f, 0.31f ) )
	{
		return;
	}
	if ( ImGui::BeginTabBar( "##IssuesTabs" ) )
	{
		if ( ImGui::BeginTabItem( "Issues" ) )
		{
			DrawIssuesTab( S );
			ImGui::EndTabItem();
		}
		if ( ImGui::BeginTabItem( "World settings" ) )
		{
			DrawSettingsTab( S );
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}
