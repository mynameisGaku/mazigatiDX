// SPDX-License-Identifier: NOASSERTION
#include "WorldSelfTestSupport.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/EffectEditorInternal.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Platform/FileSystem.h"
#include "VendorModules/ImGui/imgui_internal.h"

#include <cmath>
#include <limits>

namespace
{
	// 実際の材質パネルへ入力し、独立したImGui context内で履歴を調べる。
	class FUVDistortionInputContext
	{
	public:
		// 独立したcontextと既定fontを用意する。
		FUVDistortionInputContext() : Previous( ImGui::GetCurrentContext() ), Context( ImGui::CreateContext() )
		{
			ImGui::SetCurrentContext( Context );
			ImGuiIO& Io = ImGui::GetIO();
			Io.DisplaySize = ImVec2( 1400.0f, 1800.0f );
			Io.DeltaTime = 1.0f / 60.0f;
			Io.IniFilename = nullptr;
			Io.LogFilename = nullptr;
			Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			Io.ConfigInputTrickleEventQueue = false;
			Io.Fonts->AddFontDefaultBitmap();
			bFontsReady = Io.Fonts->Build();
		}

		// 独立contextを破棄し、呼出し前のcontextを戻す。
		~FUVDistortionInputContext()
		{
			ImGui::DestroyContext( Context );
			ImGui::SetCurrentContext( Previous );
		}
		// ImGui contextの複製を禁止する。
		FUVDistortionInputContext( const FUVDistortionInputContext& ) = delete;
		// ImGui contextの代入を禁止する。
		FUVDistortionInputContext& operator=( const FUVDistortionInputContext& ) = delete;

		// 本番と同じ材質panelを一度描いて文書へ変更を記録する。
		bool Frame( FEffectEditorState& State )
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos( ImVec2( 0.0f, 0.0f ), ImGuiCond_Always );
			ImGui::SetNextWindowSize( ImVec2( 1000.0f, 1700.0f ), ImGuiCond_Always );
			if ( ImGui::Begin( "EffectUVDistortionInputSelfTest", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize ) )
			{
				Window = ImGui::GetCurrentWindow();
				if ( State.Document.Get().Emitters.IsValidIndex( 0 ) )
				{
					// 本番panelと同じく一時コピーを編集してから文書へ戻す。
					FParticleEmitterDefinition Emitter = State.Document.Get().Emitters[0];
					if ( NEffectEditorUI::DrawMaterialPanel( State, Emitter ) ) NParticleEdit::SetEmitter( State.Document, 0, Emitter, "emitter-settings" );
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
			// 画面内widgetを探す最大tab回数。
			for ( int32 Attempt = 0; Attempt < 256; ++Attempt )
			{
				if ( Context->NavId == Id ) return true;
				Tap( State, ImGuiKey_Tab );
			}
			return Context->NavId == Id;
		}

		// 表示したpanel内の通常widget IDを得る。
		ImGuiID ItemId( const char* Label ) const { return Window->GetID( Label ); }

		// 曲線panelの入れ子button IDを得る。
		ImGuiID NestedItemId( const char* Parent, const char* Label ) const { return ImHashStr( Label, 0, Window->GetID( Parent ) ); }
		// DragFloat2の指定成分に割り当てられたwidget IDを得る。
		ImGuiID VectorComponentItemId( const char* Label, int32 Component ) const
		{
			// group全体のID。
			const ImGuiID GroupId = ImHashStr( Label, 0, Window->IDStack.back() );
			// 選択した成分のID。
			const ImGuiID ComponentId = ImHashData( &Component, sizeof( Component ), GroupId );
			return ImHashStr( "", 0, ComponentId );
		}

		// 数値dragを文字入力へ切り替えて値を入力する。
		bool ReplaceNumeric( FEffectEditorState& State, ImGuiID Id, const char* Text )
		{
			Tap( State, ImGuiKey_Enter );
			// 対象dragが文字入力へ切り替わった状態。
			const bool bActivated = Context->TempInputId == Id && Context->ActiveId == Id;
			ImGui::GetIO().AddKeyEvent( ImGuiMod_Ctrl, true );
			ImGui::GetIO().AddKeyEvent( ImGuiKey_A, true );
			Frame( State );
			ImGui::GetIO().AddKeyEvent( ImGuiKey_A, false );
			ImGui::GetIO().AddKeyEvent( ImGuiMod_Ctrl, false );
			ImGui::GetIO().AddInputCharactersUTF8( Text );
			Frame( State );
			Tap( State, ImGuiKey_Tab );
			return bActivated;
		}

		// 文字入力欄の内容を全選択して置き換える。
		bool ReplaceText( FEffectEditorState& State, ImGuiID Id, const char* Text )
		{
			ImGui::GetIO().AddKeyEvent( ImGuiMod_Ctrl, true );
			ImGui::GetIO().AddKeyEvent( ImGuiKey_A, true );
			Frame( State );
			ImGui::GetIO().AddKeyEvent( ImGuiKey_A, false );
			ImGui::GetIO().AddKeyEvent( ImGuiMod_Ctrl, false );
			ImGui::GetIO().AddInputCharactersUTF8( Text );
			Frame( State );
			Tap( State, ImGuiKey_Tab );
			return Context->NavId != Id || Context->ActiveId == Id;
		}

		// font atlasを構築できたかを返す。
		bool IsReady() const { return bFontsReady; }
		// ImGuiのframe/window stackが閉じた状態かを返す。
		bool FramesBalanced() const { return bFramesBalanced; }

	private:
		// harness開始前に使われていたImGui context。
		ImGuiContext* Previous = nullptr;
		// 入力確認だけで使うImGui context。
		ImGuiContext* Context = nullptr;
		// widget IDを求めるため最後に描いたwindowを参照する。
		ImGuiWindow* Window = nullptr;
		// 独立font atlasを構築できた状態か。
		bool bFontsReady = false;
		// NewFrame/RenderとBegin/Endが釣り合ったか。
		bool bFramesBalanced = true;
	};

