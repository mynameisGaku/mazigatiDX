#include "EffectEditor.h"

#include <algorithm>
#include <cmath>

#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/EditorCommon.h"
#include "Library/DebugUI/EffectEditorInternal.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Effect/ParticleEffectBackend.h"
#include "Library/Framework/Effect/EffectFlipbookFrames.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Platform/FileSystem.h"

DEFINE_LOG_CATEGORY_STATIC( LogEffectEditor, Log )

namespace
{
	// 操作結果を表示し、失敗の場合はログにも残す。
	void SetStatus( FEffectEditorState& State, const FString& Message, bool bError = false )
	{
		State.StatusMessage = Message;
		State.bStatusIsError = bError;
		if ( bError ) LOG( LogEffectEditor, Warning, "%s", *Message );
	}

	// 文書を切り替えたとき、保存されない終点操作を既定値へ戻す。
	void ResetPreviewTargetState( FEffectEditorState& State )
	{
		State.bPreviewTargetEnabled = false;
		State.PreviewTargetPosition = FVector3( 0.0f, 1.0f, 0.0f );
		State.bShowPreviewTargetGuides = true;
		State.bPreviewTargetCameraFocusPending = false;
		State.PreviewTargetDrag.End();
		State.PreviewTargetDragStart = FVector3::Zero;
	}

	// 開く・保存する両方のパスを同じ区切りと余白の扱いにする。
	FString NormalizePath( const FString& Path )
	{
		return Path.TrimStartAndEnd().Replace( "\\", "/" );
	}

	// 既存のファイル列挙を使い、編集中のファイルと画像の候補を更新する。
	void RescanFiles( FEffectEditorState& State )
	{
		// アセットのルートを所有する、既存の管理機構。
		CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
		if ( Assets == nullptr ) return;
		// 一覧を毎フレーム走査しないための現在時刻。
		const double Now = ImGui::GetTime();
		if ( Now - State.LastScanTime < 3.0 ) return;
		State.LastScanTime = Now;
		State.EffectFiles = NFileSystem::ListFilesRecursive( Assets->GetRootDirectory(), { ".effect.json" } );
		State.TextureFiles = NFileSystem::ListFilesRecursive( Assets->GetRootDirectory(), { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".dds" } );
		State.SheetFiles = NFileSystem::ListFilesRecursive( Assets->GetRootDirectory(), { ".sheet.json" } );
	}

	// 確認を済ませた操作だけを実行する。読み込みの失敗では現在の文書を保つ。
	bool ExecuteAction( FEffectEditorState& State, FEffectEditorState::EPending Action, const FString& Path, bool* pOpen )
	{
		if ( Action == FEffectEditorState::EPending::Open ) return NDebugUI::OpenEffectFile( State, Path );
		if ( Action == FEffectEditorState::EPending::New ) NDebugUI::NewEffect( State );
		if ( Action == FEffectEditorState::EPending::Preset ) return NDebugUI::NewEffectFromPreset( State, Path );
		if ( Action == FEffectEditorState::EPending::Close )
		{
			NDebugUI::ReleaseEffectEditor( State );
			State.bInitialized = false;
			if ( pOpen != nullptr ) *pOpen = false;
		}
		return true;
	}

