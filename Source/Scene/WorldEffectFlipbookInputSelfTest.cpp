// SPDX-License-Identifier: NOASSERTION
#include "WorldSelfTestSupport.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/EffectEditorInternal.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Platform/FileSystem.h"
#include "VendorModules/ImGui/imgui_internal.h"

#include <cmath>
#include <limits>

namespace
{
	// 実際のFlipbook panelへ文字入力し、別のImGui context内で入力状態を調べる。
	class FFlipbookInputContext
	{
	public:
		// 独立したImGui contextと既定fontを用意する。
		FFlipbookInputContext() : Previous( ImGui::GetCurrentContext() ), Context( ImGui::CreateContext() )
		{
			ImGui::SetCurrentContext( Context );
			ImGuiIO& Io = ImGui::GetIO();
			Io.DisplaySize = ImVec2( 1400.0f, 900.0f );
			Io.DeltaTime = 1.0f / 60.0f;
			Io.IniFilename = nullptr;
			Io.LogFilename = nullptr;
			Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			Io.ConfigInputTrickleEventQueue = false;
			Io.Fonts->AddFontDefaultBitmap();
			bFontsReady = Io.Fonts->Build();
		}

		// 独立contextを破棄し、呼出し前のcontextを戻す。
		~FFlipbookInputContext()
		{
			ImGui::DestroyContext( Context );
			ImGui::SetCurrentContext( Previous );
		}
		// ImGui contextは共有できないため複製を禁止する。
		FFlipbookInputContext( const FFlipbookInputContext& ) = delete;
		// ImGui contextの代入を禁止する。
		FFlipbookInputContext& operator=( const FFlipbookInputContext& ) = delete;

		// panelを一度描き、変更を通常のemitter履歴sessionへ反映する。
		bool Frame( FEffectEditorState& State )
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos( ImVec2( 0.0f, 0.0f ), ImGuiCond_Always );
			ImGui::SetNextWindowSize( ImVec2( 1000.0f, 800.0f ), ImGuiCond_Always );
			if ( ImGui::Begin( "EffectFlipbookInputSelfTest", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize ) )
			{
				Window = ImGui::GetCurrentWindow();
				if ( State.Document.Get().Emitters.IsValidIndex( 0 ) )
				{
					// 本番panelと同じく一時コピーを編集してから文書へ戻す。
					FParticleEmitterDefinition Emitter = State.Document.Get().Emitters[0];
					if ( NEffectEditorUI::DrawFlipbookPanel( State, Emitter ) ) NParticleEdit::SetEmitter( State.Document, 0, Emitter, "emitter-settings" );
				}
				if ( !ImGui::IsAnyItemActive() ) State.Document.EndEditSession();
			}
			ImGui::End();
			ImGui::Render();
			bFramesBalanced = bFramesBalanced && !Context->WithinFrameScope && Context->CurrentWindowStack.Size == 0;
			return bFramesBalanced;
		}

		// 指定keyを押して離し、両frameでpanelを更新する。
		void Tap( FEffectEditorState& State, ImGuiKey Key )
		{
			ImGui::GetIO().AddKeyEvent( Key, true );
			Frame( State );
			ImGui::GetIO().AddKeyEvent( Key, false );
			Frame( State );
		}

		// tab巡回で指定項目へkeyboard focusを移す。
		bool TabTo( FEffectEditorState& State, ImGuiID Id )
		{
			for ( int32 Attempt = 0; Attempt < 256; ++Attempt )
			{
				if ( Context->NavId == Id ) return true;
				Tap( State, ImGuiKey_Tab );
			}
			return Context->NavId == Id;
		}

		// 最後に描いたwindowから通常項目のIDを得る。
		ImGuiID ItemId( const char* Label ) const { return Window->GetID( Label ); }

		// 数値dragをtext入力へ切り替え、指定値を入力する。
		bool ReplaceNumeric( FEffectEditorState& State, ImGuiID Id, const char* Text )
		{
			Tap( State, ImGuiKey_Enter );
			const bool bActivated = Context->TempInputId == Id && Context->ActiveId == Id;
			ImGui::GetIO().AddKeyEvent( ImGuiMod_Ctrl, true );
			ImGui::GetIO().AddKeyEvent( ImGuiKey_A, true );
			Frame( State );
			ImGui::GetIO().AddKeyEvent( ImGuiKey_A, false );
			ImGui::GetIO().AddKeyEvent( ImGuiMod_Ctrl, false );
			ImGui::GetIO().AddInputCharactersUTF8( Text );
			Frame( State );
			return bActivated && Context->TempInputId == Id && Context->ActiveId == Id;
		}