	// 実ImGui入力で値変更・履歴・リセットを確かめる。
	void TestUVDistortionPanelInput( NAssetTest::FChecker& C, FUVDistortionInputContext& Input )
	{
		C.Group = "effect-uv-distortion-panel-input";
		// 材質欄以外にも保持する画像と表示設定。
		FParticleEmitterDefinition OriginalEmitter;
		OriginalEmitter.Texture = "Effects/energy_veil_fibers.tga";
		OriginalEmitter.Material.bEnabled = true;
		OriginalEmitter.Material.UVScroll = FVector2( 0.23f, -0.31f );
		OriginalEmitter.Appearance.Size.Keys.Add( { 0.0f, 0.7f } );
		// 入力開始前に検証可能なエフェクト定義。
		FParticleEffectDefinition Definition;
		Definition.Name = "UVDistortion input";
		Definition.Emitters.Add( OriginalEmitter );
		C.Check( !FParticleEffectDefinition::HasErrors( Definition.Validate() ), "ImGui入力fixtureが有効な定義である" );
		// 材質panelが編集する文書と資産候補。
		FEffectEditorState State;
		State.Document.Reset( Definition );
		State.Document.MarkSaved();
		State.TextureFiles.Add( "Effects/uv_distortion_flow.tga" );
		Input.Frame( State );
		C.Check( Input.TabTo( State, Input.ItemId( "Enable UV distortion" ) ), "UV distortion checkboxへkeyboard focusする" );
		Input.Tap( State, ImGuiKey_Enter );
		C.Check( State.Document.Get().Emitters[0].Material.Distortion.bEnabled && State.Document.IsDirty(), "checkbox入力で歪みを有効化しdirtyにする" );
		C.Check( State.Document.GetNumUndo() == 1 && State.Document.Undo() && !State.Document.Get().Emitters[0].Material.Distortion.bEnabled, "有効化だけをUndoする" );
		C.Check( State.Document.Redo() && State.Document.Get().Emitters[0].Material.Distortion.bEnabled, "有効化をRedoする" );
		C.Check( Input.TabTo( State, Input.ItemId( "Noise path" ) ), "Noise pathへkeyboard focusする" );
		// noiseパス欄で置換入力が完了したか。
		const bool bTextureTyped = Input.ReplaceText( State, Input.ItemId( "Noise path" ), "Effects/uv_distortion_flow.tga" );
		C.Check( bTextureTyped && State.Document.Get().Emitters[0].Material.Distortion.Texture == "Effects/uv_distortion_flow.tga", "ImGui文字入力でnoise texture pathを変更する" );
		// scaleの横成分ID。
		const ImGuiID ScaleXId = Input.VectorComponentItemId( "Distortion scale", 0 );
		C.Check( Input.TabTo( State, ScaleXId ), "Distortion scaleへkeyboard focusする" );
		// scale横成分の文字入力結果。
		const bool bScaleTyped = Input.ReplaceNumeric( State, ScaleXId, "2.25" );
		C.Check( bScaleTyped && NMath::Abs( State.Document.Get().Emitters[0].Material.Distortion.UVScale.x - 2.25f ) < 0.003f, "Distortion scaleをImGui文字入力する" );
		// scrollの横成分ID。
		const ImGuiID ScrollXId = Input.VectorComponentItemId( "Distortion scroll / second", 0 );
		C.Check( Input.TabTo( State, ScrollXId ), "Distortion scrollへkeyboard focusする" );
		// scroll横成分の文字入力結果。
		const bool bScrollTyped = Input.ReplaceNumeric( State, ScrollXId, "-0.5" );
		C.Check( bScrollTyped && NMath::Abs( State.Document.Get().Emitters[0].Material.Distortion.UVScroll.x + 0.5f ) < 0.003f, "Distortion scrollをImGui文字入力する" );
		// Strength曲線へ点を足すbutton ID。
		const ImGuiID AddKeyId = Input.NestedItemId( "Distortion strength", "Add key" );
		C.Check( Input.TabTo( State, AddKeyId ), "Distortion strengthのAdd keyへkeyboard focusする" );
		Input.Tap( State, ImGuiKey_Enter );
		C.Check( State.Document.Get().Emitters[0].Material.Distortion.Strength.Keys.Num() == 1, "実curve UIからStrength keyを追加する" );
		C.Check( State.Document.GetNumUndo() >= 4 && State.Document.Undo() && State.Document.Get().Emitters[0].Material.Distortion.Strength.Keys.IsEmpty(), "Strength curve追加をUndoする" );
		C.Check( State.Document.Redo() && State.Document.Get().Emitters[0].Material.Distortion.Strength.Keys.Num() == 1, "Strength curve追加をRedoする" );
		// Reset後も保つ材質のUV移動量。
		const FVector2 PreservedMaterialScroll = State.Document.Get().Emitters[0].Material.UVScroll;
		C.Check( Input.TabTo( State, Input.ItemId( "Reset distortion" ) ), "Reset distortionへkeyboard focusする" );
		Input.Tap( State, ImGuiKey_Enter );
		// Reset後のemitter値。
		const FParticleEmitterDefinition& ResetEmitter = State.Document.Get().Emitters[0];
		C.Check( ResetEmitter.Material.Distortion.IsDefault(), "Reset distortionで歪み値だけ既定化する" );
		C.Check( ResetEmitter.Texture == OriginalEmitter.Texture && ResetEmitter.Material.bEnabled && ResetEmitter.Material.UVScroll == PreservedMaterialScroll && ResetEmitter.Appearance.Size.Keys.Num() == 1 && ResetEmitter.Appearance.Size.Keys[0].Value == 0.7f, "歪みResetが画像・材質・Appearanceを保持する" );
		// NaNを含む既存値でも安全な一時値でpanelが描けることを確認する。
		// NaNを含む表示安全性確認用emitter。
		FParticleEmitterDefinition InvalidEmitter = ResetEmitter;
		InvalidEmitter.Material.Distortion.bEnabled = true;
		InvalidEmitter.Material.Distortion.Texture = "Effects/uv_distortion_flow.tga";
		InvalidEmitter.Material.Distortion.UVScale.x = TNumericLimits<float32>::QuietNaN();
		NParticleEdit::SetEmitter( State.Document, 0, InvalidEmitter, "emitter-settings" );
		C.Check( Input.Frame( State ) && Input.FramesBalanced(), "NaNのscaleを含んでもImGui assertなく描画する" );
	}

