// SPDX-License-Identifier: MIT
#include "WorldEditorModelMaterialPanel.h"


#include "Library/Core/Types/CString.h"
#include "Library/DebugUI/EditorCommon.h"
#include "Library/DebugUI/WorldEditorInternal.h"
#include "Library/DebugUI/WorldEditorMeshMaterialPanel.h"
#include "Library/DebugUI/WorldEditorModelPreview.h"
#include "Library/DebugUI/WorldEditorModelPreviewKey.h"
#include "Library/Framework/Asset/EditorAssetInfo.h"
#include "Library/Framework/Asset/StaticModelSurface.h"
#include "Library/Framework/Render/ModelMaterialEffectRenderer.h"
#include "Library/Framework/World/WorldEdit.h"

namespace
{
	// ImGuiの編集終了をDocumentのcoalesce境界へ伝える。
	// bChanged: widgetが変えた値。bInputEnded: 呼出側の終了印へORする値。
	bool TrackModelMaterialInputEnd( bool bChanged, bool& bInputEnded )
	{
		if ( ImGui::IsItemDeactivatedAfterEdit() ) bInputEnded = true;
		return bChanged;
	}

	// Modelの全設定を比較し、transportが現行の保存値を操作できるか調べる。
	// Saved: 現在の保存値。Prepared: 前回更新時の値。一致しなければfalse。
	bool SameModelSettings( const FWorldModel& Saved, const FWorldModel& Prepared )
	{
		if ( Saved.Path != Prepared.Path || Saved.Animations != Prepared.Animations || Saved.Clip != Prepared.Clip || FMemory::Memcmp( &Saved.Speed, &Prepared.Speed, sizeof( float32 ) ) != 0 ) return false;
		if ( Saved.MaterialEffect.IsSet() != Prepared.MaterialEffect.IsSet() ) return false;
		if ( !Saved.MaterialEffect.IsSet() ) return true;
		// 保存値側のoptional内binding。
		const FWorldModelMaterialEffect& Left = Saved.MaterialEffect.GetValue();
		// preview側のoptional内binding。
		const FWorldModelMaterialEffect& Right = Prepared.MaterialEffect.GetValue();
		return Left.IsSameAs( Right );
	}

	// Model材質枠の色・借用画像の有無・section数を表示する。
	void DrawStaticModelMaterialList( const FStaticModelSurfaceData& Data )
	{
		ImGui::SeparatorText( "Model material slots" );
		if ( Data.Materials.IsEmpty() )
		{
			ImGui::TextDisabled( "No static material slots were found." );
			return;
		}
		// 実際に取得した材質slotを番号順に表示する。
		for ( int32 MaterialIndex = 0; MaterialIndex < Data.Materials.Num(); ++MaterialIndex )
		{
			// このslotの色、画像handle、使用sectionを表示する。
			const FStaticModelSurfaceMaterial& Material = Data.Materials[MaterialIndex];
			// このslotを参照するsectionの数。
			int32 SectionCount = 0;
			// snapshotに保存されたsectionを数える。
			for ( const FStaticModelSurfaceSection& Section : Data.Sections )
			{
				if ( Section.MaterialIndex == MaterialIndex ) ++SectionCount;
			}
			ImGui::PushID( MaterialIndex );
			ImGui::ColorButton( "##Diffuse", ImVec4( Material.Diffuse[0], Material.Diffuse[1], Material.Diffuse[2], Material.Diffuse[3] ), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_AlphaPreview, ImVec2( 22.0f, 16.0f ) );
			ImGui::SameLine();
			ImGui::Text( "Slot %d  Diffuse %.3f, %.3f, %.3f, %.3f  Image: %s  Sections: %d", MaterialIndex, Material.Diffuse[0], Material.Diffuse[1], Material.Diffuse[2], Material.Diffuse[3], Material.BorrowedDiffuseTexture >= 0 ? "yes" : "no", SectionCount );
			ImGui::PopID();
		}
	}
}

