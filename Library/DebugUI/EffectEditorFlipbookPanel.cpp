// SPDX-License-Identifier: NOASSERTION
#include "EffectEditorInternal.h"

#include <cmath>

#include "Library/DebugUI/EditorCommon.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Effect/EffectFlipbookFrames.h"
#include "Library/Framework/Engine/Engine.h"

namespace
{
	// 入力欄を離れたときに同じ文書の履歴区切りを閉じる。
	bool TrackFlipbookInputEnd( bool bChanged, TParticleEffectDocument& Document )
	{
		if ( ImGui::IsItemDeactivatedAfterEdit() ) Document.EndEditSession();
		return bChanged;
	}

	// 選択番号とFlipbook定義を組み合わせ、資産検証の使い回し範囲を決める。
	uint64 MakeFlipbookValidationKey( int32 EmitterIndex, const FEffectFlipbookDefinition& Definition )
	{
		// emitter番号を定義hashへ混ぜる初期値。
		const uint64 IndexHash = static_cast<uint64>( static_cast<uint32>( EmitterIndex + 1 ) ) * 0x9E3779B185EBCA87ull;
		return Definition.ComputeHash() ^ IndexHash;
	}
}

bool NEffectEditorUI::DrawFlipbookPanel( FEffectEditorState& State, FParticleEmitterDefinition& Emitter )
{
	ImGui::SeparatorText( "Flipbook" );
	// 変更を選択中emitterへまとめる状態。
	bool bChanged = false;
	// Flipbookだけを既定値へ戻す。
	if ( ImGui::SmallButton( "Reset flipbook" ) )
	{
		Emitter.Flipbook = FEffectFlipbookDefinition();
		State.FlipbookValidationCacheKey = ~0ull;
		State.Document.EndEditSession();
		bChanged = true;
	}
	// 壊れたJSON由来の型や範囲外値はImGuiへ渡さず、修復手段を示す。
	const TArray<FString> DefinitionIssues = Emitter.Flipbook.Validate();
	if ( !DefinitionIssues.IsEmpty() ) ImGui::TextColored( ImVec4( 1.0f, 0.65f, 0.3f, 1.0f ), "Flipbook settings are invalid; reset or replace the invalid value." );
	// 有効/無効の切替でもシートとクリップ名を保持する。
	bChanged |= TrackFlipbookInputEnd( ImGui::Checkbox( "Enable flipbook", &Emitter.Flipbook.bEnabled ), State.Document );
	// .sheet.json候補と任意の相対パスを同じ値へ入力する。
	bChanged |= TrackFlipbookInputEnd( NDebugUI::FileCombo( "Sheet file", Emitter.Flipbook.Sheet, State.SheetFiles, "(choose sheet)" ), State.Document );
	bChanged |= TrackFlipbookInputEnd( NDebugUI::InputString( "Sheet path", Emitter.Flipbook.Sheet ), State.Document );
	// 無効・非四角形では未使用assetを読まず、名前の手入力だけを許可する。
	const bool bCanBrowseSheet = Emitter.Flipbook.bEnabled && Emitter.Path.Kind == FEffectPathDefinition::EKind::None;
	if ( bCanBrowseSheet && ImGui::BeginCombo( "Clip", Emitter.Flipbook.Clip.IsEmpty() ? "(first clip)" : UI_TEXT( Emitter.Flipbook.Clip ) ) )
	{
		if ( ImGui::Selectable( "(first clip)", Emitter.Flipbook.Clip.IsEmpty() ) )
		{
			Emitter.Flipbook.Clip.Empty();
			bChanged = true;
		}
		// 選択中シートの読み込みをパスごとに一度だけ試す。
		CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
		if ( Emitter.Flipbook.Sheet != State.FlipbookSheetCachePath )
		{
			State.FlipbookSheetCachePath = Emitter.Flipbook.Sheet;
			State.bFlipbookSheetLoadAttempted = false;
			State.FlipbookSheetCache.Reset();
		}
		if ( Assets != nullptr && !Emitter.Flipbook.Sheet.IsEmpty() && !State.bFlipbookSheetLoadAttempted )
		{
			State.bFlipbookSheetLoadAttempted = true;
			State.FlipbookSheetCache = Assets->LoadSpriteSheet( Emitter.Flipbook.Sheet, false );
		}
		// 読めたシートだけ定義済みクリップを並べる。
		if ( State.FlipbookSheetCache.IsValid() && State.FlipbookSheetCache->IsReady() )
		{
			const FSpriteSheetDefinition& SheetDefinition = State.FlipbookSheetCache->GetDefinition();
			for ( const FSpriteClip& Clip : SheetDefinition.Clips )
			{
				if ( ImGui::Selectable( UI_TEXT( Clip.Name ), Emitter.Flipbook.Clip == Clip.Name ) )
				{
					Emitter.Flipbook.Clip = Clip.Name;
					bChanged = true;
				}
			}
		}
		ImGui::EndCombo();
	}
	else if ( !bCanBrowseSheet ) ImGui::TextDisabled( "Enable a quad flipbook to browse its sheet clips." );
	// クリップ一覧が読めない場合も手入力で編集を続けられる。
	bChanged |= TrackFlipbookInputEnd( NDebugUI::InputString( "Clip name", Emitter.Flipbook.Clip ), State.Document );
	// enumの不正値をComboへ渡さず、明示的に修復を促す。
	const bool bValidMode = Emitter.Flipbook.TimeMode == FEffectFlipbookDefinition::ETimeMode::Lifetime || Emitter.Flipbook.TimeMode == FEffectFlipbookDefinition::ETimeMode::Seconds;
	if ( !bValidMode ) ImGui::TextColored( ImVec4( 1.0f, 0.65f, 0.3f, 1.0f ), "Time mode is invalid; reset flipbook to repair it." );
	else
	{
		// ImGuiへ渡す時計種別は検証済みの二択。
		int32 TimeMode = Emitter.Flipbook.TimeMode == FEffectFlipbookDefinition::ETimeMode::Seconds ? 1 : 0;
		static const char* TimeModeNames[] = { "Lifetime", "Seconds" };
		if ( ImGui::Combo( "Time mode", &TimeMode, TimeModeNames, 2 ) )
		{
			Emitter.Flipbook.TimeMode = TimeMode == 1 ? FEffectFlipbookDefinition::ETimeMode::Seconds : FEffectFlipbookDefinition::ETimeMode::Lifetime;
			bChanged = true;
		}
	}
	if ( Emitter.Flipbook.TimeMode == FEffectFlipbookDefinition::ETimeMode::Seconds )
	{
		// 不正なfloatをDragFloatへ渡すとImGuiのassertや破損を招くため入力を止める。
		if ( std::isfinite( Emitter.Flipbook.Speed ) && Emitter.Flipbook.Speed >= 0.0f && Emitter.Flipbook.Speed <= 128.0f ) bChanged |= TrackFlipbookInputEnd( ImGui::DragFloat( "Seconds speed", &Emitter.Flipbook.Speed, 0.01f, 0.0f, 128.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ), State.Document );
		else ImGui::TextColored( ImVec4( 1.0f, 0.65f, 0.3f, 1.0f ), "Seconds speed is invalid; reset flipbook to repair it." );
	}
	ImGui::TextWrapped( "Lifetime plays the clip order once from birth to end, then holds the last frame. Seconds follows the clip loop mode at age times speed." );
	// 有効中はシート画像をImageより優先し、無効に戻すとImageへ戻る。
	ImGui::TextWrapped( "For quad particles, flipbook uses the sheet texture instead of Image. Disabling it restores Image." );
	if ( Emitter.Path.Kind != FEffectPathDefinition::EKind::None ) ImGui::TextColored( ImVec4( 1.0f, 0.65f, 0.3f, 1.0f ), "Flipbook currently applies to quad particles only; this path keeps the setting but ignores it." );
	// enabledで四角形のときだけ資産とクリップを検証する。
	if ( Emitter.Flipbook.bEnabled && Emitter.Path.Kind == FEffectPathDefinition::EKind::None )
	{
		// 一覧の選択やファイル差替え後に明示的に読込をやり直す。
		if ( ImGui::SmallButton( "Recheck flipbook sheet" ) )
		{
			State.FlipbookSheetCachePath.Empty();
			State.FlipbookSheetCache.Reset();
			State.bFlipbookSheetLoadAttempted = false;
			State.FlipbookValidationCacheKey = ~0ull;
		}
		// 同じemitter定義を描く間は同期検証結果を再利用する。
		const uint64 ValidationKey = MakeFlipbookValidationKey( State.SelectedEmitter, Emitter.Flipbook );
		if ( State.FlipbookValidationCacheKey != ValidationKey )
		{
			CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
			if ( Assets != nullptr )
			{
				State.FlipbookValidationIssues = NEffectFlipbook::ValidateAssets( Emitter.Flipbook, *Assets );
				State.FlipbookValidationCacheKey = ValidationKey;
			}
		}
		if ( GetAppSubsystem<CAssetManager>() == nullptr ) ImGui::TextDisabled( "Asset manager unavailable; manual editing remains available." );
		else if ( !State.FlipbookValidationIssues.IsEmpty() ) ImGui::TextColored( ImVec4( 1.0f, 0.65f, 0.3f, 1.0f ), "Asset check: %s", UI_TEXT( State.FlipbookValidationIssues[0] ) );
		else ImGui::TextDisabled( "Sheet, clip, texture and frame timing are valid." );
	}
	return bChanged;
}