	// 別名保存の往復・原本保持と非四角形設定の保存を確かめる。
	void TestUVDistortionSave( NAssetTest::FChecker& C )
	{
		C.Group = "effect-uv-distortion-save";
		// 保存と画像検証を行う既存アセット窓口。
		CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
		C.Check( Assets != nullptr, "UV歪み保存試験にasset managerがある" );
		if ( Assets == nullptr ) return;
		// 原本presetを比較する実パス。
		const TOptional<FString> PresetPath = Assets->ResolvePath( "Effects/uv_distortion_energy_veil.effect.json" );
		// コピー前のpreset JSON。
		NJson::CJsonObject BeforeJson;
		const bool bReadBefore = PresetPath.IsSet() && BeforeJson.LoadFromFile( PresetPath.GetValue() );
		C.Check( bReadBefore, "Flowing energy veil preset原本を読む" );
		if ( !bReadBefore ) return;
		// 別名保存後との比較に使う原本JSON値。
		const NlohmannJson Before = BeforeJson.GetRoot();
		// 原本のデータを解析した比較定義。
		FParticleEffectDefinition PresetDefinition;
		const bool bParsedPreset = FParticleEffectDefinition::FromJson( Before, PresetDefinition );
		C.Check( bParsedPreset && !FParticleEffectDefinition::HasErrors( PresetDefinition.Validate() ), "preset原本が有効な定義である" );
		if ( !bParsedPreset ) return;
		// 実際のPreset APIが作る未保存copy。
		FEffectEditorState State;
		const bool bCreatedCopy = NDebugUI::NewEffectFromPreset( State, "Effects/uv_distortion_energy_veil.effect.json" );
		C.Check( bCreatedCopy && State.Document.IsDirty() && State.FilePath.IsEmpty(), "NewEffectFromPresetがdirtyな未保存copyを作る" );
		if ( !bCreatedCopy ) return;
		// distortion編集対象となるcopyの先頭emitter。
		const bool bHasCopyEmitter = State.Document.Get().Emitters.IsValidIndex( 0 );
		C.Check( bHasCopyEmitter, "未保存copyに編集可能なemitterがある" );
		if ( !bHasCopyEmitter ) return;
		// 倍率・秒ごとの移動・強さ曲線を変更するcopy emitter。
		FParticleEmitterDefinition EditedEmitter = State.Document.Get().Emitters[0];
		// 保存するcopyが材質付きquad歪みを有効にするか。
		const bool bCopyEmitterHasActiveQuadDistortion = EditedEmitter.Path.Kind == FEffectPathDefinition::EKind::None && EditedEmitter.Material.bEnabled && EditedEmitter.Material.Distortion.bEnabled;
		C.Check( bCopyEmitterHasActiveQuadDistortion, "copy先頭emitterが材質付きquad歪みを使う" );
		if ( !bCopyEmitterHasActiveQuadDistortion ) return;
		EditedEmitter.Material.Distortion.UVScale = FVector2( 1.375f, 0.625f );
		EditedEmitter.Material.Distortion.UVScroll = FVector2( -0.375f, 0.125f );
		EditedEmitter.Material.Distortion.Strength.Interpolation = FEffectScalarCurve::EInterpolation::Linear;
		EditedEmitter.Material.Distortion.Strength.Keys.Reset();
		EditedEmitter.Material.Distortion.Strength.Keys.Add( { 0.0f, 0.2f } );
		EditedEmitter.Material.Distortion.Strength.Keys.Add( { 1.0f, 0.8f } );
		C.Check( NParticleEdit::SetEmitter( State.Document, 0, EditedEmitter, "emitter-settings" ) && State.Document.IsDirty(), "copyのUV歪み値を編集してdirty状態へ記録する" );
		// 保存内容と照合する編集後定義。
		const FParticleEffectDefinition EditedDefinition = State.Document.Get();
		C.Check( !FParticleEffectDefinition::HasErrors( EditedDefinition.Validate() ), "copyの編集後定義が有効である" );
		// copyを保存する一時名。
		const FString CopyPath = "Effects/selftest_uv_distortion_copy.effect.json";
		// 保存APIが書き込むcopy先の絶対path。
		const FString CopyTargetPath = Assets->GetRootDirectory() / CopyPath;
		// 保存前にcopy先が空いているか。
		const bool bCopyPathAvailable = !NFileSystem::FileExists( CopyTargetPath );
		C.Check( bCopyPathAvailable, "一時copy保存先が存在せず上書きしない" );
		if ( !bCopyPathAvailable ) return;
		// 編集済みcopyを一時pathへ保存する結果。
		const bool bSaved = NDebugUI::SaveEffectFile( State, CopyPath );
		C.Check( bSaved && State.FilePath == CopyPath && !State.Document.IsDirty() && State.SaveSerial == 1, "有効なcopyを別名保存しeditor状態が更新される" );
		if ( !bSaved ) return;
		// 保存したJSONを読み返す実パス。
		const TOptional<FString> CopyFullPath = Assets->ResolvePath( CopyPath );
		// 保存JSONを開くreader。
		NJson::CJsonObject CopyJson;
		const bool bReadCopy = CopyFullPath.IsSet() && CopyJson.LoadFromFile( CopyFullPath.GetValue() );
		// fresh parse後の独立定義。
		FParticleEffectDefinition FreshDefinition;
		const bool bParsedCopy = bReadCopy && FParticleEffectDefinition::FromJson( CopyJson.GetRoot(), FreshDefinition );
		C.Check( bParsedCopy && FreshDefinition.ComputeHash() == EditedDefinition.ComputeHash(), "編集後に別名保存したJSONの定義hashをfresh readで確認する" );
		C.Check( bParsedCopy && FreshDefinition.Emitters.IsValidIndex( 0 ) && FreshDefinition.Emitters[0].Material.Distortion.ToJson() == EditedDefinition.Emitters[0].Material.Distortion.ToJson() && FreshDefinition.Emitters[0].Material.Distortion.ComputeHash() == EditedDefinition.Emitters[0].Material.Distortion.ComputeHash(), "fresh read後の編集済みDistortion値とhashが一致する" );
		C.Check( bParsedCopy && FreshDefinition.Emitters.IsValidIndex( 0 ) && FreshDefinition.Emitters[0].Material.Distortion.UVScale.x == 1.375f && FreshDefinition.Emitters[0].Material.Distortion.UVScale.y == 0.625f && FreshDefinition.Emitters[0].Material.Distortion.UVScroll.x == -0.375f && FreshDefinition.Emitters[0].Material.Distortion.UVScroll.y == 0.125f && FreshDefinition.Emitters[0].Material.Distortion.Strength.Keys.Num() == 2 && FreshDefinition.Emitters[0].Material.Distortion.Strength.Keys[0].Value == 0.2f && FreshDefinition.Emitters[0].Material.Distortion.Strength.Keys[1].Value == 0.8f, "fresh readが非整数倍率・負scroll・編集後strengthを保つ" );
		// Save後の原本JSONを比較するreader。
		NJson::CJsonObject AfterJson;
		const bool bReadAfter = PresetPath.IsSet() && AfterJson.LoadFromFile( PresetPath.GetValue() );
		C.Check( bReadAfter && AfterJson.GetRoot() == Before, "別名保存後もpreset原本JSONが不変" );
		// 保存したcopyだけsandboxから片付ける。
		C.Check( CopyFullPath.IsSet() && NFileSystem::RemoveFile( CopyFullPath.GetValue() ), "一時copy effectを削除する" );

		// 非四角形の材質保存元となる検証済み帯preset。
		const TOptional<FString> RibbonPath = Assets->ResolvePath( "Effects/material_ribbon_trail.effect.json" );
		// 帯presetのJSON reader。
		NJson::CJsonObject RibbonJson;
		const bool bReadRibbon = RibbonPath.IsSet() && RibbonJson.LoadFromFile( RibbonPath.GetValue() );
		// 帯の有効な初期定義。
		FParticleEffectDefinition RibbonDefinition;
		const bool bParsedRibbon = bReadRibbon && FParticleEffectDefinition::FromJson( RibbonJson.GetRoot(), RibbonDefinition );
		C.Check( bParsedRibbon && !FParticleEffectDefinition::HasErrors( RibbonDefinition.Validate() ), "非quad fixtureとなる帯presetが有効" );
		if ( bParsedRibbon && RibbonDefinition.Emitters.IsValidIndex( 0 ) )
		{
			// 四角形以外でも設定を保持して保存するemitter。
			FParticleEmitterDefinition& RibbonEmitter = RibbonDefinition.Emitters[0];
			RibbonEmitter.Material.Distortion.bEnabled = true;
			RibbonEmitter.Material.Distortion.Texture = "Effects/uv_distortion_flow.tga";
			RibbonEmitter.Material.Distortion.UVScale = FVector2( 1.4f, 0.8f );
			// 非quad設定を保存するeditor state。
			FEffectEditorState RibbonState;
			RibbonState.Document.Reset( RibbonDefinition );
			RibbonState.Document.MarkSaved();
			NParticleEdit::SetEmitter( RibbonState.Document, 0, RibbonEmitter, "emitter-settings" );
			// 非quad定義を保存する一時名。
			const FString RibbonCopyPath = "Effects/selftest_uv_distortion_nonquad.effect.json";
			// 保存APIが書き込むnonquad先の絶対path。
			const FString RibbonTargetPath = Assets->GetRootDirectory() / RibbonCopyPath;
			// 保存前にnonquad先が空いているか。
			const bool bRibbonPathAvailable = !NFileSystem::FileExists( RibbonTargetPath );
			C.Check( bRibbonPathAvailable, "一時nonquad保存先が存在せず上書きしない" );
			if ( !bRibbonPathAvailable ) return;
			// 非quad設定を保存する結果。
			const bool bSavedRibbon = NDebugUI::SaveEffectFile( RibbonState, RibbonCopyPath );
			C.Check( bSavedRibbon, "非quadの歪み設定は画像を適用せず保持して保存する" );
			// 保存後JSONを解決する実パス。
			const TOptional<FString> SavedRibbonPath = Assets->ResolvePath( RibbonCopyPath );
			// 保存後JSONのreader。
			NJson::CJsonObject SavedRibbonJson;
			// 保存後JSONを読み込めたか。
			const bool bReadSavedRibbon = bSavedRibbon && SavedRibbonPath.IsSet() && SavedRibbonJson.LoadFromFile( SavedRibbonPath.GetValue() );
			// fresh parse後の帯定義。
			FParticleEffectDefinition FreshRibbonDefinition;
			const bool bParsedSavedRibbon = bReadSavedRibbon && FParticleEffectDefinition::FromJson( SavedRibbonJson.GetRoot(), FreshRibbonDefinition );
			C.Check( bParsedSavedRibbon && FreshRibbonDefinition.Emitters.IsValidIndex( 0 ) && FreshRibbonDefinition.Emitters[0].Material.Distortion.ComputeHash() == RibbonEmitter.Material.Distortion.ComputeHash(), "非quad保存後もDistortion設定hashを保つ" );
			if ( bSavedRibbon && SavedRibbonPath.IsSet() ) C.Check( NFileSystem::RemoveFile( SavedRibbonPath.GetValue() ), "非quadの一時effectを削除する" );
		}
	}