void NWorldEditorUI::DrawModelMaterialEffectPanel( FWorldEditorState& State, int32 ObjectId )
{
	// 現在のdocumentと編集履歴を持つsession。
	CWorldEditorSession& Session = State.Session;
	// このframeの最新Modelを引く。
	const FWorldObject* FoundObject = Session.Document.Get().FindObject( ObjectId );
	if ( FoundObject == nullptr || !FoundObject->Model.IsSet() ) return;
	// UI操作は保存前の一時値へまとめる。
	FWorldModel EditedModel = FoundObject->Model.GetValue();
	// 既に準備済みのslot情報だけを借りる。
	const TSharedPtr<const FStaticModelSurface> PreparedSurface = State.AssetInfo.IsValid() ? State.AssetInfo->FindPreparedModelSurface( EditedModel.Path ) : TSharedPtr<const FStaticModelSurface>();
	// 定義編集で保存値が変化した印。
	bool bChanged = false;
	// 入力を離して履歴を区切る印。
	bool bInputEnded = false;
	ImGui::SeparatorText( "Model material effect" );

	if ( !EditedModel.MaterialEffect.IsSet() )
	{
		if ( ImGui::Button( "Add material effect (disabled)###ModelMaterialBinding" ) )
		{
			EditedModel.MaterialEffect = FWorldModelMaterialEffect();
			bChanged = true;
			bInputEnded = true;
		}
	}
	else
	{
		if ( ImGui::Button( "Remove material effect###ModelMaterialBinding" ) )
		{
			EditedModel.MaterialEffect.Reset();
			bChanged = true;
			bInputEnded = true;
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Reset binding" ) )
		{
			EditedModel.MaterialEffect = FWorldModelMaterialEffect();
			bChanged = true;
			bInputEnded = true;
		}
		if ( EditedModel.MaterialEffect.IsSet() )
		{
			// Reset後も有効な保存binding。
			FWorldModelMaterialEffect& CurrentBinding = EditedModel.MaterialEffect.GetValue();
			bChanged |= TrackModelMaterialInputEnd( ImGui::InputInt( "Material index", &CurrentBinding.MaterialIndex ), bInputEnded );
			if ( PreparedSurface.IsValid() )
			{
				// 現在選択できるslot一覧。
				const FStaticModelSurfaceData& SurfaceData = PreparedSurface->GetData();
				// 保存番号が実slot範囲にあるか。
				const bool bIndexValid = CurrentBinding.MaterialIndex >= 0 && CurrentBinding.MaterialIndex < SurfaceData.Materials.Num();
				// 不正番号を変更せずcombo見出しへ残す。
				const FString SlotLabel = bIndexValid ? FString::Printf( "Slot %d", CurrentBinding.MaterialIndex ) : FString::Printf( "Invalid slot (%d)", CurrentBinding.MaterialIndex );
				if ( ImGui::BeginCombo( "Choose material slot", UI_TEXT( SlotLabel ) ) )
				{
					// snapshotにあるslotだけを選択候補にする。
					for ( int32 SlotIndex = 0; SlotIndex < SurfaceData.Materials.Num(); ++SlotIndex )
					{
						// 候補slotの表示名。
						const FString Option = FString::Printf( "Slot %d", SlotIndex );
						if ( ImGui::Selectable( UI_TEXT( Option ), CurrentBinding.MaterialIndex == SlotIndex ) )
						{
							CurrentBinding.MaterialIndex = SlotIndex;
							bChanged = true;
							bInputEnded = true;
						}
					}
					ImGui::EndCombo();
				}
			}
			bChanged |= TrackModelMaterialInputEnd( ImGui::Checkbox( "Two-sided", &CurrentBinding.bTwoSided ), bInputEnded );
			// 共通definition欄から返る入力終了を一時受けする。
			bool bDefinitionInputEnded = false;
			bChanged |= DrawMaterialEffectDefinitionPanel( CurrentBinding.Effect, State.TextureFiles, bDefinitionInputEnded );
			bInputEnded |= bDefinitionInputEnded;
			// 内側のEffect検証と重複しないbinding固有の問題を示す。
			bool bHasBindingIssue = false;
			// 不正root、材質番号、型markerを呼出側へ表示する。
			for ( const FString& Issue : CurrentBinding.Validate() )
			{
				if ( Issue.StartsWith( "MaterialEffect.Effect:" ) ) continue;
				bHasBindingIssue = true;
				ImGui::TextWrapped( "%s", UI_TEXT( Issue ) );
			}
			if ( bHasBindingIssue ) ImGui::TextDisabled( "Use Reset binding to clear invalid saved binding types." );
			ImGui::TextDisabled( "Model material effects use unlit LDR Alpha / Add rendering; lighting and bloom are not included." );

			// root Model配置専用previewを引くkey。
			FWorldEditorModelPreviewKey PreviewKey;
			PreviewKey.ObjectIds.Add( ObjectId );
			// Update済みpreviewだけを読む。
			FWorldEditorModelPreview* Preview = State.ModelPreviews.Find( PreviewKey );
			// 保存値とpreview snapshotが一致する場合だけtransportを有効にする。
			const bool bPreviewMatches = Preview != nullptr && Preview->bBuilt && SameModelSettings( EditedModel, Preview->Definition );
			if ( !FoundObject->bEnabled ) ImGui::TextDisabled( "This Model object is disabled and has no active placement preview." );
			else if ( Preview == nullptr || !Preview->bBuilt ) ImGui::TextDisabled( "The placement preview is waiting for its next update." );
			else if ( !bPreviewMatches ) ImGui::TextDisabled( "The placement preview will refresh from the latest saved settings." );
			else if ( !Preview->FailureReason.IsEmpty() ) ImGui::TextWrapped( "%s", UI_TEXT( Preview->FailureReason ) );
			if ( CurrentBinding.Effect.Material.bEnabled && bPreviewMatches && Preview->Animator.GetMaterialEffect().IsPrepared() )
			{
				// 専用個体の再生状態。
				FModelMaterialEffectRenderer& Renderer = Preview->Animator.GetMaterialEffect();
				ImGui::SeparatorText( "Placement preview transport" );
				ImGui::BeginDisabled( !FoundObject->bEnabled );
				if ( ImGui::Button( "Play" ) ) Renderer.Play();
				ImGui::SameLine();
				if ( ImGui::Button( "Pause" ) ) Renderer.Pause();
				ImGui::SameLine();
				if ( ImGui::Button( "Stop" ) ) Renderer.Stop();
				ImGui::SameLine();
				if ( ImGui::Button( "Restart" ) ) Renderer.Restart();
				ImGui::EndDisabled();
				// 専用previewの再生状態。
				const FMeshMaterialEffectPlayback::EEffectPlaybackState Playback = Renderer.GetPlaybackState();
				// 停止を既定にして再生状態を表示名へ変換する。
				const char* StateText = "Stopped";
				switch ( Playback )
				{
				case FMeshMaterialEffectPlayback::EEffectPlaybackState::Playing: StateText = "Playing"; break;
				case FMeshMaterialEffectPlayback::EEffectPlaybackState::Paused: StateText = "Paused"; break;
				case FMeshMaterialEffectPlayback::EEffectPlaybackState::Finished: StateText = "Finished"; break;
				default: break;
				}
				ImGui::TextDisabled( "%s  %.3f s", StateText, Renderer.GetEffectTime() );
			}
			else if ( bChanged ) ImGui::TextDisabled( "Preview controls will refresh after this setting is saved." );
			else if ( CurrentBinding.Effect.Material.bEnabled && Preview != nullptr && !Preview->FailureReason.IsEmpty() ) ImGui::TextDisabled( "Playback controls are unavailable while the effect is not prepared." );
		}
	}

	if ( State.AssetInfo.IsValid() )
	{
		if ( PreparedSurface.IsValid() )
		{
			// snapshotが保持する実材質slotを表示する。
			const FStaticModelSurfaceData& SurfaceData = PreparedSurface->GetData();
			DrawStaticModelMaterialList( SurfaceData );
			if ( EditedModel.MaterialEffect.IsSet() )
			{
				// 保存値を丸めず、実slot範囲外だけを知らせる。
				const int32 MaterialIndex = EditedModel.MaterialEffect.GetValue().MaterialIndex;
				if ( MaterialIndex < 0 || MaterialIndex >= SurfaceData.Materials.Num() ) ImGui::TextColored( ImVec4( 1.0f, 0.55f, 0.4f, 1.0f ), "Material index %d is outside the %d available slot(s). The saved value is preserved.", MaterialIndex, SurfaceData.Materials.Num() );
			}
		}
		else ImGui::TextDisabled( "Static material information is pending or unavailable; it is prepared before viewport recording." );
	}
	else ImGui::TextDisabled( "Static material information is unavailable until the editor asset cache is ready." );

	if ( bChanged )
	{
		// 編集前参照を再利用せず、最新documentで対象を確認する。
		const FWorldObject* CurrentObject = Session.Document.Get().FindObject( ObjectId );
		if ( CurrentObject != nullptr && CurrentObject->Model.IsSet() )
		{
			// objectごとに独立し連続入力をまとめる履歴key。
			char CoalesceKey[64] = {};
			NCString::Format( CoalesceKey, sizeof( CoalesceKey ), "Inspector.Model.MaterialEffect.%d", ObjectId );
			// 最新Model部品だけへ一時値を保存する。
			if ( FWorldObject* EditableObject = NWorldEdit::EditObject( Session.Document, ObjectId, CoalesceKey ); EditableObject != nullptr && EditableObject->Model.IsSet() ) EditableObject->Model.GetValue().MaterialEffect = EditedModel.MaterialEffect;
		}
	}
	if ( bInputEnded ) Session.Document.EndEditSession();
}