	// 新規・開く・閉じるを同じ未保存確認へ通す。
	void DrawUnsavedDialog( FEffectEditorState& State, bool* pOpen )
	{
		if ( State.bRequestUnsavedPopup )
		{
			State.bRequestUnsavedPopup = false;
			ImGui::OpenPopup( "Unsaved effect##EffectEditor" );
		}
		if ( !ImGui::BeginPopupModal( "Unsaved effect##EffectEditor", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) ) return;
		ImGui::TextUnformatted( "Save changes before continuing?" );
		if ( State.FilePath.IsEmpty() ) NDebugUI::InputString( "Save path", State.SaveAsPath );
		if ( ImGui::Button( "Save and continue" ) && NEffectEditorUI::CompletePendingAction( State, true, pOpen ) ) ImGui::CloseCurrentPopup();
		ImGui::SameLine();
		if ( ImGui::Button( "Discard changes" ) && NEffectEditorUI::CompletePendingAction( State, false, pOpen ) ) ImGui::CloseCurrentPopup();
		ImGui::SameLine();
		if ( ImGui::Button( "Cancel" ) )
		{
			State.Pending = FEffectEditorState::EPending::None;
			State.PendingPath.Empty();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	// メニューとファイル選択を描く。ウィンドウの閉じる操作と同じ確認経路を使う。
	void DrawMenu( FEffectEditorState& State, bool* pOpen )
	{
		// メニューを閉じてから開く、ファイル選択の要求。
		bool bOpenDialog = false;
		// メニューを閉じてから開く、保存先入力の要求。
		bool bSaveDialog = false;
		if ( ImGui::BeginMenuBar() )
		{
			if ( ImGui::BeginMenu( "File" ) )
			{
				if ( ImGui::MenuItem( "New" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::New, FString(), pOpen );
				if ( ImGui::MenuItem( "Open..." ) ) bOpenDialog = true;
				if ( ImGui::MenuItem( "Save", "Ctrl+S" ) )
				{
					if ( State.FilePath.IsEmpty() ) bSaveDialog = true;
					else NDebugUI::SaveEffectFile( State );
				}
				if ( ImGui::MenuItem( "Save as..." ) ) bSaveDialog = true;
				ImGui::Separator();
				if ( ImGui::MenuItem( "Close", nullptr, false, pOpen != nullptr ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Close, FString(), pOpen );
				ImGui::EndMenu();
			}
			if ( ImGui::BeginMenu( "Preset" ) )
			{
				if ( ImGui::MenuItem( "Campfire" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/campfire.effect.json", pOpen );
				if ( ImGui::MenuItem( "Sparks" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/sparks.effect.json", pOpen );
				if ( ImGui::MenuItem( "Aura" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/aura.effect.json", pOpen );
				if ( ImGui::MenuItem( "Ribbon trail" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/ribbon_trail.effect.json", pOpen );
				if ( ImGui::MenuItem( "Branch lightning" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/branch_lightning.effect.json", pOpen );
				if ( ImGui::MenuItem( "Target lightning" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/target_lightning.effect.json", pOpen );
				if ( ImGui::MenuItem( "Realistic lightning tree" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/tree_lightning.effect.json", pOpen );
				if ( ImGui::MenuItem( "Growing lightning tree" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/growing_tree_lightning.effect.json", pOpen );
				if ( ImGui::MenuItem( "Dissolve sigil" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/material_dissolve.effect.json", pOpen );
				if ( ImGui::MenuItem( "Material ribbon trail" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/material_ribbon_trail.effect.json", pOpen );
				if ( ImGui::MenuItem( "Material branch lightning" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/material_branch_lightning.effect.json", pOpen );
				if ( ImGui::MenuItem( "Chromatic ribbon" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/appearance_ribbon_spectrum.effect.json", pOpen );
				if ( ImGui::MenuItem( "Pulse lightning" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/appearance_pulse_lightning.effect.json", pOpen );
				if ( ImGui::MenuItem( "Flipbook sigil burst" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/flipbook_sigil_burst.effect.json", pOpen );
				if ( ImGui::MenuItem( "Flowing energy veil" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/uv_distortion_energy_veil.effect.json", pOpen );
				if ( ImGui::MenuItem( "Distorted ribbon trail" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/uv_distortion_ribbon.effect.json", pOpen );
				if ( ImGui::MenuItem( "Flowing lightning tree" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/uv_distortion_tree_lightning.effect.json", pOpen );
				if ( ImGui::MenuItem( "Mesh energy ring" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/mesh_energy_ring.effect.json", pOpen );
				if ( ImGui::MenuItem( "Mesh tilted veil" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/mesh_tilted_veil.effect.json", pOpen );
				if ( ImGui::MenuItem( "Force field wind" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/force_field_wind.effect.json", pOpen );
				if ( ImGui::MenuItem( "Gravity well" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/force_field_gravity_well.effect.json", pOpen );
				if ( ImGui::MenuItem( "Whirlwind" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/force_field_whirlwind.effect.json", pOpen );
				if ( ImGui::MenuItem( "Gust" ) ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Preset, "Effects/force_field_gust.effect.json", pOpen );
				ImGui::EndMenu();
			}
			if ( ImGui::BeginMenu( "Edit" ) )
			{
				if ( ImGui::MenuItem( "Undo", "Ctrl+Z", false, State.Document.CanUndo() ) ) State.Document.Undo();
				if ( ImGui::MenuItem( "Redo", "Ctrl+Y", false, State.Document.CanRedo() ) ) State.Document.Redo();
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}
		if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) && !ImGui::GetIO().WantTextInput )
		{
			if ( ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiKey_Z ) ) State.Document.Undo();
			if ( ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiKey_Y ) ) State.Document.Redo();
			if ( ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiKey_S ) )
			{
				if ( State.FilePath.IsEmpty() ) bSaveDialog = true;
				else NDebugUI::SaveEffectFile( State );
			}
		}
		if ( bOpenDialog ) ImGui::OpenPopup( "Open effect##EffectEditor" );
		if ( bSaveDialog )
		{
			if ( State.SaveAsPath.IsEmpty() ) State.SaveAsPath = State.FilePath.IsEmpty() ? FString( "Effects/NewEffect.effect.json" ) : State.FilePath;
			ImGui::OpenPopup( "Save effect as##EffectEditor" );
		}
		if ( ImGui::BeginPopupModal( "Open effect##EffectEditor", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			NDebugUI::FileCombo( "Effect file", State.OpenSelection, State.EffectFiles );
			NDebugUI::InputString( "Path", State.OpenSelection );
			ImGui::BeginDisabled( State.OpenSelection.IsEmpty() );
			if ( ImGui::Button( "Open" ) )
			{
				NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Open, State.OpenSelection, pOpen );
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) ) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if ( ImGui::BeginPopupModal( "Save effect as##EffectEditor", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
		{
			NDebugUI::InputString( "Path", State.SaveAsPath );
			if ( ImGui::Button( "Save" ) && NDebugUI::SaveEffectFile( State, State.SaveAsPath ) ) ImGui::CloseCurrentPopup();
			ImGui::SameLine();
			if ( ImGui::Button( "Cancel" ) ) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}
}

bool NDebugUI::OpenEffectFile( FEffectEditorState& State, const FString& AssetPath )
{
	// アセットのルートから、明示的に開いたファイルの場所を解決する。
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	// 読み込みと、その後に保存する際の共通のパス。
	const FString Path = NormalizePath( AssetPath );
	if ( Assets == nullptr || Path.IsEmpty() ) return false;
	// 別の利用者が古い資産を保持していても、開く操作ではディスク上の内容を読む。
	const TOptional<FString> FullPath = Assets->ResolvePath( Path );
	// 読み込みの成否が分かるまで、現在の文書へ触らないための一時領域。
	NJson::CJsonObject Json;
	// 読み取った新しい文書。
	FParticleEffectDefinition Definition;
	// 修復された値や、読み取れなかった項目の一覧。
	TArray<FString> Warnings;
	if ( !FullPath.IsSet() || !Json.LoadFromFile( FullPath.GetValue() ) || !FParticleEffectDefinition::FromJson( Json.GetRoot(), Definition, &Warnings ) )
	{
		SetStatus( State, "エフェクトを開けません: " + Path, true );
		return false;
	}
	State.Document.Reset( Definition );
	State.FilePath = Path;
	State.SaveAsPath = Path;
	State.SelectedEmitter = State.Document.Get().Emitters.IsEmpty() ? INDEX_NONE : 0;
	State.SelectedForceField = 0;
	State.bInitialized = true;
	State.bPreviewMotion = false;
	ResetPreviewTargetState( State );
	// Tree・mesh・力場を含む文書だけ、実viewport確定後に一度全体をcameraへ収める。
	NEffectEditorUI::SetPendingEffectCameraFocus( State, Definition );
	State.Camera.Distance = 6.0f;
	// 動的終点つきの雷があれば、試し再生の終点を有効にして、終点を含む範囲へ一度だけcameraを合わせる。
	NEffectEditorUI::ApplyInitialPreviewTarget( State );
	RestartEffectPreview( State );
	SetStatus( State, "エフェクトを開きました: " + Path );
	if ( !Warnings.IsEmpty() ) SetStatus( State, FString::Printf( "エフェクトを開きました: %s（読み込み時の警告 %d 件: %s）", *Path, Warnings.Num(), *Warnings[0] ) );
	return true;
}

void NDebugUI::NewEffect( FEffectEditorState& State )
{
	State.Document.Reset( FParticleEffectDefinition() );
	NParticleEdit::SetProperties( State.Document, "NewEffect", 0.0f, true, 1u );
	State.SelectedEmitter = NParticleEdit::AddEmitter( State.Document );
	State.SelectedForceField = 0;
	State.FilePath.Empty();
	State.SaveAsPath = "Effects/NewEffect.effect.json";
	State.bInitialized = true;
	State.bPreviewMotion = false;
	ResetPreviewTargetState( State );
	State.bTreeCameraFocusPending = false;
	State.bEffectGeometryCameraFocusPending = false;
	State.Camera = FEditorCamera();
	State.Camera.Distance = 6.0f;
	RestartEffectPreview( State );
	SetStatus( State, "新しいエフェクトを作成しました" );
}

bool NDebugUI::NewEffectFromPreset( FEffectEditorState& State, const FString& PresetPath )
{
	// ファイルを読み取れた場合だけ、現在の文書を独立したコピーへ置き換える。
	const FString Path = NormalizePath( PresetPath );
	if ( !OpenEffectFile( State, Path ) ) return false;
	// プリセットの内容は保持し、元のファイルを保存先に使わない。
	const FParticleEffectDefinition Copy = State.Document.Get();
	State.Document.Reset( FParticleEffectDefinition() );
	State.Document.BeginEdit() = Copy;
	State.Document.EndEditSession();
	State.FilePath.Empty();
	// 定義ファイルの拡張子がある場合だけ外し、コピー専用の保存先を提案する。
	const FString Base = Path.EndsWith( ".effect.json", ESearchCase::IgnoreCase ) ? Path.Left( Path.Len() - 12 ) : Path;
	State.SaveAsPath = Base + "_copy.effect.json";
	State.bPreviewMotion = false;
	ResetPreviewTargetState( State );
	NEffectEditorUI::ApplyInitialPreviewTarget( State );
	// 力場がないRibbonだけ、生成位置の円運動で従来の軌跡previewを見せる。
	for ( const FParticleEmitterDefinition& Emitter : Copy.Emitters )
	{
		if ( Emitter.Path.Kind != FEffectPathDefinition::EKind::Ribbon || !Emitter.ForceFields.IsEmpty() ) continue;
		State.bPreviewMotion = true;
		break;
	}
	RestartEffectPreview( State );
	SetStatus( State, "プリセットを新しいエフェクトへコピーしました: " + Path );
	return true;
}

bool NDebugUI::SaveEffectFile( FEffectEditorState& State, const FString& AssetPath )
{
	// 保存の成否を確かめるまで、文書の保存済み状態は更新しない。
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	// 指定を省略したときは、現在開いているファイルへ保存する。
	FString Path = NormalizePath( AssetPath.IsEmpty() ? State.FilePath : AssetPath );
	State.Issues = State.Document.Get().Validate();
	if ( Assets == nullptr || Path.IsEmpty() || FParticleEffectDefinition::HasErrors( State.Issues ) )
	{
		SetStatus( State, "保存先、アセット管理、定義のエラーを確認してください", true );
		return false;
	}
	for ( int32 Index = 0; Index < State.Document.Get().Emitters.Num(); ++Index )
	{
		// 四角形で実際に使うシートだけ保存直前に同期検証する。
		const FParticleEmitterDefinition& Emitter = State.Document.Get().Emitters[Index];
		if ( !Emitter.Flipbook.bEnabled || Emitter.Path.Kind != FEffectPathDefinition::EKind::None ) continue;
		const TArray<FString> AssetIssues = NEffectFlipbook::ValidateAssets( Emitter.Flipbook, *Assets );
		if ( !AssetIssues.IsEmpty() )
		{
			SetStatus( State, FString::Printf( "Emitter %d: %s", Index + 1, *AssetIssues[0] ), true );
			return false;
		}
	}
	// 保存対象のemitterを順に確認する。
	for ( int32 Index = 0; Index < State.Document.Get().Emitters.Num(); ++Index )
	{
		// 材質が有効で歪みを使う対応経路のnoiseを保存時に同期確認する。
		const FParticleEmitterDefinition& Emitter = State.Document.Get().Emitters[Index];
		// このemitterの歪みnoise定義。
		const FEffectUVDistortionDefinition& Distortion = Emitter.Material.Distortion;
		// 現在のpath kindでnoise検証が必要か。
		const bool bDistortionPath = Emitter.Path.Kind == FEffectPathDefinition::EKind::None || Emitter.Path.Kind == FEffectPathDefinition::EKind::Ribbon || Emitter.Path.Kind == FEffectPathDefinition::EKind::Lightning || Emitter.Path.Kind == FEffectPathDefinition::EKind::TreeLightning;
		if ( !Distortion.bEnabled || !Emitter.Material.bEnabled || !bDistortionPath ) continue;
		// 既存AssetManagerから同期で取得し、保存後すぐ使える画像か確認する。
		const TSharedPtr<CTexture> NoiseTexture = Assets->LoadTexture( Distortion.Texture, false );
		if ( !NoiseTexture.IsValid() || !NoiseTexture->IsReady() )
		{
			SetStatus( State, FString::Printf( "Emitter %d: UV歪み用ノイズ画像を読み込めないか、まだ使用できません: %s", Index + 1, *Distortion.Texture ), true );
			return false;
		}
	}
	if ( !Path.EndsWith( ".effect.json", ESearchCase::IgnoreCase ) ) Path += ".effect.json";
	if ( !Assets->SaveEffect( Path, State.Document.Get() ) )
	{
		SetStatus( State, "エフェクトを保存できません: " + Path, true );
		return false;
	}
	State.FilePath = Path;
	State.SaveAsPath = Path;
	State.Document.MarkSaved();
	State.LastScanTime = -100.0;
	++State.SaveSerial;
	SetStatus( State, "エフェクトを保存しました: " + Path );
	return true;
}

FTransform NEffectEditorUI::GetPreviewEmitterTransform( const FEffectEditorState& State )
{
	if ( !State.bPreviewMotion ) return FTransform::Identity;
	return FTransform( FVector3( std::cos( State.PreviewMotionTime ) * 1.2f, 1.0f, std::sin( State.PreviewMotionTime ) * 1.2f ) );
}

void NDebugUI::RestartEffectPreview( FEffectEditorState& State )
{
	State.Playback.Reset();
	State.PlaybackRevision = ~0u;
	State.FinishedTime = 0.0f;
	State.PreviewMotionTime = 0.0f;
}

void NDebugUI::UpdateEffectEditor( FEffectEditorState& State, float32 DeltaTime )
{
	if ( State.PlaybackRevision != State.Document.GetRevision() )
	{
		State.Playback.Reset();
		State.FinishedTime = 0.0f;
		State.PreviewMotionTime = 0.0f;
		State.PlaybackRevision = State.Document.GetRevision();
		State.Issues = State.Document.Get().Validate();
		// 選択が削除・取り消しで範囲外になった場合だけ補正する。
		const int32 NumEmitters = State.Document.Get().Emitters.Num();
		State.SelectedEmitter = NumEmitters > 0 ? NMath::Clamp( State.SelectedEmitter, 0, NumEmitters - 1 ) : INDEX_NONE;
		// 未保存の変更を、共有アセットや既に作った再生へ書き込まない。
		const TSharedPtr<const FParticleEffectDefinition> Snapshot = MakeShared<FParticleEffectDefinition>( State.Document.Get() );
		if ( !FParticleEffectDefinition::HasErrors( State.Issues ) ) State.Playback = NEffect::CreateParticlePlayback( Snapshot, GetAppSubsystem<CAssetManager>(), FEffectPlayParams(), FTransform::Identity, "editor" );
	}
	if ( State.Playback.IsValid() )
	{
		// 選択中のエミッターに関係なく、文書内のどれかの雷が終点を使うか。
		const bool bPathUsesTarget = NEffectEditorUI::FindTargetPathEmitter( State.Document.Get() ) != nullptr;
		const FVector3& Target = State.PreviewTargetPosition;
		if ( bPathUsesTarget && State.bPreviewTargetEnabled && NMath::IsFinite( Target.x ) && NMath::IsFinite( Target.y ) && NMath::IsFinite( Target.z ) ) State.Playback->SetTargetPosition( Target );
		else State.Playback->ClearTargetPosition();
	}
	if ( !State.bPlaying || !State.Playback.IsValid() || !NMath::IsFinite( DeltaTime ) || DeltaTime <= 0.0f ) return;
	// 長く止まっていた直後も、編集画面の粒子が一度に遠くへ飛ばないようにする。
	const float32 Step = NMath::Clamp( DeltaTime, 0.0f, 0.1f ) * ( NMath::IsFinite( State.PreviewSpeed ) ? NMath::Clamp( State.PreviewSpeed, 0.0f, 4.0f ) : 1.0f );
	if ( State.bPreviewMotion )
	{
		// 一周で折り返し、長時間開いたままでも位置の計算精度を保つ。
		constexpr float32 CirclePeriod = 6.283185307f;
		State.PreviewMotionTime = std::fmod( State.PreviewMotionTime + Step, CirclePeriod );
		State.Playback->SetTransform( NEffectEditorUI::GetPreviewEmitterTransform( State ) );
	}
	else State.Playback->SetTransform( FTransform::Identity );
	State.Playback->Update( Step );
	if ( State.bRepeatPreview && State.Playback->IsFinished() )
	{
		State.FinishedTime += Step;
		if ( State.FinishedTime >= 0.75f ) RestartEffectPreview( State );
	}
}

void NDebugUI::ReleaseEffectEditor( FEffectEditorState& State )
{
	RestartEffectPreview( State );
	ResetPreviewTargetState( State );
	State.Surface.Release();
	State.FlipbookSheetCache.Reset();
	State.FlipbookSheetCachePath.Empty();
	State.bFlipbookSheetLoadAttempted = false;
	State.FlipbookValidationCacheKey = ~0ull;
	State.FlipbookValidationIssues.Empty();
	State.UVDistortionTextureCache.Reset();
	State.UVDistortionTextureCachePath.Empty();
	State.bUVDistortionTextureLoadAttempted = false;
}

bool NEffectEditorUI::RequestAction( FEffectEditorState& State, FEffectEditorState::EPending Action, const FString& Path, bool* pOpen )
{
	if ( State.Document.IsDirty() )
	{
		State.Pending = Action;
		State.PendingPath = Path;
		State.bRequestUnsavedPopup = true;
		return false;
	}
	return ExecuteAction( State, Action, Path, pOpen );
}

bool NEffectEditorUI::CompletePendingAction( FEffectEditorState& State, bool bSave, bool* pOpen )
{
	if ( State.Pending == FEffectEditorState::EPending::None ) return false;
	if ( bSave && !NDebugUI::SaveEffectFile( State, State.FilePath.IsEmpty() ? State.SaveAsPath : State.FilePath ) ) return false;
	if ( !ExecuteAction( State, State.Pending, State.PendingPath, pOpen ) ) return false;
	State.Pending = FEffectEditorState::EPending::None;
	State.PendingPath.Empty();
	State.bRequestUnsavedPopup = false;
	return true;
}

void NDebugUI::DrawEffectEditor( FEffectEditorState& State, bool* pOpen )
{
	if ( !State.bInitialized ) NewEffect( State );
	RescanFiles( State );
	ImGui::SetNextWindowSize( ImVec2( 1120.0f, 660.0f ), ImGuiCond_FirstUseEver );
	// ×を押しても、未保存確認が済むまで実際の表示フラグは下げない。
	bool bWindowOpen = true;
	// 折り畳み時にも、閉じる要求の確認だけは処理する。
	const bool bExpanded = ImGui::Begin( "Effect Editor", pOpen != nullptr ? &bWindowOpen : nullptr, ImGuiWindowFlags_MenuBar );
	if ( !bWindowOpen ) NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Close, FString(), pOpen );
	if ( bExpanded && ( pOpen == nullptr || *pOpen ) )
	{
		DrawMenu( State, pOpen );
		if ( pOpen == nullptr || *pOpen )
		{
			ImGui::Text( "%s%s", State.FilePath.IsEmpty() ? "(new effect)" : UI_TEXT( State.FilePath ), State.Document.IsDirty() ? " *" : "" );
			ImGui::TextDisabled( "Supported: sprites, ribbon trails, branch lightning, tree lightning and mesh particles" );
			// 設定を十分な幅で読みつつ、小さい画面ではプレビュー領域も残す。
			const float LeftWidth = NMath::Min( 460.0f, ImGui::GetContentRegionAvail().x * 0.46f );
			ImGui::BeginChild( "EffectDefinition", ImVec2( LeftWidth, -28.0f ), ImGuiChildFlags_Borders );
			NEffectEditorUI::DrawDefinitionPanel( State );
			ImGui::EndChild();
			ImGui::SameLine();
			UpdateEffectEditor( State, ImGui::GetIO().DeltaTime );
			ImGui::BeginChild( "EffectPreview", ImVec2( 0.0f, -28.0f ), ImGuiChildFlags_Borders );
			NEffectEditorUI::DrawPreview( State );
			ImGui::EndChild();
			if ( State.bStatusIsError ) NDebugUI::PushWarningColor();
			ImGui::TextUnformatted( UI_TEXT( State.StatusMessage ) );
			if ( State.bStatusIsError ) NDebugUI::PopWarningColor();
		}
	}
	DrawUnsavedDialog( State, pOpen );
	if ( !ImGui::IsAnyItemActive() ) State.Document.EndEditSession();
	ImGui::End();
}