	// RibbonとTree Lightningの組み込みpresetを複製し、編集保存後に読み直す。
	void TestRibbonAndTreeDistortionPresetSaves( NAssetTest::FChecker& C )
	{
		C.Group = "effect-uv-distortion-path-preset-save";
		// presetと保存先を順に確認する固定path一覧。
		const FString PresetPaths[] = { "Effects/uv_distortion_ribbon.effect.json", "Effects/uv_distortion_tree_lightning.effect.json" };
		// save/parse処理に使う既存asset窓口。
		CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
		C.Check( Assets != nullptr, "path preset保存試験にasset managerがある" );
		if ( Assets == nullptr ) return;
		for ( const FString& PresetPath : PresetPaths )
		{
			// 複製前のpreset原本を読む実path。
			const TOptional<FString> PresetFullPath = Assets->ResolvePath( PresetPath );
			// 保存前の原本JSON reader。
			NJson::CJsonObject BeforeJson;
			// preset原本JSONを読めたか。
			const bool bReadBefore = PresetFullPath.IsSet() && BeforeJson.LoadFromFile( PresetFullPath.GetValue() );
			C.Check( bReadBefore, "歪み付きpath preset原本を保存前に読む" );
			if ( !bReadBefore ) continue;
			// preset APIで作る未保存編集文書。
			FEffectEditorState State;
			// preset APIがcopyを作れたか。
			const bool bCreatedCopy = NDebugUI::NewEffectFromPreset( State, PresetPath );
			C.Check( bCreatedCopy && State.Document.IsDirty() && State.FilePath.IsEmpty(), "NewEffectFromPresetがpath presetを未保存copyにする" );
			if ( !bCreatedCopy || !State.Document.Get().Emitters.IsValidIndex( 0 ) ) continue;
			// 先頭emitterの歪み倍率だけを編集する値。
			FParticleEmitterDefinition EditedEmitter = State.Document.Get().Emitters[0];
			EditedEmitter.Material.Distortion.UVScale.x += 0.125f;
			// 倍率編集をdocument historyへ記録した結果。
			const bool bEdited = NParticleEdit::SetEmitter( State.Document, 0, EditedEmitter, "emitter-settings" );
			C.Check( bEdited && State.Document.IsDirty(), "path presetのUV倍率編集をhistoryへ記録する" );
			if ( !bEdited ) continue;
			// 別名保存するpathと、元ファイルを保護する存在確認。
			const FString CopyPath = PresetPath.Contains( "ribbon" ) ? "Effects/selftest_uv_distortion_ribbon_copy.effect.json" : "Effects/selftest_uv_distortion_tree_copy.effect.json";
			// copy先がまだ存在せず上書きしない状態。
			const bool bCopyPathAvailable = !Assets->ResolvePath( CopyPath ).IsSet();
			C.Check( bCopyPathAvailable, "path preset copy保存先が空いていて既存ファイルを上書きしない" );
			if ( !bCopyPathAvailable ) continue;
			// 編集後にfresh parseと比較する保存snapshot。
			const FParticleEffectDefinition EditedDefinition = State.Document.Get();
			// copyを別名保存できたか。
			const bool bSaved = NDebugUI::SaveEffectFile( State, CopyPath );
			C.Check( bSaved && !State.Document.IsDirty() && State.FilePath == CopyPath && State.SaveSerial == 1, "歪み付きRibbon/Tree copyを保存して状態を更新する" );
			// 実際に保存されたcopyの解決path。
			const TOptional<FString> CopyFullPath = Assets->ResolvePath( CopyPath );
			// 保存JSONを読むreader。
			NJson::CJsonObject CopyJson;
			// 保存JSONをreaderへ読み込めたか。
			const bool bReadCopy = bSaved && CopyFullPath.IsSet() && CopyJson.LoadFromFile( CopyFullPath.GetValue() );
			// fresh parseされた保存定義。
			FParticleEffectDefinition FreshDefinition;
			// 新しいdefinitionへのparse結果。
			const bool bParsedCopy = bReadCopy && FParticleEffectDefinition::FromJson( CopyJson.GetRoot(), FreshDefinition );
			C.Check( bParsedCopy && FreshDefinition.ComputeHash() == EditedDefinition.ComputeHash() && FreshDefinition.Emitters.IsValidIndex( 0 ) && FreshDefinition.Emitters[0].Material.Distortion.ComputeHash() == EditedDefinition.Emitters[0].Material.Distortion.ComputeHash(), "path presetの編集値とhashをfresh readで保つ" );
			// 保存後も原本JSONの全fieldが同じか確かめるreader。
			NJson::CJsonObject AfterJson;
			// 保存後にpreset原本を読めたか。
			const bool bReadAfter = AfterJson.LoadFromFile( PresetFullPath.GetValue() );
			C.Check( bReadAfter && AfterJson.GetRoot() == BeforeJson.GetRoot(), "path presetのcopy保存後も原本JSONが不変" );
			if ( bSaved && CopyFullPath.IsSet() ) C.Check( NFileSystem::RemoveFile( CopyFullPath.GetValue() ), "一時path preset copyを削除する" );
		}
	}