		// ImGuiの全frameでwindowとframe scopeが閉じたかを返す。
		bool FramesBalanced() const { return bFramesBalanced; }
		// font atlasを構築できたかを返す。
		bool IsReady() const { return bFontsReady; }

	private:
		// このharness開始前に選ばれていたImGui context。
		ImGuiContext* Previous = nullptr;
		// 入力確認が所有するImGui context。
		ImGuiContext* Context = nullptr;
		// widget IDとkeyboard focusの参照元。
		ImGuiWindow* Window = nullptr;
		// 独立font atlasが使える状態か。
		bool bFontsReady = false;
		// 全描画frameのBegin/EndとNewFrame/Renderが釣り合ったか。
		bool bFramesBalanced = true;
	};

	// 固定されたFlipbook fieldの編集・履歴・無効値修復を入力する。
	void TestFlipbookInputValues( NAssetTest::FChecker& C, FFlipbookInputContext& Input )
	{
		C.Group = "effect-flipbook-input-values";
		// 元から保ちたい画像・材質・Appearance設定。
		FParticleEmitterDefinition OriginalEmitter;
		OriginalEmitter.Texture = "Effects/material_dissolve.tga";
		OriginalEmitter.Material.UVScroll = FVector2( 0.25f, -0.5f );
		OriginalEmitter.Appearance.Size.Keys.Add( { 0.0f, 0.5f } );
		// 入力対象を持つ文書。
		FParticleEffectDefinition Definition;
		Definition.Emitters.Add( OriginalEmitter );
		// panel描画とundo対象を所有するstate。
		FEffectEditorState State;
		State.Document.Reset( Definition );
		State.Document.MarkSaved();
		Input.Frame( State );
		// checkboxをkeyboardで有効化し、dirtyと単独Undo/Redoを確認する。
		C.Check( Input.TabTo( State, Input.ItemId( "Enable flipbook" ) ), "Flipbook enable checkboxへkeyboard focusする" );
		Input.Tap( State, ImGuiKey_Enter );
		C.Check( State.Document.Get().Emitters[0].Flipbook.bEnabled && State.Document.IsDirty(), "checkbox入力で有効化し文書をdirtyにする" );
		C.Check( State.Document.GetNumUndo() == 1 && State.Document.Undo() && !State.Document.Get().Emitters[0].Flipbook.bEnabled, "checkboxの変更だけをUndoする" );
		C.Check( State.Document.Redo() && State.Document.Get().Emitters[0].Flipbook.bEnabled, "checkbox変更をRedoする" );
		// Modeの選択はCombo入力でSecondsへ変える。
		const ImGuiID ModeId = Input.ItemId( "Time mode" );
		C.Check( Input.TabTo( State, ModeId ), "Time modeへkeyboard focusする" );
		Input.Tap( State, ImGuiKey_Enter );
		Input.Tap( State, ImGuiKey_DownArrow );
		Input.Tap( State, ImGuiKey_Enter );
		C.Check( State.Document.Get().Emitters[0].Flipbook.TimeMode == FEffectFlipbookDefinition::ETimeMode::Seconds, "Time mode ComboでSecondsを選ぶ" );
		// Speedを文字入力し、入力欄を離れると履歴が分かれることを確認する。
		const ImGuiID SpeedId = Input.ItemId( "Seconds speed" );
		C.Check( Input.TabTo( State, SpeedId ), "Seconds speedへkeyboard focusする" );
		const bool bTyped = Input.ReplaceNumeric( State, SpeedId, "2.75" );
		Input.Tap( State, ImGuiKey_Tab );
		C.Check( bTyped && NMath::Abs( State.Document.Get().Emitters[0].Flipbook.Speed - 2.75f ) < 0.002f, "Seconds speedを文字入力する" );
		C.Check( State.Document.GetNumUndo() >= 3, "enable・mode・speed入力に履歴がある" );
		C.Check( State.Document.Undo() && NMath::Abs( State.Document.Get().Emitters[0].Flipbook.Speed - 1.0f ) < 0.002f, "speed入力だけをUndoする" );
		C.Check( State.Document.Redo() && NMath::Abs( State.Document.Get().Emitters[0].Flipbook.Speed - 2.75f ) < 0.002f, "speed文字入力をRedoする" );
		// disableしてもSheet/Clipと他のemitter設定を保つ。
		FParticleEmitterDefinition Configured = State.Document.Get().Emitters[0];
		Configured.Flipbook.Sheet = "Effects/flipbook_sigil_burst.sheet.json";
		Configured.Flipbook.Clip = "Burst";
		NParticleEdit::SetEmitter( State.Document, 0, Configured, "emitter-settings" );
		Input.Frame( State );
		C.Check( Input.TabTo( State, Input.ItemId( "Enable flipbook" ) ), "disable試験でcheckboxへ移る" );
		Input.Tap( State, ImGuiKey_Enter );
		const FParticleEmitterDefinition& Disabled = State.Document.Get().Emitters[0];
		C.Check( !Disabled.Flipbook.bEnabled && Disabled.Flipbook.Sheet == Configured.Flipbook.Sheet && Disabled.Flipbook.Clip == "Burst", "disableしてもSheetとClipを保持する" );
		C.Check( Disabled.Texture == OriginalEmitter.Texture && Disabled.Material.UVScroll == OriginalEmitter.Material.UVScroll && Disabled.Appearance.Size.Keys.Num() == 1 && Disabled.Appearance.Size.Keys[0].Time == 0.0f && Disabled.Appearance.Size.Keys[0].Value == 0.5f, "Flipbook切替がImage・Material・Appearanceを変えない" );
		// 不正enumとNaNを含む描画後、Resetだけで修復しFlipbook外を維持する。
		FParticleEmitterDefinition Invalid = Disabled;
		Invalid.Flipbook.TimeMode = static_cast<FEffectFlipbookDefinition::ETimeMode>( 0xFFu );
		Invalid.Flipbook.Speed = TNumericLimits<float32>::QuietNaN();
		NParticleEdit::SetEmitter( State.Document, 0, Invalid, "emitter-settings" );
		Input.Frame( State );
		C.Check( !State.Document.Get().Emitters[0].Flipbook.Validate().IsEmpty() && Input.FramesBalanced(), "不正modeとNaN speedを渡してもassertせず描画する" );
		C.Check( Input.TabTo( State, Input.ItemId( "Reset flipbook" ) ), "invalid FlipbookをResetへkeyboard移動する" );
		Input.Tap( State, ImGuiKey_Enter );
		const FParticleEmitterDefinition& Reset = State.Document.Get().Emitters[0];
		C.Check( Reset.Flipbook.IsDefault() && Reset.Flipbook.Validate().IsEmpty(), "Resetで不正modeとNaN speedを修復する" );
		C.Check( Reset.Texture == OriginalEmitter.Texture && Reset.Material.UVScroll == OriginalEmitter.Material.UVScroll && Reset.Appearance.Size.Keys.Num() == 1 && Reset.Appearance.Size.Keys[0].Time == 0.0f && Reset.Appearance.Size.Keys[0].Value == 0.5f, "Flipbook ResetはImage・Material・Appearanceを保持する" );
	}

	// presetを新規コピーとして開き、別名保存の値と履歴を確かめる。
	void TestFlipbookPresetCopySave( NAssetTest::FChecker& C )
	{
		C.Group = "effect-flipbook-input-save-copy";
		// アプリから提供されるasset manager。
		CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
		C.Check( Assets != nullptr, "preset copy保存試験にasset managerがある" );
		if ( Assets == nullptr ) return;
		// 組み込みpresetの絶対パス。
		const TOptional<FString> PresetFullPath = Assets->ResolvePath( "Effects/flipbook_sigil_burst.effect.json" );
		// 保存先となる試験専用effectの相対パス。
		const FString CopyPath = "Effects/selftest_flipbook_copy.effect.json";
		// 元preset JSONを読み、保存前の値を保つ。
		NJson::CJsonObject PresetBeforeJson;
		const bool bReadPresetBefore = PresetFullPath.IsSet() && PresetBeforeJson.LoadFromFile( PresetFullPath.GetValue() );
		// 原本比較用に保存前JSON valueを複写する。
		const NlohmannJson PresetBefore = bReadPresetBefore ? PresetBeforeJson.GetRoot() : NlohmannJson();
		C.Check( bReadPresetBefore, "組み込みpreset原本JSONを読める" );
		if ( !bReadPresetBefore ) return;
		// preset JSONを比較対象となる定義へ読む。
		FParticleEffectDefinition PresetDefinition;
		const bool bParsedPreset = FParticleEffectDefinition::FromJson( PresetBefore, PresetDefinition );
		C.Check( bParsedPreset && !PresetDefinition.Emitters.IsEmpty(), "presetをemitter付き定義へ読める" );
		if ( !bParsedPreset || PresetDefinition.Emitters.IsEmpty() ) return;
		// 実際のEditor APIが作成する未保存preset copy。
		FEffectEditorState State;
		const bool bCreatedCopy = NDebugUI::NewEffectFromPreset( State, "Effects/flipbook_sigil_burst.effect.json" );
		C.Check( bCreatedCopy && State.Document.IsDirty() && State.FilePath.IsEmpty(), "NewEffectFromPresetがdirtyな未保存copyを作る" );
		if ( !bCreatedCopy ) return;
		// copyの内容がpresetのFlipbook設定を保つ。
		const FParticleEffectDefinition& CopyDefinition = State.Document.Get();
		C.Check( CopyDefinition.Emitters.Num() == PresetDefinition.Emitters.Num() && CopyDefinition.ComputeHash() == PresetDefinition.ComputeHash(), "未保存copyの定義hashがpresetと一致する" );
		C.Check( CopyDefinition.Emitters.IsValidIndex( 0 ) && CopyDefinition.Emitters[0].Flipbook.ComputeHash() == PresetDefinition.Emitters[0].Flipbook.ComputeHash(), "未保存copyのFlipbook hashがpresetと一致する" );
		// 別名保存の戻り値と文書状態を確かめる。
		const bool bSaved = NDebugUI::SaveEffectFile( State, CopyPath );
		C.Check( bSaved && State.FilePath == CopyPath && !State.Document.IsDirty() && State.SaveSerial == 1, "別名保存がFilePath・dirty・SaveSerialを更新する" );
		if ( !bSaved ) return;
		// 保存したJSONを新しいeffect定義へ読み直す。
		const TOptional<FString> CopyFullPath = Assets->ResolvePath( CopyPath );
		NJson::CJsonObject CopyJson;
		const bool bReadCopy = CopyFullPath.IsSet() && CopyJson.LoadFromFile( CopyFullPath.GetValue() );
		FParticleEffectDefinition FreshDefinition;
		const bool bParsedCopy = bReadCopy && FParticleEffectDefinition::FromJson( CopyJson.GetRoot(), FreshDefinition );
		C.Check( bParsedCopy && FreshDefinition.ComputeHash() == PresetDefinition.ComputeHash(), "保存JSONをfresh定義へ読み直すとeffect hashが一致する" );
		C.Check( bParsedCopy && FreshDefinition.Emitters.IsValidIndex( 0 ) && FreshDefinition.Emitters[0].Flipbook.ToJson() == PresetDefinition.Emitters[0].Flipbook.ToJson() && FreshDefinition.Emitters[0].Flipbook.ComputeHash() == PresetDefinition.Emitters[0].Flipbook.ComputeHash(), "保存JSONのFlipbook値とhashがpresetと一致する" );
		// 保存処理後も組み込みpreset原本のJSON valueを照合する。
		NJson::CJsonObject PresetAfterJson;
		const bool bReadPresetAfter = PresetFullPath.IsSet() && PresetAfterJson.LoadFromFile( PresetFullPath.GetValue() );
		C.Check( bReadPresetAfter && PresetAfterJson.GetRoot() == PresetBefore, "copy保存後もpreset原本JSONが不変" );
		// 終了処理がEditorだけのシート参照を解放することを確かめる。
		const uint64 DocumentHashBeforeRelease = State.Document.Get().ComputeHash();
		// State以外に強参照を残さず、終了後のsheet寿命を調べる弱参照。
		TWeakPtr<CSpriteSheet> WeakSheet;
		{
			// 実AssetManagerから取得した組み込みsheet。
			const TSharedPtr<CSpriteSheet> LoadedSheet = Assets->LoadSpriteSheet( PresetDefinition.Emitters[0].Flipbook.Sheet, false );
			State.FlipbookSheetCachePath = PresetDefinition.Emitters[0].Flipbook.Sheet;
			State.bFlipbookSheetLoadAttempted = true;
			State.FlipbookSheetCache = LoadedSheet;
			WeakSheet = LoadedSheet;
			C.Check( LoadedSheet.IsValid() && WeakSheet.IsValid(), "Editor cacheへ実sheetを入れて弱参照を保持する" );
		}
		// releaseが消す各cache状態を検査できるよう値を入れる。
		State.FlipbookValidationCacheKey = 123u;
		State.FlipbookValidationIssues.Add( "selftest validation cache" );
		C.Check( State.FlipbookSheetCache.IsValid() && State.bFlipbookSheetLoadAttempted, "Release前にEditorがsheetを強参照する" );
		NDebugUI::ReleaseEffectEditor( State );
		C.Check( !State.FlipbookSheetCache.IsValid() && State.FlipbookSheetCachePath.IsEmpty() && !State.bFlipbookSheetLoadAttempted && State.FlipbookValidationCacheKey == ~0ull && State.FlipbookValidationIssues.IsEmpty(), "ReleaseでFlipbook cache状態を初期化する" );
		C.Check( !WeakSheet.IsValid() && !WeakSheet.Pin().IsValid(), "Release後に外部強参照なしでsheet弱参照が失効する" );
		C.Check( State.Document.Get().ComputeHash() == DocumentHashBeforeRelease, "Release後も編集中Documentを保持する" );
		// runner sandbox内に作ったcopyだけを試験後に削除する。
		C.Check( CopyFullPath.IsSet() && NFileSystem::RemoveFile( CopyFullPath.GetValue() ), "試験用copy effectを削除する" );
	}

	// 使えないsheetの保存を拒み、preset原本のJSONを変えない。
	void TestFlipbookInvalidSave( NAssetTest::FChecker& C )
	{
		C.Group = "effect-flipbook-input-save";
		// アプリから提供されるasset manager。
		CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
		C.Check( Assets != nullptr, "保存拒否試験にasset managerがある" );
		if ( Assets == nullptr ) return;
		// 保存対象となる組み込みpresetの絶対パス。
		const TOptional<FString> FullPath = Assets->ResolvePath( "Effects/flipbook_sigil_burst.effect.json" );
		// 保存前に原本JSONを読むreader。
		NJson::CJsonObject BeforeJson;
		const bool bReadBefore = FullPath.IsSet() && BeforeJson.LoadFromFile( FullPath.GetValue() );
		// 原本のJSON valueを保存前比較用に複写する。
		const NlohmannJson Before = bReadBefore ? BeforeJson.GetRoot() : NlohmannJson();
		C.Check( bReadBefore, "Flipbook preset原本を保存前に読める" );
		if ( !bReadBefore ) return;
		// 原本の定義から、存在しないsheetを使う変更文書を作る。
		FParticleEffectDefinition Definition;
		const bool bParsed = FParticleEffectDefinition::FromJson( Before, Definition );
		C.Check( bParsed && Definition.Emitters.Num() > 0, "preset原本を定義へ読み込む" );
		if ( !bParsed || Definition.Emitters.IsEmpty() ) return;
		// 保存時に拒否させるemitter copy。
		FParticleEmitterDefinition Emitter = Definition.Emitters[0];
		Emitter.Flipbook.bEnabled = true;
		Emitter.Flipbook.Sheet = "Effects/missing_flipbook_test.sheet.json";
		Emitter.Flipbook.Clip = "Missing";
		Emitter.Path.Kind = FEffectPathDefinition::EKind::None;
		// 失敗が原本の保存状態を壊さないことを確認するstate。
		FEffectEditorState State;
		State.Document.Reset( Definition );
		State.Document.MarkSaved();
		NParticleEdit::SetEmitter( State.Document, 0, Emitter, "emitter-settings" );
		State.FilePath = "Effects/flipbook_sigil_burst.effect.json";
		C.Check( !NDebugUI::SaveEffectFile( State ) && State.Document.IsDirty(), "assetの無いFlipbook保存を拒みdirty状態を維持する" );
		// 保存後に同じ原本を再読し、JSON値が完全一致するか確認する。
		NJson::CJsonObject AfterJson;
		const bool bReadAfter = AfterJson.LoadFromFile( FullPath.GetValue() );
		C.Check( bReadAfter && AfterJson.GetRoot() == Before, "無効sheetの保存拒否で組み込みpreset原本を変更しない" );
	}
}

namespace NWorldTest
{
	// 実Flipbook panelへkeyboardと文字を入力し履歴・修復・保存拒否を確かめる。
	void RunEffectFlipbookInputTests( NAssetTest::FChecker& C )
	{
		C.Group = "effect-flipbook-input-context";
		// 試験前に選ばれていた画面のImGui context。
		ImGuiContext* Previous = ImGui::GetCurrentContext();
		{
			// Flipbook panelのUI入力を隔離するcontext。
			FFlipbookInputContext Input;
			C.Check( Input.IsReady(), "Flipbook入力用font atlasを構築する" );
			if ( Input.IsReady() )
			{
				TestFlipbookInputValues( C, Input );
				C.Check( Input.FramesBalanced(), "すべてのFlipbook入力frameでImGui stackを閉じる" );
			}
		}
		C.Check( ImGui::GetCurrentContext() == Previous, "Flipbook入力後に元のImGui contextへ戻る" );
		TestFlipbookPresetCopySave( C );
		TestFlipbookInvalidSave( C );
	}
}
