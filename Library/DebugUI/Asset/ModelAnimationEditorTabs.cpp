#include "ModelAnimationEditorInternal.h"

namespace NModelAnimationEditorUI
{
	//=========================================================================
	// 左: モデル・クリップ
	//=========================================================================
	void DrawModelTab( FState& S )
	{
		CAssetSubsystem* Assets = GetAssets();
		const FModelAnimationDefinition& Definition = S.Document.Get();
		ImGui::TextUnformatted( "Model" );
		if ( !Definition.ModelPath.IsEmpty() )
		{
			ImGui::SameLine();
			if ( ImGui::SmallButton( "Clear (motion pack)" ) )
			{
				NModelAnimationEdit::SetModelPath( S.Document, FString() );
			}
		}
		FString Selected = Definition.ModelPath;
		ImGui::SetNextItemWidth( -FLT_MIN );
		if ( NDebugUI::FileCombo( "##Model", Selected, S.ModelFiles, "(none - motion pack)" ) )
		{
			NModelAnimationEdit::SetModelPath( S.Document, Selected );
		}

		bool bNeedPreviewModel = Definition.ModelPath.IsEmpty();
		if ( S.Asset.IsValid() && S.Asset->GetModel().IsValid() )
		{
			const TSharedPtr<CModel>& Model = S.Asset->GetModel();
			ImGui::Text( "%s, ~%s in memory", UI_TEXT( Model->Describe() ), UI_TEXT( NDebugUI::FormatBytes( Model->GetMemoryBytes() ) ) );
			if ( Model->IsAnimationOnly() )
			{
				bNeedPreviewModel = true;
				ImGui::TextColored( ImVec4( 0.55f, 0.85f, 1.0f, 1.0f ), "Animation-only file (0 meshes). Choose a Preview model below to see 3D playback." );
			}
		}
		else if ( !Definition.ModelPath.IsEmpty() )
		{
			NDebugUI::PushWarningColor();
			ImGui::TextWrapped( "The model could not be loaded (see the Output Log)." );
			NDebugUI::PopWarningColor();
		}
		else
		{
			ImGui::TextDisabled( "Motion pack (no bound model). Can be applied to any character with CreateAnimatorFor." );
		}

		if ( bNeedPreviewModel || !Definition.PreviewModelPath.IsEmpty() )
		{
			ImGui::Spacing();
			ImGui::TextUnformatted( "Preview model (editor only)" );
			if ( !Definition.PreviewModelPath.IsEmpty() )
			{
				ImGui::SameLine();
				if ( ImGui::SmallButton( "Clear##Preview" ) )
				{
					NModelAnimationEdit::SetPreviewModelPath( S.Document, FString() );
				}
			}
			FString PreviewSelected = Definition.PreviewModelPath;
			ImGui::SetNextItemWidth( -FLT_MIN );
			if ( NDebugUI::FileCombo( "##PreviewModel", PreviewSelected, S.ModelFiles, "(choose a mesh model for 3D preview)" ) )
			{
				NModelAnimationEdit::SetPreviewModelPath( S.Document, PreviewSelected );
			}
			if ( S.PreviewModel.IsValid() && S.PreviewModel->IsAnimationOnly() )
			{
				NDebugUI::PushWarningColor();
				ImGui::TextWrapped( "The selected preview model also has 0 meshes. Pick a model with a mesh." );
				NDebugUI::PopWarningColor();
			}
		}

		float32 UnitsPerSecond = Definition.UnitsPerSecond;
		if ( ImGui::DragFloat( "Time units / s", &UnitsPerSecond, 0.1f, 0.001f, 1000.0f, "%.2f" ) )
		{
			NModelAnimationEdit::SetUnitsPerSecond( S.Document, UnitsPerSecond, "ups" );
		}
		if ( ImGui::IsItemHovered() )
		{
			ImGui::SetTooltip( "The model's animation time is in frames. 30 per second is usual for FBX / MV1 / X." );
		}

		ImGui::SeparatorText( "Placement (applied when drawn)" );
		FVector3 Offset = Definition.Offset, Rotation = Definition.RotationDegrees, Scale = Definition.Scale;
		bool bChanged = false;
		bChanged |= ImGui::DragFloat3( "Offset", &Offset.x, 0.01f );
		bChanged |= ImGui::DragFloat3( "Rotation", &Rotation.x, 0.5f, -360.0f, 360.0f, "%.1f deg" );
		bChanged |= ImGui::DragFloat3( "Scale", &Scale.x, 0.005f );
		if ( bChanged )
		{
			NModelAnimationEdit::SetPlacement( S.Document, Offset, Rotation, Scale, "placement" );
		}

		ImGui::SeparatorText( ".mv1 output" );
		ImGui::Checkbox( "Also output .mv1 when saving", &S.bExportMV1OnSave );
		if ( ImGui::IsItemHovered() )
		{
			ImGui::SetTooltip( "When saving this .gxmodelanim, any .fbx / .obj / .x / .pmx / .vmd used by it is also saved as .mv1 next to the source file." );
		}
		if ( Assets != nullptr )
		{
			if ( ImGui::Button( "Export .mv1 now" ) )
			{
				const FModelBatchExportResult Batch = Assets->ExportModelsUsedBy( Definition, false );
				SetStatus( S, ".mv1 export: " + Batch.GetSummary(), Batch.NumFailed > 0 );
				RescanFiles( S, true );
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Force rebuild .mv1" ) )
			{
				const FModelBatchExportResult Batch = Assets->ExportModelsUsedBy( Definition, true );
				SetStatus( S, ".mv1 rebuild: " + Batch.GetSummary(), Batch.NumFailed > 0 );
				RescanFiles( S, true );
			}
		}

		ImGui::SeparatorText( "Check" );
		const TArray<FString> Problems = NModelAnimationEdit::Validate( Definition );
		if ( Problems.IsEmpty() )
		{
			ImGui::TextColored( ImVec4( 0.5f, 1.0f, 0.5f, 1.0f ), "No problems found." );
		}
		else
		{
			ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.35f, 1.0f ), "%d problem(s):", Problems.Num() );
			for ( const FString& Problem : Problems )
			{
				ImGui::TextWrapped( "- %s", UI_TEXT( Problem ) );
			}
		}
		// 定義が指すクリップが、モデルに実在するか・骨の名前が合うか
		if ( S.Asset.IsValid() )
		{
			const TSharedPtr<CModel> TargetMesh = GetEffectiveMeshModel( S );
			for ( int32 i = 0; i < Definition.Clips.Num(); ++i )
			{
				const FModelClipDef& Clip = Definition.Clips[i];
				if ( !S.Animator.HasClip( Clip.Name ) || S.Animator.GetClipLength( Clip.Name ) <= 0.0f )
				{
					NDebugUI::PushWarningColor();
					ImGui::TextWrapped( "- Clip '%s': the animation was not found in its file.", UI_TEXT( Clip.Name ) );
					NDebugUI::PopWarningColor();
				}
				const TSharedPtr<CModel> SourceModel = S.Asset->GetClipSource( i );
				if ( TargetMesh.IsValid() && SourceModel.IsValid() && SourceModel != TargetMesh )
				{
					const FBoneMatch Match = TargetMesh->CompareBones( *SourceModel );
					if ( Match.IsPoor() )
					{
						NDebugUI::PushWarningColor();
						ImGui::TextWrapped( "- Clip '%s': only %d / %d bone(s) match '%s'.", UI_TEXT( Clip.Name ), Match.NumMatched, Match.NumAnimBones, UI_TEXT( TargetMesh->GetPath() ) );
						NDebugUI::PopWarningColor();
					}
				}
			}
		}
	}

	void DrawClipsTab( FState& S )
	{
		CAssetSubsystem* Assets = GetAssets();
		const FModelAnimationDefinition& Definition = S.Document.Get();
		bool bOpenAddPopup = false;
		if ( ImGui::Button( "Add clip..." ) )
		{
			bOpenAddPopup = true;
		}
		ImGui::SameLine();
		ImGui::BeginDisabled( !Definition.Clips.IsValidIndex( S.SelectedClip ) );
		if ( ImGui::Button( "Delete" ) )
		{
			NModelAnimationEdit::RemoveClip( S.Document, S.SelectedClip );
			S.SelectedClip = NMath::Min( S.SelectedClip, S.Document.Get().Clips.Num() - 1 );
			S.SelectedEvent = INDEX_NONE;
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Up" ) && S.SelectedClip > 0 )
		{
			NModelAnimationEdit::MoveClip( S.Document, S.SelectedClip, S.SelectedClip - 1 );
			--S.SelectedClip;
		}
		ImGui::SameLine();
		if ( ImGui::Button( "Down" ) && S.SelectedClip + 1 < Definition.Clips.Num() )
		{
			NModelAnimationEdit::MoveClip( S.Document, S.SelectedClip, S.SelectedClip + 1 );
			++S.SelectedClip;
		}
		ImGui::EndDisabled();

		if ( bOpenAddPopup )
		{
			S.AddSourceSelection.Empty();
			ImGui::OpenPopup( "Add clip" );
		}
		ImGui::SetNextWindowSize( ImVec2( 500.0f, 380.0f ), ImGuiCond_Appearing );
		if ( ImGui::BeginPopupModal( "Add clip", nullptr ) )
		{
			ImGui::TextUnformatted( "Add an animation from:" );
			ImGui::SetNextItemWidth( -FLT_MIN );
			NDebugUI::FileCombo( "##Source", S.AddSourceSelection, S.ModelFiles, Definition.ModelPath.IsEmpty() ? "(choose an animation / model file)" : "(the model itself)" );
			if ( !Definition.ModelPath.IsEmpty() && ImGui::SmallButton( "the model itself" ) )
			{
				S.AddSourceSelection.Empty();
			}

			const FString EffectiveSourcePath = S.AddSourceSelection.IsEmpty() ? Definition.ModelPath : S.AddSourceSelection;
			TSharedPtr<CModel> Source;
			if ( Assets != nullptr && !EffectiveSourcePath.IsEmpty() )
			{
				Source = Assets->LoadModel( EffectiveSourcePath );
			}
			if ( Source.IsValid() )
			{
				ImGui::TextDisabled( "%s   (%d bone(s))", UI_TEXT( Source->Describe() ), Source->GetBoneNames().Num() );
				const TSharedPtr<CModel> TargetMesh = GetEffectiveMeshModel( S );
				if ( TargetMesh.IsValid() && Source != TargetMesh )
				{
					const FBoneMatch Match = TargetMesh->CompareBones( *Source );
					if ( Match.IsPoor() )
					{
						NDebugUI::PushWarningColor();
						ImGui::TextWrapped( "Bone match with '%s': %d / %d (%.0f%%) - most bones do not match.", UI_TEXT( TargetMesh->GetPath() ), Match.NumMatched, Match.NumAnimBones, Match.GetRatio() * 100.0f );
						NDebugUI::PopWarningColor();
					}
					else
					{
						ImGui::TextColored( ImVec4( 0.55f, 0.95f, 0.65f, 1.0f ), "Bone match with '%s': %d / %d (%.0f%%)", UI_TEXT( TargetMesh->GetPath() ), Match.NumMatched, Match.NumAnimBones, Match.GetRatio() * 100.0f );
					}
				}
				if ( Source->GetClips().Num() > 1 )
				{
					char BatchLabel[96];
					NCString::Format( BatchLabel, sizeof( BatchLabel ), "Add all animations (%d)", Source->GetClips().Num() );
					if ( ImGui::Button( BatchLabel ) )
					{
						TArray<FModelClipDef> Batch;
						for ( const FModelClipInfo& Info : Source->GetClips() )
						{
							FModelClipDef Clip;
							Clip.Name = Info.Name.IsEmpty() ? FString::Printf( "Anim%d", Info.AnimIndex ) : Info.Name;
							Clip.SourcePath = S.AddSourceSelection;
							Clip.AnimIndex = Info.AnimIndex;
							Clip.AnimName = Info.Name;
							Batch.Add( Clip );
						}
						S.SelectedClip = NModelAnimationEdit::AddClips( S.Document, Batch );
						S.SelectedEvent = INDEX_NONE;
						ImGui::CloseCurrentPopup();
					}
				}
			}
			ImGui::BeginChild( "##Animations", ImVec2( 0.0f, -ImGui::GetFrameHeightWithSpacing() ), ImGuiChildFlags_Borders );
			if ( Source.IsValid() )
			{
				if ( Source->GetClips().IsEmpty() )
				{
					ImGui::TextDisabled( "This file has no animations." );
				}
				for ( const FModelClipInfo& Info : Source->GetClips() )
				{
					char Label[200];
					NCString::Format( Label, sizeof( Label ), "%s   (#%d, %.0f time units)###anim%d", Info.Name.IsEmpty() ? "(unnamed)" : *Info.Name, Info.AnimIndex, Info.Length, Info.AnimIndex );
					if ( ImGui::Selectable( Label ) )
					{
						FModelClipDef Clip;
						Clip.Name = Info.Name.IsEmpty() ? FString::Printf( "Anim%d", Info.AnimIndex ) : Info.Name;
						Clip.SourcePath = S.AddSourceSelection;
						Clip.AnimIndex = Info.AnimIndex;
						Clip.AnimName = Info.Name;
						S.SelectedClip = NModelAnimationEdit::AddClip( S.Document, Clip );
						S.SelectedEvent = INDEX_NONE;
						ImGui::CloseCurrentPopup();
					}
				}
			}
			else if ( EffectiveSourcePath.IsEmpty() )
			{
				ImGui::TextDisabled( "Choose a model or animation file above." );
			}
			else
			{
				ImGui::TextDisabled( "The file could not be loaded." );
			}
			ImGui::EndChild();
			if ( ImGui::Button( "Close" ) )
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::BeginChild( "##ClipList", ImVec2( 0.0f, 120.0f ), ImGuiChildFlags_Borders );
		for ( int32 i = 0; i < Definition.Clips.Num(); ++i )
		{
			const FModelClipDef& Clip = Definition.Clips[i];
			char Label[200];
			NCString::Format( Label, sizeof( Label ), "%s   (%.2f s, %s, %d event(s))###clip%d", *Clip.Name, S.Animator.GetClipLength( Clip.Name ), ToString( Clip.Loop ), Clip.Events.Num(), i );
			if ( ImGui::Selectable( Label, S.SelectedClip == i ) )
			{
				S.SelectedClip = i;
				S.SelectedEvent = INDEX_NONE;
			}
		}
		if ( Definition.Clips.IsEmpty() )
		{
			ImGui::TextDisabled( "No clips yet. Click 'Add clip...'." );
		}
		ImGui::EndChild();

		const FModelClipDef* Clip = SelectedClip( S );
		if ( Clip == nullptr )
		{
			return;
		}
		const int32 ClipIndex = S.SelectedClip;

		if ( !ImGui::IsAnyItemActive() )
		{
			S.NameBuffer = Clip->Name;
		}
		NDebugUI::InputString( "Name", S.NameBuffer );
		if ( ImGui::IsItemDeactivatedAfterEdit() )
		{
			S.NameBuffer = NModelAnimationEdit::RenameClip( S.Document, ClipIndex, S.NameBuffer );
		}

		FModelClipDef Edited = *Clip;
		bool bChanged = false;

		// アニメーションの入ったファイルと、その中のアニメーション
		FString Source = Edited.SourcePath;
		ImGui::SetNextItemWidth( -FLT_MIN );
		ImGui::TextUnformatted( "Animation file" );
		if ( NDebugUI::FileCombo( "##SourceFile", Source, S.ModelFiles, "(the model itself)" ) )
		{
			Edited.SourcePath = Source;
			bChanged = true;
		}
		TSharedPtr<CModel> SourceModel = S.Asset.IsValid() ? S.Asset->GetClipSource( ClipIndex ) : TSharedPtr<CModel>();
		if ( SourceModel.IsValid() )
		{
			if ( SourceModel->IsAnimationOnly() )
			{
				ImGui::TextDisabled( "Animation-only source (%d bone(s))", SourceModel->GetBoneNames().Num() );
			}
			const char* Preview = Edited.AnimName.IsEmpty() ? "(by number)" : *Edited.AnimName;
			if ( ImGui::BeginCombo( "Animation", Preview ) )
			{
				for ( const FModelClipInfo& Info : SourceModel->GetClips() )
				{
					char Label[160];
					NCString::Format( Label, sizeof( Label ), "%s  (#%d)###a%d", Info.Name.IsEmpty() ? "(unnamed)" : *Info.Name, Info.AnimIndex, Info.AnimIndex );
					if ( ImGui::Selectable( Label, Info.AnimIndex == Edited.AnimIndex ) )
					{
						Edited.AnimIndex = Info.AnimIndex;
						Edited.AnimName = Info.Name;
						bChanged = true;
					}
				}
				ImGui::EndCombo();
			}
		}
		int Loop = static_cast<int>( Edited.Loop );
		if ( ImGui::Combo( "Loop", &Loop, "Once\0Loop\0Ping-pong\0" ) )
		{
			Edited.Loop = static_cast<EAnimLoopMode>( Loop );
			bChanged = true;
		}
		bChanged |= ImGui::DragFloat( "Speed", &Edited.Speed, 0.01f, -4.0f, 4.0f, "x%.2f" );
		bChanged |= ImGui::DragFloat( "Fade in", &Edited.FadeTime, 0.01f, 0.0f, 5.0f, "%.2f s" );
		if ( ImGui::IsItemHovered() )
		{
			ImGui::SetTooltip( "The crossfade time when the game plays this clip without specifying one." );
		}
		if ( bChanged )
		{
			NModelAnimationEdit::SetClipSettings( S.Document, ClipIndex, Edited, "clip" );
		}

		// クロスフェードの確認
		ImGui::SeparatorText( "Crossfade test" );
		const auto ClipCombo = [&]( const char* Label, int32& Index )
		{
			const char* Preview = Definition.Clips.IsValidIndex( Index ) ? *Definition.Clips[Index].Name : "(none)";
			if ( ImGui::BeginCombo( Label, Preview ) )
			{
				for ( int32 i = 0; i < Definition.Clips.Num(); ++i )
				{
					if ( ImGui::Selectable( *Definition.Clips[i].Name, Index == i ) )
					{
						Index = i;
					}
				}
				ImGui::EndCombo();
			}
		};
		ClipCombo( "From", S.BlendFrom );
		ClipCombo( "To", S.BlendTo );
		ImGui::DragFloat( "Fade time", &S.BlendFade, 0.01f, 0.0f, 5.0f, "%.2f s" );
		const bool bCanBlend = Definition.Clips.IsValidIndex( S.BlendFrom ) && Definition.Clips.IsValidIndex( S.BlendTo );
		ImGui::BeginDisabled( !bCanBlend );
		if ( ImGui::Button( "Play From, then fade to To" ) )
		{
			FModelPlayParams First;
			First.FadeTime = 0.0f;
			First.bRestartIfPlaying = true;
			S.Animator.Play( Definition.Clips[S.BlendFrom].Name, First );
			S.Animator.Update( 0.0f );
			FModelPlayParams Second;
			Second.FadeTime = S.BlendFade;
			Second.bRestartIfPlaying = true;
			S.Animator.Play( Definition.Clips[S.BlendTo].Name, Second );
			S.bPlaying = true;
			S.Animator.Resume();
			S.PlayingClip = S.SelectedClip;   // 選択中のクリップに切り替わり直さない
		}
		ImGui::EndDisabled();
		for ( const FModelAnimator::FLayerInfo& Layer : S.Animator.GetLayerInfos() )
		{
			if ( Layer.Weight <= 0.0f )
			{
				continue;
			}
			char Overlay[96];
			NCString::Format( Overlay, sizeof( Overlay ), "%s  %.0f%%", *Layer.ClipName, Layer.Weight * 100.0f );
			ImGui::ProgressBar( Layer.Weight, ImVec2( -FLT_MIN, 0.0f ), Overlay );
		}
	}
}