	// 読めないnoiseを使う四角形・材質付きpathの保存を拒否する。
	void TestUVDistortionMissingNoise( NAssetTest::FChecker& C )
	{
		C.Group = "effect-uv-distortion-save-missing-noise";
		// 保存拒否で参照する既存asset窓口。
		CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
		C.Check( Assets != nullptr, "missing-noise保存試験にasset managerがある" );
		if ( Assets == nullptr ) return;
		// 保存試験の有効な素材元。
		FParticleEmitterDefinition Emitter;
		Emitter.Texture = "Effects/energy_veil_fibers.tga";
		Emitter.Material.bEnabled = true;
		Emitter.Material.Distortion.bEnabled = true;
		Emitter.Material.Distortion.Texture = "Effects/missing_uv_distortion_noise.tga";
		// missing texture検証に使う有効な文書。
		// missing noiseを含めたdocument定義。
		FParticleEffectDefinition Definition;
		Definition.Emitters.Add( Emitter );
		C.Check( !FParticleEffectDefinition::HasErrors( Definition.Validate() ), "missing noise fixtureはasset参照以外が有効" );
		// 保存拒否でdirty状態を観察するeditor state。
		FEffectEditorState State;
		State.Document.Reset( Definition );
		State.Document.MarkSaved();
		State.SaveAsPath = "Effects/selftest_uv_distortion_missing.effect.json";
		// 意図しない保存成功でも既存ファイルを上書きしないpath。
		const FString MissingOutputPath = Assets->GetRootDirectory() / State.SaveAsPath;
		// 保存前にmissing-noise先が空いているか。
		const bool bMissingOutputAvailable = !NFileSystem::FileExists( MissingOutputPath );
		C.Check( bMissingOutputAvailable, "missing-noise出力先が存在せず上書きしない" );
		if ( !bMissingOutputAvailable ) return;
		// 保存拒否を確かめるため別の未存在noiseへ編集するemitter。
		FParticleEmitterDefinition EditedEmitter = Emitter;
		EditedEmitter.Material.Distortion.Texture = "Effects/missing_uv_distortion_noise_edit.tga";
		C.Check( NParticleEdit::SetEmitter( State.Document, 0, EditedEmitter, "emitter-settings" ) && State.Document.IsDirty(), "未存在noiseへの編集をdirty状態へ記録する" );
		C.Check( !FParticleEffectDefinition::HasErrors( State.Document.Get().Validate() ), "保存前もmissing noise以外の定義検証は通る" );
		// missing noiseで保存を拒否する結果。
		const bool bSaved = NDebugUI::SaveEffectFile( State, State.SaveAsPath );
		C.Check( !bSaved && State.Document.IsDirty() && State.SaveSerial == 0 && State.FilePath.IsEmpty() && State.StatusMessage.Contains( "UV歪み用ノイズ画像" ) && State.StatusMessage.Contains( *EditedEmitter.Material.Distortion.Texture ), "missing noiseだけを理由に保存を拒み保存状態を維持する" );
		// Ribbon経路でもmissing noise以外は有効な変更にする。
		FParticleEmitterDefinition MissingRibbonEmitter = State.Document.Get().Emitters[0];
		MissingRibbonEmitter.Path.Kind = FEffectPathDefinition::EKind::Ribbon;
		MissingRibbonEmitter.Material.Distortion.Texture = "Effects/missing_ribbon_uv_noise.tga";
		// Ribbon設定への編集結果。
		const bool bRibbonEdited = NParticleEdit::SetEmitter( State.Document, 0, MissingRibbonEmitter, "emitter-settings" );
		C.Check( bRibbonEdited && !FParticleEffectDefinition::HasErrors( State.Document.Get().Validate() ), "Ribbon missing noise fixtureは参照以外の定義が有効" );
		if ( !bRibbonEdited ) return;
		// Ribbonの失敗保存先を上書きから守る。
		const FString MissingRibbonCopyPath = "Effects/selftest_uv_distortion_missing_ribbon.effect.json";
		// Ribbonの失敗保存先がまだ存在しない状態。
		const bool bRibbonOutputAvailable = !Assets->ResolvePath( MissingRibbonCopyPath ).IsSet();
		C.Check( bRibbonOutputAvailable, "Ribbon missing-noise出力先が空いていて上書きしない" );
		if ( !bRibbonOutputAvailable ) return;
		// Ribbonのmissing noiseでも保存を拒否する結果。
		// missing noise理由で保存できなかった結果。
		const bool bRibbonSaved = NDebugUI::SaveEffectFile( State, MissingRibbonCopyPath );
		C.Check( !bRibbonSaved && State.Document.IsDirty() && State.SaveSerial == 0 && State.FilePath.IsEmpty() && State.StatusMessage.Contains( "UV歪み用ノイズ画像" ) && State.StatusMessage.Contains( *MissingRibbonEmitter.Material.Distortion.Texture ), "Ribbonはmissing noiseだけを理由に保存を拒否する" );
	}

	// panel cacheが閉じるとnoiseを手放し文書だけを残す。
	void TestUVDistortionCacheRelease( NAssetTest::FChecker& C, FUVDistortionInputContext& Input )
	{
		C.Group = "effect-uv-distortion-cache-release";
		// 解放前も有効なnoiseを参照するemitter。
		FParticleEmitterDefinition Emitter;
		Emitter.Texture = "Effects/energy_veil_fibers.tga";
		Emitter.Material.bEnabled = true;
		Emitter.Material.Distortion.bEnabled = true;
		Emitter.Material.Distortion.Texture = "Effects/uv_distortion_flow.tga";
		// cache解放時に保持する有効なeditor定義。
		FParticleEffectDefinition Definition;
		Definition.Emitters.Add( Emitter );
		C.Check( !FParticleEffectDefinition::HasErrors( Definition.Validate() ), "cache lifetime fixtureが有効な定義である" );
		// UI描画と参照を所有するstate。
		FEffectEditorState State;
		State.Document.Reset( Definition );
		State.Document.MarkSaved();
		State.TextureFiles.Add( "Effects/uv_distortion_flow.tga" );
		Input.Frame( State );
		C.Check( State.UVDistortionTextureCache.IsValid() && State.UVDistortionTextureCache->IsReady(), "panelがnoise画像を一度読みcacheに保持する" );
		// stateの強参照だけが残ったことを確かめる弱参照。
		TWeakPtr<CTexture> WeakTexture = State.UVDistortionTextureCache;
		// Release前後の文書が同じか調べるhash。
		const uint64 DocumentHash = State.Document.Get().ComputeHash();
		NDebugUI::ReleaseEffectEditor( State );
		C.Check( !State.UVDistortionTextureCache.IsValid() && State.UVDistortionTextureCachePath.IsEmpty() && !State.bUVDistortionTextureLoadAttempted, "ReleaseがUV distortion cache状態を初期化する" );
		C.Check( !WeakTexture.IsValid() && !WeakTexture.Pin().IsValid(), "Release後にnoise textureの弱参照が失効する" );
		C.Check( State.Document.Get().ComputeHash() == DocumentHash, "Releaseは編集中のdocumentを保持する" );
		C.Check( Input.FramesBalanced(), "すべてのUV distortion入力frameでImGui stackを閉じる" );
	}
}

namespace NWorldTest
{
	// UV歪みのImGui入力・保存・資源寿命を実assetで確かめる。
	void RunEffectUVDistortionInputTests( NAssetTest::FChecker& C )
	{
		// harness開始前に使われていたImGui context。
		ImGuiContext* Previous = ImGui::GetCurrentContext();
		{
			// UV歪みのwidgetだけを隔離して入力するcontext。
			FUVDistortionInputContext Input;
			C.Check( Input.IsReady(), "UV歪み入力用font atlasを構築する" );
			if ( Input.IsReady() ) TestUVDistortionPanelInput( C, Input );
			TestUVDistortionCacheRelease( C, Input );
		}
		C.Check( ImGui::GetCurrentContext() == Previous, "UV歪み入力後に元のImGui contextへ戻る" );
		TestUVDistortionSave( C );
		TestRibbonAndTreeDistortionPresetSaves( C );
		TestUVDistortionMissingNoise( C );
	}
}
