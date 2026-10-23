// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectForceFieldEditorSelfTest.h"

#include <cmath>
#include <cstring>
#include <limits>

#include "AssetSelfTestSupport.h"
#include "Library/DebugUI/EffectEditorInternal.h"
#include "Library/Framework/Effect/EffectForceFieldDefinition.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Platform/DxLib/EffectPreviewSurface.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "VendorModules/ImGui/imgui_internal.h"

namespace
{
	// OS入力とゲーム側設定を使わない、実パネル用の独立 ImGui 文脈。
	class FForceFieldInputContext
	{
	public:
		// 描画backendなしでフォントatlasとキーボード移動を準備する。
		FForceFieldInputContext() : Previous( ImGui::GetCurrentContext() ), Context( ImGui::CreateContext() )
		{
			ImGui::SetCurrentContext( Context );
			// 独立文脈の入力・描画設定。
			ImGuiIO& Io = ImGui::GetIO();
			Io.DisplaySize = ImVec2( 1500.0f, 1900.0f );
			Io.DeltaTime = 1.0f / 60.0f;
			Io.IniFilename = nullptr;
			Io.LogFilename = nullptr;
			Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			Io.ConfigInputTrickleEventQueue = false;
			Io.Fonts->AddFontDefaultBitmap();
			bFontsReady = Io.Fonts->Build();
		}

		// この文脈を解放して、呼出元の文脈へ戻す。
		~FForceFieldInputContext()
		{
			ImGui::DestroyContext( Context );
			ImGui::SetCurrentContext( Previous );
		}
		// ImGui contextの所有権を複製しない。
		FForceFieldInputContext( const FForceFieldInputContext& ) = delete;
		// ImGui contextの所有権を代入しない。
		FForceFieldInputContext& operator=( const FForceFieldInputContext& ) = delete;

		// font atlasの構築結果を返す。
		bool IsReady() const { return bFontsReady; }

		// 実際の定義panelを一frame描き、終了時に編集sessionを閉じる。
		void Frame( FEffectEditorState& State )
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos( ImVec2( 0.0f, 0.0f ), ImGuiCond_Always );
			ImGui::SetNextWindowSize( ImVec2( 1400.0f, 1850.0f ), ImGuiCond_Always );
			if ( ImGui::Begin( "ForceFieldEditorInputSelfTest", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize ) )
			{
				Window = ImGui::GetCurrentWindow();
				NEffectEditorUI::DrawDefinitionPanel( State );
				if ( !ImGui::IsAnyItemActive() ) State.Document.EndEditSession();
			}
			ImGui::End();
			ImGui::Render();
			bFramesBalanced = bFramesBalanced && !Context->WithinFrameScope && Context->CurrentWindowStack.Size == 0;
		}

		// 指定した実項目へTab入力だけでfocusを移す。
		bool TabTo( FEffectEditorState& State, ImGuiID Id, int32 MaxAttempts = 512 )
		{
			for ( int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt )
			{
				if ( Context->NavId == Id ) return true;
				Tap( State, ImGuiKey_Tab );
			}
			return Context->NavId == Id;
		}

		// キーを押して離し、panelを両frameで更新する。
		void Tap( FEffectEditorState& State, ImGuiKey Key )
		{
			ImGui::GetIO().AddKeyEvent( Key, true );
			Frame( State );
			ImGui::GetIO().AddKeyEvent( Key, false );
			Frame( State );
		}

		// 今回描いたwindowから、通常項目の識別子を得る。
		ImGuiID ItemId( const char* Label ) const { return Window->GetID( Label ); }
		// PushID(index)内で描いた項目の識別子を同じ規則で得る。
		ImGuiID FieldItemId( const char* Label, int32 Component = -1 ) const
		{
			// multi-input全体の親識別子。
			const ImGuiID WidgetId = Window->GetID( Label );
			if ( Component < 0 ) return WidgetId;
			// DragFloat3が成分番号と空ラベルから作る識別子。
			const ImGuiID ComponentId = ImHashData( &Component, sizeof( Component ), WidgetId );
			return ImHashStr( "", 0, ComponentId );
		}

		// Drag入力を文字入力へ切り替え、文字列を入力してTabで確定する。
		bool ReplaceNumeric( FEffectEditorState& State, ImGuiID Id, const char* Text )
		{
			if ( !TabTo( State, Id ) ) return false;
			Tap( State, ImGuiKey_Enter );
			// Enterで実際の一時文字入力へ切り替わったか。
			const bool bActivated = Context->TempInputId == Id && Context->ActiveId == Id;
			ImGui::GetIO().AddKeyEvent( ImGuiMod_Ctrl, true );
			ImGui::GetIO().AddKeyEvent( ImGuiKey_A, true );
			Frame( State );
			ImGui::GetIO().AddKeyEvent( ImGuiKey_A, false );
			ImGui::GetIO().AddKeyEvent( ImGuiMod_Ctrl, false );
			// 一文字ずつframeを進め、連続編集sessionの更新を実入力で作る。
			for ( const char* Character = Text; *Character != '\0'; ++Character )
			{
				// 今回だけ入力する一文字と終端。
				const char InputCharacter[2] = { *Character, '\0' };
				ImGui::GetIO().AddInputCharactersUTF8( InputCharacter );
				Frame( State );
			}
			Tap( State, ImGuiKey_Tab );
			return bActivated;
		}

		// ボタンを実際のTab・Enter入力で押す。
		bool Press( FEffectEditorState& State, const char* Label, int32 MaxAttempts = 512 )
		{
			if ( !TabTo( State, ItemId( Label ), MaxAttempts ) ) return false;
			Tap( State, ImGuiKey_Enter );
			return true;
		}

		// 表示用vertexにNaNや無限大が混ざっていないか返す。
		bool HasFiniteVertices() const
		{
			// 直前frameの表示用vertex一式。
			const ImDrawData* Data = ImGui::GetDrawData();
			if ( Data == nullptr ) return false;
			// 直前frameの各描画一覧。
			for ( int32 ListIndex = 0; ListIndex < Data->CmdListsCount; ++ListIndex )
			{
				// 有限性を調べる現在の描画一覧。
				const ImDrawList* List = Data->CmdLists[ListIndex];
				// 表示位置が有限である必要があるvertex。
				for ( const ImDrawVert& Vertex : List->VtxBuffer ) if ( !std::isfinite( Vertex.pos.x ) || !std::isfinite( Vertex.pos.y ) ) return false;
			}
			return true;
		}

		// ImGuiのwindow stackとframeが毎回閉じているかを返す。
		bool AreFramesBalanced() const { return bFramesBalanced; }

	private:
		// 呼び出し元が所有していたImGui文脈。
		ImGuiContext* Previous = nullptr;
		// この入力確認だけで所有するImGui文脈。
		ImGuiContext* Context = nullptr;
		// 実項目IDを求める最後の描画window。
		ImGuiWindow* Window = nullptr;
		// font atlas構築の成否。
		bool bFontsReady = false;
		// 全frameでstackが閉じたか。
		bool bFramesBalanced = true;
	};

	// 既知の値を持つForceField文書を作る。
	FParticleEffectDefinition MakeForceFieldInputDefinition()
	{
		// 入力確認用の保存文書。
		FParticleEffectDefinition Definition;
		Definition.Name = "Force field input";
		// 既知のfieldを含むemitter。
		FParticleEmitterDefinition Emitter;
		// 初期状態として確認する半径field。
		FEffectForceFieldDefinition Field;
		Field.Kind = FEffectForceFieldDefinition::EKind::Radial;
		Field.Acceleration = FVector3( 2.0f, 3.0f, 4.0f );
		Field.Position = FVector3( 5.0f, 6.0f, 7.0f );
		Field.Radius = 8.0f;
		Field.Strength = -9.0f;
		Emitter.ForceFields.Add( Field );
		Definition.Emitters.Add( Emitter );
		return Definition;
	}

	// 実パネルのすべての保存値入力を実文字操作で変更し、履歴の範囲を確かめる。
	void TestForceFieldValueInputs( NAssetTest::FChecker& C, FForceFieldInputContext& Input )
	{
		C.Group = "effect-force-field-editor-values";
		// 描画ガイドの元になる文書と設定。
		FEffectEditorState State;
		State.Document.Reset( MakeForceFieldInputDefinition() );
		Input.Frame( State );
		// guide切替前の保存値識別子。
		const uint64 InitialHash = State.Document.Get().Emitters[0].ForceFields[0].ComputeHash();
		// guide切替前の履歴数。
		const int32 InitialUndo = State.Document.GetNumUndo();
		// guide checkboxへTabで移れたか。
		const bool bGuideFocused = Input.TabTo( State, Input.ItemId( "Show force field guides" ) );
		Input.Tap( State, ImGuiKey_Space );
		C.Check( bGuideFocused && !State.bShowForceFieldGuides && !State.Document.IsDirty() && State.Document.GetNumUndo() == InitialUndo && State.Document.Get().Emitters[0].ForceFields[0].ComputeHash() == InitialHash, "guide表示切替は実チェックボックスで文書dirty・履歴を作らない" );

		// 実Enabled checkboxのImGui識別子。
		const ImGuiID EnabledId = Input.FieldItemId( "Enable force field##ForceFieldEnabled" );
		// Enabled checkboxへTabで移れたか。
		const bool bEnabledFocused = Input.TabTo( State, EnabledId );
		Input.Tap( State, ImGuiKey_Space );
		C.Check( bEnabledFocused && !State.Document.Get().Emitters[0].ForceFields[0].bEnabled && State.Document.GetNumUndo() == InitialUndo + 1, "Enabledを実checkboxから変え、保存文書へ一回反映する" );

		// 種類comboのImGui識別子。
		const ImGuiID KindId = Input.FieldItemId( "Kind##ForceFieldKind" );
		// 種類comboへTabで移れたか。
		const bool bKindFocused = Input.TabTo( State, KindId );
		Input.Tap( State, ImGuiKey_Enter );
		Input.Tap( State, ImGuiKey_UpArrow );
		Input.Tap( State, ImGuiKey_Enter );
		C.Check( bKindFocused && State.Document.Get().Emitters[0].ForceFields[0].Kind == FEffectForceFieldDefinition::EKind::Uniform, "Kindを実comboのキーボード入力でRadialからUniformへ変えられる" );
		C.Check( Input.ReplaceNumeric( State, Input.FieldItemId( "Acceleration (m/s2)##ForceFieldAcceleration", 0 ), "11" ) && State.Document.Get().Emitters[0].ForceFields[0].Acceleration.x == 11.0f && Input.ReplaceNumeric( State, Input.FieldItemId( "Acceleration (m/s2)##ForceFieldAcceleration", 1 ), "12" ) && State.Document.Get().Emitters[0].ForceFields[0].Acceleration.y == 12.0f && Input.ReplaceNumeric( State, Input.FieldItemId( "Acceleration (m/s2)##ForceFieldAcceleration", 2 ), "13" ) && State.Document.Get().Emitters[0].ForceFields[0].Acceleration.z == 13.0f, "Accelerationの3成分を実DragFloat3文字入力で編集する" );
		C.Check( Input.ReplaceNumeric( State, Input.FieldItemId( "World center (m)##ForceFieldPosition", 0 ), "21" ) && State.Document.Get().Emitters[0].ForceFields[0].Position.x == 21.0f && Input.ReplaceNumeric( State, Input.FieldItemId( "World center (m)##ForceFieldPosition", 1 ), "22" ) && State.Document.Get().Emitters[0].ForceFields[0].Position.y == 22.0f && Input.ReplaceNumeric( State, Input.FieldItemId( "World center (m)##ForceFieldPosition", 2 ), "23" ) && State.Document.Get().Emitters[0].ForceFields[0].Position.z == 23.0f, "World centerの3成分を実DragFloat3文字入力で編集する" );
		C.Check( Input.ReplaceNumeric( State, Input.FieldItemId( "Radius (m)##ForceFieldRadius" ), "24" ) && State.Document.Get().Emitters[0].ForceFields[0].Radius == 24.0f, "Radiusを実数値入力で編集する" );
		// Strengthの文字入力前の履歴数。
		const int32 BeforeStrengthUndo = State.Document.GetNumUndo();
		C.Check( Input.ReplaceNumeric( State, Input.FieldItemId( "Strength (m/s2)##ForceFieldStrength" ), "-25" ) && State.Document.Get().Emitters[0].ForceFields[0].Strength == -25.0f, "Strengthの負値を複数frameの実数値入力で引力として保存する" );
		C.Check( State.Document.GetNumUndo() == BeforeStrengthUndo + 1, "一つのStrength編集sessionで複数の値更新を一履歴へまとめる" );
		// 入力後の保存値をまとめて照合するfield。
		const FEffectForceFieldDefinition& Edited = State.Document.Get().Emitters[0].ForceFields[0];
		C.Check( Edited.Kind == FEffectForceFieldDefinition::EKind::Uniform && Edited.Acceleration.x == 11.0f && Edited.Acceleration.y == 12.0f && Edited.Acceleration.z == 13.0f && Edited.Position.x == 21.0f && Edited.Position.y == 22.0f && Edited.Position.z == 23.0f && Edited.Radius == 24.0f && Edited.Strength == -25.0f, "種類で使わない設定値も切替後に保持される" );
		C.Check( State.Document.Undo() && State.Document.Get().Emitters[0].ForceFields[0].Strength == -9.0f && State.Document.Get().Emitters[0].ForceFields[0].Radius == 24.0f && State.Document.Get().Emitters[0].ForceFields[0].Position.x == 21.0f, "一度のUndoでStrengthだけを戻し、別のfield値を保つ" );
		C.Check( State.Document.Redo() && State.Document.Get().Emitters[0].ForceFields[0].Strength == -25.0f && State.Document.Get().Emitters[0].ForceFields[0].Radius == 24.0f, "Strength入力をRedoして他の保存値を保つ" );
		C.Check( Input.AreFramesBalanced(), "力場の入力確認中にImGui frameとwindow stackを閉じる" );
	}

	// 実際の構造ボタンで安定した一覧と履歴を編集し、末尾削除後もTab移動を続ける。
	void TestForceFieldStructureInputs( NAssetTest::FChecker& C, FForceFieldInputContext& Input )
	{
		C.Group = "effect-force-field-editor-structure";
		// 構造操作を行う独立文書と履歴。
		FEffectEditorState State;
		State.Document.Reset( MakeForceFieldInputDefinition() );
		Input.Frame( State );
		// AddボタンがTabとEnterで操作できたか。
		const bool bAddFocused = Input.Press( State, "Add##ForceFieldAdd" );
		C.Check( bAddFocused && State.Document.Get().Emitters[0].ForceFields.Num() == 2 && State.SelectedForceField == 1 && State.Document.Get().Emitters[0].ForceFields[1].Acceleration.x == 1.0f && State.Document.GetNumUndo() == 1, "Addは既定の一様加速度を末尾へ加えて一つ選び、独立した履歴を作る" );
		if ( !State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 1 ) ) return;
		// Duplicateで値を保つ基準field。
		FEffectForceFieldDefinition Custom;
		Custom.Kind = FEffectForceFieldDefinition::EKind::Radial;
		Custom.Position = FVector3( 31.0f, 32.0f, 33.0f );
		Custom.Radius = 34.0f;
		Custom.Strength = -35.0f;
		State.Document.BeginEdit().Emitters[0].ForceFields[1] = Custom;
		State.Document.EndEditSession();
		Input.Frame( State );
		// Duplicateボタンの実入力結果。
		const bool bDuplicateFocused = Input.Press( State, "Duplicate##ForceFieldDuplicate" );
		C.Check( bDuplicateFocused && State.Document.Get().Emitters[0].ForceFields.Num() == 3 && State.SelectedForceField == 2 && State.Document.Get().Emitters[0].ForceFields[2].ComputeHash() == Custom.ComputeHash(), "Duplicateは選択中の全設定を保ち、直後へ追加する" );
		if ( !State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 2 ) ) return;
		// 並べ替え対象を同値にせず履歴へ記録できるよう、複製後の値を区別する編集。
		State.Document.BeginEdit().Emitters[0].ForceFields[2].Strength = -45.0f;
		State.Document.EndEditSession();
		Input.Frame( State );
		C.Check( Input.Press( State, "Up##ForceFieldUp" ) && State.Document.Get().Emitters[0].ForceFields.Num() == 3 && State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 1 ) && State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 2 ) && State.SelectedForceField == 1 && State.Document.Get().Emitters[0].ForceFields[1].Strength == -45.0f && State.Document.Get().Emitters[0].ForceFields[2].Strength == -35.0f, "Upは異なるfield値を一つ前へ実際に移し、選択を保つ" );
		C.Check( Input.Press( State, "Down##ForceFieldDown" ) && State.Document.Get().Emitters[0].ForceFields.Num() == 3 && State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 1 ) && State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 2 ) && State.SelectedForceField == 2 && State.Document.Get().Emitters[0].ForceFields[1].Strength == -35.0f && State.Document.Get().Emitters[0].ForceFields[2].Strength == -45.0f, "Downは異なるfield値を一つ後ろへ実際に移し、選択を保つ" );
		C.Check( Input.Press( State, "Delete##ForceFieldDelete" ) && State.Document.Get().Emitters[0].ForceFields.Num() == 2 && State.SelectedForceField == 1, "末尾fieldの削除で残る最後のfieldを選択する" );
		C.Check( Input.TabTo( State, Input.ItemId( "Add##ForceFieldAdd" ) ), "末尾削除後もTabで実際のAddボタンへ再び移動できる" );
		C.Check( State.Document.Undo() && State.Document.Get().Emitters[0].ForceFields.Num() == 3 && State.Document.Redo() && State.Document.Get().Emitters[0].ForceFields.Num() == 2, "構造操作の最後の削除をUndo/Redoできる" );
		C.Check( State.Document.Undo() && State.Document.Get().Emitters[0].ForceFields.Num() == 3 && State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 2 ) && State.Document.Get().Emitters[0].ForceFields[2].Strength == -45.0f, "Redo後に戻した末尾fieldの全設定を復元する" );
		C.Check( State.Document.Undo() && State.Document.Get().Emitters[0].ForceFields.Num() == 3 && State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 1 ) && State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 2 ) && State.Document.Get().Emitters[0].ForceFields[1].Strength == -45.0f && State.Document.Get().Emitters[0].ForceFields[2].Strength == -35.0f, "DownをUndoすると二つの異なるfield値が元の順へ戻る" );
		C.Check( State.Document.Undo() && State.Document.Get().Emitters[0].ForceFields.Num() == 3 && State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 1 ) && State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 2 ) && State.Document.Get().Emitters[0].ForceFields[1].Strength == -35.0f && State.Document.Get().Emitters[0].ForceFields[2].Strength == -45.0f, "UpをUndoすると二つの異なるfield値が元の順へ戻る" );
		C.Check( State.Document.Undo() && State.Document.Get().Emitters[0].ForceFields.Num() == 3 && State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 2 ) && State.Document.Get().Emitters[0].ForceFields[2].Strength == -35.0f, "複製後の値編集をUndoして複製元と同じ値へ戻す" );
		C.Check( State.Document.Undo() && State.Document.Get().Emitters[0].ForceFields.Num() == 2 && State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 1 ) && State.Document.Get().Emitters[0].ForceFields[1].ComputeHash() == Custom.ComputeHash(), "DuplicateをUndoすると追加分だけが消える" );
		C.Check( State.Document.Undo() && State.Document.Get().Emitters[0].ForceFields.Num() == 2 && State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 1 ) && State.Document.Get().Emitters[0].ForceFields[1].Acceleration.x == 1.0f, "構造操作前の直接編集も独立した履歴へ戻る" );
		C.Check( State.Document.Undo() && State.Document.Get().Emitters[0].ForceFields.Num() == 1, "AddをUndoすると最初のfield数へ戻る" );
		if ( !State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 0 ) ) return;
		// Reset確認用fieldを選択する状態。
		State.SelectedForceField = 0;
		// Reset前の保存値を書き換える文書参照。
		FParticleEffectDefinition& BeforeReset = State.Document.BeginEdit();
		BeforeReset.Emitters[0].ForceFields[0].Position = FVector3( 5.0f, 6.0f, 7.0f );
		BeforeReset.Emitters[0].ForceFields[0].Strength = -8.0f;
		State.Document.EndEditSession();
		Input.Frame( State );
		// Resetボタンの実入力結果。
		const bool bResetFocused = Input.Press( State, "Reset##ForceFieldReset" );
		// Reset後に照合するfield。
		const FEffectForceFieldDefinition& Reset = State.Document.Get().Emitters[0].ForceFields[0];
		C.Check( bResetFocused && Reset.Kind == FEffectForceFieldDefinition::EKind::Uniform && Reset.bEnabled && Reset.Position.x == 0.0f && Reset.Position.y == 0.0f && Reset.Position.z == 0.0f && Reset.Radius == 1.0f && Reset.Strength == 0.0f, "Resetは有効中のfield全体を既定値へ置き換える" );
		C.Check( State.Document.Undo() && State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 0 ) && State.Document.Get().Emitters[0].ForceFields[0].Position.x == 5.0f && State.Document.Get().Emitters[0].ForceFields[0].Strength == -8.0f && State.Document.Redo() && State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 0 ) && State.Document.Get().Emitters[0].ForceFields[0].Radius == 1.0f, "Resetも値を保ったUndo/Redoができる" );
		C.Check( Input.Press( State, "Delete##ForceFieldDelete" ) && State.Document.Get().Emitters[0].ForceFields.IsEmpty(), "最後のfieldを実入力で削除して一覧を空にできる" );
		// 空一覧で唯一有効なAdd項目へTab移動し、追加し直せるか。
		const bool bEmptyAddFocused = Input.TabTo( State, Input.ItemId( "Add##ForceFieldAdd" ) );
		const bool bEmptyAddRestored = bEmptyAddFocused && Input.Press( State, "Add##ForceFieldAdd" ) && State.Document.Get().Emitters[0].ForceFields.Num() == 1 && State.Document.Get().Emitters[0].ForceFields[0].Acceleration.x == 1.0f;
		C.Check( bEmptyAddRestored, "最後のfield削除後もTabでAddへ移り新しいfieldを追加できる" );
	}

	// 非有限・型不正・上限超過をImGuiへ渡さず、削除とResetで復旧できる。
	void TestForceFieldInvalidInputs( NAssetTest::FChecker& C, FForceFieldInputContext& Input )
	{
		C.Group = "effect-force-field-editor-invalid";
		// guide描画に使う既知の場の文書。
		FParticleEffectDefinition Definition = MakeForceFieldInputDefinition();
		// 不正fieldを組み込む対象emitter。
		FParticleEmitterDefinition& Emitter = Definition.Emitters[0];
		Emitter.ForceFields[0].Acceleration.x = ( std::numeric_limits<float32>::quiet_NaN )();
		// JSON型不正の印を持つfield。
		FEffectForceFieldDefinition InvalidType;
		// finite値を保ったままEnabledの型だけを壊した入力。
		const NlohmannJson InvalidJson = NlohmannJson::parse( "{\"Enabled\":null,\"Kind\":\"Radial\",\"Radius\":8,\"Strength\":-3}" );
		// invalid type markerを読取時に設定できたか。
		const bool bInvalidRead = FEffectForceFieldDefinition::FromJson( InvalidJson, InvalidType );
		Emitter.ForceFields.Add( InvalidType );
		// 17件になるまで保持する既定fieldの番号。
		for ( int32 Index = 2; Index < FEffectForceFieldDefinition::MaxFields + 1; ++Index ) Emitter.ForceFields.Add( FEffectForceFieldDefinition{} );
		// 不正値を持つ文書を表示するeditor状態。
		FEffectEditorState State;
		State.Document.Reset( Definition );
		State.SelectedForceField = 0;
		Input.Frame( State );
		C.Check( bInvalidRead && State.Document.Get().Emitters[0].ForceFields.Num() == FEffectForceFieldDefinition::MaxFields + 1 && std::isnan( State.Document.Get().Emitters[0].ForceFields[0].Acceleration.x ) && Input.HasFiniteVertices(), "17件とNaNを保持したまま数値widgetへ渡さず有限なImGui描画を作る" );
		// 上限超過でも有効なDelete項目へTabを送り、古いNavIdを切り替える。
		const bool bNaNDeleteFocused = Input.TabTo( State, Input.ItemId( "Delete##ForceFieldDelete" ) );
		// 既知の別項目からResetへ実Tab入力が届くか。
		const bool bNaNResetFocused = bNaNDeleteFocused && Input.TabTo( State, Input.ItemId( "Reset##ForceFieldReset" ) );
		// Resetから移動してNaN fieldのStrengthがTab候補に現れるか。
		const bool bNaNStrengthFocused = Input.TabTo( State, Input.FieldItemId( "Strength (m/s2)##ForceFieldStrength" ), 128 );
		C.Check( bNaNResetFocused && !bNaNStrengthFocused, "Resetへfocusを確定してからTabを巡回してもNaN fieldのStrength widgetは存在しない" );
		C.Check( Input.Press( State, "Reset##ForceFieldReset" ) && State.Document.Get().Emitters[0].ForceFields.Num() == FEffectForceFieldDefinition::MaxFields + 1 && State.Document.Get().Emitters[0].ForceFields[0].Validate().IsEmpty() && std::isfinite( State.Document.Get().Emitters[0].ForceFields[0].Acceleration.x ), "NaNを含む場はResetで全体を既定値へ修復し、超過件数を残す" );
		State.SelectedForceField = 1;
		Input.Frame( State );
		if ( !State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 1 ) )
		{
			C.Check( false, "型不正fieldの配列位置をReset後も保持する" );
			return;
		}
		// Reset前にJSON値を保持しているfield。
		const FEffectForceFieldDefinition& RetainedInvalid = State.Document.Get().Emitters[0].ForceFields[1];
		C.Check( RetainedInvalid.Validate().Num() > 0 && RetainedInvalid.ToJson().contains( "Enabled" ) && RetainedInvalid.ToJson()["Enabled"].is_null() && Input.HasFiniteVertices(), "finite設定のまま型不正印を保持し、警告を表示する" );
		// 型不正fieldでも使えるDeleteへTabを送り、選択変更前のNavIdを切り替える。
		const bool bInvalidDeleteFocused = Input.TabTo( State, Input.ItemId( "Delete##ForceFieldDelete" ) );
		// 既知の別項目からResetへ実Tab入力が届くか。
		const bool bInvalidResetFocused = bInvalidDeleteFocused && Input.TabTo( State, Input.ItemId( "Reset##ForceFieldReset" ) );
		// Resetから移動して型不正fieldのRadiusがTab候補に現れるか。
		const bool bInvalidRadiusFocused = Input.TabTo( State, Input.FieldItemId( "Radius (m)##ForceFieldRadius" ), 128 );
		C.Check( bInvalidResetFocused && !bInvalidRadiusFocused, "Resetへfocusを確定してからTabを巡回しても型不正fieldのRadius widgetは存在しない" );
		C.Check( Input.Press( State, "Reset##ForceFieldReset" ) && State.Document.Get().Emitters[0].ForceFields.Num() == FEffectForceFieldDefinition::MaxFields + 1 && State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 1 ) && State.Document.Get().Emitters[0].ForceFields[1].Validate().IsEmpty(), "型不正fieldはResetで修復し上限超過の既存配列を切り詰めない" );
		State.SelectedForceField = State.Document.Get().Emitters[0].ForceFields.Num() - 1;
		Input.Frame( State );
		C.Check( Input.Press( State, "Delete##ForceFieldDelete" ) && State.Document.Get().Emitters[0].ForceFields.Num() == FEffectForceFieldDefinition::MaxFields, "上限超過中でも削除は使え、利用者が一件ずつ上限へ戻せる" );
		C.Check( !Input.Press( State, "Add##ForceFieldAdd", 96 ) && State.Document.Get().Emitters[0].ForceFields.Num() == FEffectForceFieldDefinition::MaxFields, "16件では追加を受け付けず件数を保つ" );
		C.Check( !Input.Press( State, "Duplicate##ForceFieldDuplicate", 96 ) && State.Document.Get().Emitters[0].ForceFields.Num() == FEffectForceFieldDefinition::MaxFields, "16件では複製を受け付けず件数を保つ" );
	}

	// Comboを開き、現在の項目から指定数だけ下（正）または上（負）へ動かして確定する。
	void ChooseForceFieldKind( FForceFieldInputContext& Input, FEffectEditorState& State, int32 Steps )
	{
		Input.Tap( State, ImGuiKey_Enter );
		// 項目を動かす回数。
		const int32 StepCount = Steps < 0 ? -Steps : Steps;
		for ( int32 Step = 0; Step < StepCount; ++Step ) Input.Tap( State, Steps < 0 ? ImGuiKey_UpArrow : ImGuiKey_DownArrow );
		Input.Tap( State, ImGuiKey_Enter );
	}

	// Vortex・Turbulence・追従・回転軸・周波数・スクロールを実入力で編集し、保存値・履歴・復旧を確かめる。
	void TestForceFieldKindInputs( NAssetTest::FChecker& C, FForceFieldInputContext& Input )
	{
		C.Group = "effect-force-field-editor-kinds";
		// 既知のRadial fieldを持つ編集状態。
		FEffectEditorState State;
		State.Document.Reset( MakeForceFieldInputDefinition() );
		Input.Frame( State );
		// 現在の編集対象fieldを毎回文書から読み直す関数。
		const auto Field = [&State]() -> const FEffectForceFieldDefinition& { return State.Document.Get().Emitters[0].ForceFields[0]; };
		// 入力前の履歴数。
		const int32 InitialUndo = State.Document.GetNumUndo();
		// Kind comboへTabで移れたか。
		const bool bKindFocused = Input.TabTo( State, Input.FieldItemId( "Kind##ForceFieldKind" ) );
		ChooseForceFieldKind( Input, State, 1 );
		C.Check( bKindFocused && Field().Kind == FEffectForceFieldDefinition::EKind::Vortex && Field().Radius == 8.0f && Field().Strength == -9.0f && Field().Position == FVector3( 5.0f, 6.0f, 7.0f ) && State.Document.GetNumUndo() == InitialUndo + 1, "Kindを実Comboの下矢印でRadialからVortexへ変え、他の保存値を保つ" );
		// Follow emitter checkboxへTabで移れたか。
		const bool bFollowFocused = Input.TabTo( State, Input.FieldItemId( "Follow emitter##ForceFieldFollowEmitter" ) );
		Input.Tap( State, ImGuiKey_Space );
		C.Check( bFollowFocused && Field().bFollowEmitter && State.Document.GetNumUndo() == InitialUndo + 2, "Follow emitterを実チェックボックスで有効にし、履歴を一つ作る" );
		// 回転軸3成分の入力結果。
		const bool bAxisX = Input.ReplaceNumeric( State, Input.FieldItemId( "Axis##ForceFieldAxis", 0 ), "3" );
		const bool bAxisZ = Input.ReplaceNumeric( State, Input.FieldItemId( "Axis##ForceFieldAxis", 2 ), "4" );
		const bool bAxisY = Input.ReplaceNumeric( State, Input.FieldItemId( "Axis##ForceFieldAxis", 1 ), "0" );
		C.Check( bAxisX && bAxisY && bAxisZ && Field().Axis == FVector3( 3.0f, 0.0f, 4.0f ) && Field().Validate().IsEmpty(), "Axisの3成分を実DragFloat3文字入力で編集する" );
		Input.ReplaceNumeric( State, Input.FieldItemId( "Axis##ForceFieldAxis", 0 ), "0" );
		Input.ReplaceNumeric( State, Input.FieldItemId( "Axis##ForceFieldAxis", 2 ), "0" );
		C.Check( Field().Axis == FVector3( 0.0f, 0.0f, 4.0f ) && Field().Validate().IsEmpty(), "全成分がゼロになるAxis入力は保存へ反映せず、直前の有効な軸を保つ" );
		Input.ReplaceNumeric( State, Input.FieldItemId( "Axis##ForceFieldAxis", 0 ), "3" );
		// 周波数をゼロへ入力しても、UIの下限で正の値に保たれるか。
		const bool bFrequencyZero = Input.ReplaceNumeric( State, Input.FieldItemId( "Frequency (rad/m)##ForceFieldFrequency" ), "0" );
		C.Check( bFrequencyZero && Field().Frequency > 0.0f && Field().Frequency <= 0.0011f && Field().Validate().IsEmpty(), "Frequencyへ0を入力しても下限で正の値に保ち、検証エラーを作らない" );
		const bool bFrequency = Input.ReplaceNumeric( State, Input.FieldItemId( "Frequency (rad/m)##ForceFieldFrequency" ), "2.5" );
		const bool bScroll = Input.ReplaceNumeric( State, Input.FieldItemId( "Scroll speed (rad/s)##ForceFieldScrollSpeed" ), "-3" );
		C.Check( bFrequency && bScroll && Field().Frequency == 2.5f && Field().ScrollSpeed == -3.0f && Field().Axis == FVector3( 3.0f, 0.0f, 4.0f ), "FrequencyとScroll speedを実数値入力で編集し、負のScroll speedも保存する" );
		// Turbulenceへ切り替えた後に、使わない値も保たれるか。
		Input.TabTo( State, Input.FieldItemId( "Kind##ForceFieldKind" ) );
		ChooseForceFieldKind( Input, State, 1 );
		C.Check( Field().Kind == FEffectForceFieldDefinition::EKind::Turbulence && Field().bFollowEmitter && Field().Axis == FVector3( 3.0f, 0.0f, 4.0f ) && Field().Frequency == 2.5f && Field().ScrollSpeed == -3.0f && Field().Radius == 8.0f, "KindをTurbulenceへ変えても全設定値を保つ" );
		Input.TabTo( State, Input.FieldItemId( "Kind##ForceFieldKind" ) );
		ChooseForceFieldKind( Input, State, -3 );
		C.Check( Field().Kind == FEffectForceFieldDefinition::EKind::Uniform && Field().bFollowEmitter && Field().Axis == FVector3( 3.0f, 0.0f, 4.0f ) && Field().Frequency == 2.5f && Field().Strength == -9.0f, "Uniformへ戻しても、使わないAxis・Frequency・追従設定を保つ" );
		// Reset前の履歴数。
		const int32 BeforeReset = State.Document.GetNumUndo();
		const bool bResetPressed = Input.Press( State, "Reset##ForceFieldReset" );
		C.Check( bResetPressed && Field().Kind == FEffectForceFieldDefinition::EKind::Uniform && !Field().bFollowEmitter && Field().Axis == FVector3::Up && Field().Frequency == 1.0f && Field().ScrollSpeed == 1.0f && State.Document.GetNumUndo() == BeforeReset + 1, "Resetは追従・Axis・Frequency・Scroll speedも既定値へ戻し、履歴を一つ作る" );
		C.Check( State.Document.Undo() && Field().bFollowEmitter && Field().Axis == FVector3( 3.0f, 0.0f, 4.0f ) && Field().Frequency == 2.5f && Field().ScrollSpeed == -3.0f, "ResetのUndoで追従・Axis・Frequency・Scroll speedが戻る" );
		C.Check( Input.AreFramesBalanced(), "Vortex・Turbulence入力の確認中にImGui frameとwindow stackを閉じる" );
	}

	// ゼロ軸・非正の周波数・型違いの追従指定は数値入力を隠し、Resetで復旧できる。
	void TestForceFieldKindInvalidInputs( NAssetTest::FChecker& C, FForceFieldInputContext& Input )
	{
		C.Group = "effect-force-field-editor-kind-invalid";
		// ゼロ軸と非正の周波数を持つVortex。
		FParticleEffectDefinition Definition = MakeForceFieldInputDefinition();
		FEffectForceFieldDefinition& Zero = Definition.Emitters[0].ForceFields[0];
		Zero.Kind = FEffectForceFieldDefinition::EKind::Vortex;
		Zero.Axis = FVector3::Zero;
		Zero.Frequency = 0.0f;
		// 型違いのFollowEmitterを持つ二つ目のfield。
		FEffectForceFieldDefinition InvalidFollow;
		const bool bFollowRead = FEffectForceFieldDefinition::FromJson( NlohmannJson::parse( "{\"Kind\":\"Vortex\",\"FollowEmitter\":1,\"Radius\":4}" ), InvalidFollow );
		Definition.Emitters[0].ForceFields.Add( InvalidFollow );
		// 不正値を表示する編集状態。
		FEffectEditorState State;
		State.Document.Reset( Definition );
		State.SelectedForceField = 0;
		Input.Frame( State );
		// 現在の編集対象fieldを毎回文書から読み直す関数。
		const auto FieldAt = [&State]( int32 Index ) -> const FEffectForceFieldDefinition& { return State.Document.Get().Emitters[0].ForceFields[Index]; };
		C.Check( bFollowRead && FieldAt( 0 ).Validate().Num() >= 2 && FieldAt( 1 ).Validate().Num() >= 1 && Input.HasFiniteVertices(), "ゼロ軸・非正のFrequency・型違いのFollowEmitterを検証で報告する" );
		// ゼロ軸のAxis widgetが存在せず、Tab候補にならないか。
		const bool bAxisReachable = Input.TabTo( State, Input.FieldItemId( "Axis##ForceFieldAxis", 0 ), 128 );
		// 非正Frequencyのwidgetが存在せず、Tab候補にならないか。
		const bool bFrequencyReachable = Input.TabTo( State, Input.FieldItemId( "Frequency (rad/m)##ForceFieldFrequency" ), 128 );
		C.Check( !bAxisReachable && !bFrequencyReachable, "ゼロ軸・非正Frequencyのfieldでは数値widgetを隠す" );
		C.Check( Input.Press( State, "Reset##ForceFieldReset" ) && FieldAt( 0 ).Validate().IsEmpty() && FieldAt( 0 ).Kind == FEffectForceFieldDefinition::EKind::Uniform && FieldAt( 0 ).Axis == FVector3::Up && FieldAt( 0 ).Frequency == 1.0f && FieldAt( 1 ).Validate().Num() >= 1, "Resetは選択したゼロ軸・非正Frequency fieldだけを既定値へ復旧する" );
		State.SelectedForceField = 1;
		Input.Frame( State );
		// 型違い追従のfieldでもResetで復旧できるか。
		const bool bFollowResetPressed = Input.Press( State, "Reset##ForceFieldReset" );
		C.Check( bFollowResetPressed && FieldAt( 1 ).Validate().IsEmpty() && !FieldAt( 1 ).bFollowEmitter, "型違いのFollowEmitterもResetで復旧する" );
		// 非有限のAxisを持つfield。
		FEffectForceFieldDefinition NaNAxisField;
		NaNAxisField.Axis.x = ( std::numeric_limits<float32>::quiet_NaN )();
		Definition.Emitters[0].ForceFields.Add( NaNAxisField );
		// 非有限のFrequencyを持つfield。
		FEffectForceFieldDefinition NaNFreqField;
		NaNFreqField.Frequency = ( std::numeric_limits<float32>::quiet_NaN )();
		Definition.Emitters[0].ForceFields.Add( NaNFreqField );
		// 非有限のScrollSpeedを持つfield。
		FEffectForceFieldDefinition NaNSpeedField;
		NaNSpeedField.ScrollSpeed = ( std::numeric_limits<float32>::quiet_NaN )();
		Definition.Emitters[0].ForceFields.Add( NaNSpeedField );
		State.Document.Reset( Definition );
		// NaNのAxisを持つfieldの入力欄が隠れるか。
		State.SelectedForceField = 2;
		Input.Frame( State );
		const bool bNaNAxisReachable = Input.TabTo( State, Input.FieldItemId( "Radius (m)##ForceFieldRadius" ), 64 );
		C.Check( !bNaNAxisReachable && Input.Press( State, "Reset##ForceFieldReset" ) && std::isfinite( FieldAt( 2 ).Axis.x ), "NaNのAxisを持つfieldは数値widgetを隠し、Resetで有限値へ復旧する" );
		// NaNのFrequencyを持つfieldの入力欄が隠れるか。
		State.SelectedForceField = 3;
		Input.Frame( State );
		const bool bNaNFreqReachable = Input.TabTo( State, Input.FieldItemId( "Radius (m)##ForceFieldRadius" ), 64 );
		C.Check( !bNaNFreqReachable && Input.Press( State, "Reset##ForceFieldReset" ) && std::isfinite( FieldAt( 3 ).Frequency ), "NaNのFrequencyを持つfieldは数値widgetを隠し、Resetで有限値へ復旧する" );
		// NaNのScrollSpeedを持つfieldの入力欄が隠れるか。
		State.SelectedForceField = 4;
		Input.Frame( State );
		const bool bNaNSpeedReachable = Input.TabTo( State, Input.FieldItemId( "Radius (m)##ForceFieldRadius" ), 64 );
		C.Check( !bNaNSpeedReachable && Input.Press( State, "Reset##ForceFieldReset" ) && std::isfinite( FieldAt( 4 ).ScrollSpeed ), "NaNのScrollSpeedを持つfieldは数値widgetを隠し、Resetで有限値へ復旧する" );
	}
}

namespace NWorldTest
{
	// 実パネルを独立した入力文脈で操作し、力場の値・履歴・復旧を確認する。
	void RunEffectForceFieldEditorInputTests( NAssetTest::FChecker& C )
	{
		C.Group = "effect-force-field-editor-context";
		// 入力確認の前に呼出元が使っていたcontext。
		ImGuiContext* const Previous = ImGui::GetCurrentContext();
		{
			// この範囲だけで所有する実入力context。
			FForceFieldInputContext Input;
			C.Check( Input.IsReady(), "独立したImGui contextのfont atlasを用意する" );
			if ( Input.IsReady() )
			{
				TestForceFieldValueInputs( C, Input );
				TestForceFieldStructureInputs( C, Input );
				TestForceFieldInvalidInputs( C, Input );
				TestForceFieldKindInputs( C, Input );
				TestForceFieldKindInvalidInputs( C, Input );
			}
		}
		C.Check( ImGui::GetCurrentContext() == Previous, "力場の入力確認後に呼出元のImGui contextへ戻す" );
	}

	// 実プレビューへガイドを重ね、guideなしの画素と描画状態・資源寿命を比べる。
	void RunEffectForceFieldPreviewRealTests( NAssetTest::FChecker& C, CAssetManager&, IDrawExecutor& Executor )
	{
		C.Group = "effect-force-field-preview-real";
		// ガイドを描く既知のpreview定義。
		FEffectEditorState State;
		// Radial guideの中心と半径を固定した比較文書。
		FParticleEffectDefinition Definition = MakeForceFieldInputDefinition();
		Definition.Emitters[0].ForceFields[0].Kind = FEffectForceFieldDefinition::EKind::Radial;
		Definition.Emitters[0].ForceFields[0].Position = FVector3( 0.0f, 1.0f, 0.0f );
		Definition.Emitters[0].ForceFields[0].Radius = 1.5f;
		State.Document.Reset( Definition );
		// Guide命令を所有する描画buffer。
		FDrawBuffer GuideBuffer;
		// ガイドを既存描画形式へ記録する窓口。
		FDrawContext GuideDraw( GuideBuffer );
		NEffectEditorUI::RecordForceFieldGuides( State, GuideDraw );
		// 実previewへ同期して渡すguide命令範囲。
		const FDrawSpan GuideSpan { GuideBuffer.Words.data(), static_cast<int32>( GuideBuffer.Words.size() ) };
		C.Check( GuideSpan.NumWords > 0, "有効なRadial場を既存DrawContextへ範囲ガイドとして記録する" );
		// guideを正面から見るpreview camera。
		FEditorCamera Camera;
		Camera.Target = FVector3( 0.0f, 1.0f, 0.0f );
		Camera.Distance = 6.0f;
		// 実画素を読み取るpreview範囲。
		FEditorViewport View;
		View.Width = 180;
		View.Height = 120;
		// 描画中に所有する専用preview画面。
		FEffectPreviewSurface Surface;
		// preview前に呼出元が使っていた描画先。
		const int32 CallerScreen = GetDrawScreen();
		// guideなしpreviewと互換呼出しに使う空overlay。
		const FDrawSpan EmptySpan { nullptr, 0 };
		// guideなしの基準画像を読むsoft image。
		const int32 BaselineSoft = MakeARGB8ColorSoftImage( View.Width, View.Height );
		// overlayなし実描画が成功したか。
		const bool bBaselineRendered = Surface.Render( Camera, View, nullptr, Executor ) && Surface.GetHandle() >= 0;
		// 基準画像をsoft imageへ取得できるか。
		bool bBaselineRead = bBaselineRendered && BaselineSoft >= 0;
		if ( bBaselineRead )
		{
			SetDrawScreen( Surface.GetHandle() );
			GetDrawScreenSoftImage( 0, 0, View.Width, View.Height, BaselineSoft );
			SetDrawScreen( CallerScreen );
		}
		C.Check( bBaselineRead, "ガイドなし従来previewの実画素を読み取れる" );
		// resize前に再利用する描画画面。
		const int32 BaselineHandle = Surface.GetHandle();
		C.Check( Surface.Render( Camera, View, nullptr, Executor, GuideSpan ) && Surface.GetHandle() == BaselineHandle, "ガイドspan付きの実previewを同じ画面へ描画する" );
		// guideあり画像を読むsoft image。
		const int32 GuideSoft = MakeARGB8ColorSoftImage( View.Width, View.Height );
		// guideあり画像の取得可能性。
		bool bGuideImageRead = GuideSoft >= 0 && Surface.GetHandle() >= 0;
		if ( bGuideImageRead )
		{
			SetDrawScreen( Surface.GetHandle() );
			GetDrawScreenSoftImage( 0, 0, View.Width, View.Height, GuideSoft );
			SetDrawScreen( CallerScreen );
		}
		C.Check( bGuideImageRead, "力場ガイド付きpreviewの実画素を読み取れる" );
		// malformed overlayが失敗して画面状態を保つ確認関数。
		const auto RejectOverlay = [&]( FDrawSpan Span, const char* Reason )
		{
			// 不正overlayが拒否され既存描画状態を保つか。
			const bool bRejected = !Surface.Render( Camera, View, nullptr, Executor, Span ) && Surface.GetHandle() == BaselineHandle && GetDrawScreen() == CallerScreen;
			C.Check( bRejected, "%s overlayは描画せず既存画面と呼出元を保つ", Reason );
		};
		RejectOverlay( FDrawSpan { GuideSpan.Words, -1 }, "負の長さ" );
		RejectOverlay( FDrawSpan { nullptr, 1 }, "データなしの正長" );
		RejectOverlay( FDrawSpan { GuideSpan.Words, ( std::numeric_limits<int32>::max )() }, "許容範囲を超える長さ" );
		// payloadを欠かせるLine3Dの短い命令。
		FDrawBuffer TruncatedLine;
		TruncatedLine.Words.push_back( NDrawCommand::MakeHeader( EDrawCommand::Line3D, 0, 1 ) );
		RejectOverlay( FDrawSpan { TruncatedLine.Words.data(), static_cast<int32>( TruncatedLine.Words.size() ) }, "payloadの短いLine3D" );
		// payloadのないSphere3Dを示す不正命令。
		FDrawBuffer TruncatedSphere;
		TruncatedSphere.Words.push_back( NDrawCommand::MakeHeader( EDrawCommand::Sphere3D, 0, 1 ) );
		RejectOverlay( FDrawSpan { TruncatedSphere.Words.data(), static_cast<int32>( TruncatedSphere.Words.size() ) }, "payloadの短いSphere3D" );
		// Sphere payloadを持つ一命令の検査bufferを作る関数。
		const auto MakeSphereSpan = []( FDrawSphere3DData Data, uint8 Flags )
		{
			// 検査するSphere3D一つの命令。
			FDrawBuffer Buffer;
			// Sphere payloadの4 byte word数。
			const int32 DataWords = static_cast<int32>( sizeof( FDrawSphere3DData ) / sizeof( uint32 ) );
			Buffer.Words.resize( static_cast<size_t>( DataWords + 1 ) );
			Buffer.Words[0] = NDrawCommand::MakeHeader( EDrawCommand::Sphere3D, Flags, DataWords + 1 );
			std::memcpy( Buffer.Words.data() + 1, &Data, sizeof( Data ) );
			return Buffer;
		};
		// NaNや分割数を持つ拒否対象sphere。
		FDrawSphere3DData BadSphereData{};
		BadSphereData.Center[0] = ( std::numeric_limits<float32>::quiet_NaN )();
		BadSphereData.Radius = 1.0f;
		BadSphereData.Div = 16;
		// NaN座標を持つSphere3D命令。
		FDrawBuffer NonFiniteSphere = MakeSphereSpan( BadSphereData, 0 );
		RejectOverlay( FDrawSpan { NonFiniteSphere.Words.data(), static_cast<int32>( NonFiniteSphere.Words.size() ) }, "非有限座標のSphere3D" );
		BadSphereData.Center[0] = 0.0f;
		BadSphereData.Radius = 1.0f;
		BadSphereData.Div = 2;
		// 範囲外分割数を持つSphere3D命令。
		FDrawBuffer InvalidDivSphere = MakeSphereSpan( BadSphereData, 0 );
		RejectOverlay( FDrawSpan { InvalidDivSphere.Words.data(), static_cast<int32>( InvalidDivSphere.Words.size() ) }, "不正分割数のSphere3D" );
		BadSphereData.Div = 16;
		// 対象外のfill flagを持つSphere3D命令。
		FDrawBuffer FilledSphere = MakeSphereSpan( BadSphereData, FDrawFlag::Fill );
		RejectOverlay( FDrawSpan { FilledSphere.Words.data(), static_cast<int32>( FilledSphere.Words.size() ) }, "塗りつぶしSphere3D" );
		// 対応していない種類を持つ命令。
		FDrawBuffer UnknownCommand;
		UnknownCommand.Words.push_back( NDrawCommand::MakeHeader( static_cast<EDrawCommand>( 255 ), 0, 1 ) );
		RejectOverlay( FDrawSpan { UnknownCommand.Words.data(), static_cast<int32>( UnknownCommand.Words.size() ) }, "未対応命令" );
		State.bShowForceFieldGuides = false;
		GuideBuffer.Clear();
		// guide無効時の空記録を確認する描画窓口。
		FDrawContext HiddenDraw( GuideBuffer );
		NEffectEditorUI::RecordForceFieldGuides( State, HiddenDraw );
		// guide無効時に命令を記録せず描画できたか。
		const bool bHiddenRendered = GuideBuffer.Words.empty() && Surface.Render( Camera, View, nullptr, Executor, EmptySpan );
		// guide無効の出力を読むsoft image。
		const int32 HiddenSoft = MakeARGB8ColorSoftImage( View.Width, View.Height );
		// guide無効と基準previewの全画素一致状態。
		bool bImagesMatch = bGuideImageRead && bHiddenRendered && BaselineSoft >= 0 && HiddenSoft >= 0;
		if ( bImagesMatch )
		{
			SetDrawScreen( Surface.GetHandle() );
			GetDrawScreenSoftImage( 0, 0, View.Width, View.Height, HiddenSoft );
			SetDrawScreen( CallerScreen );
			// 各画素を比較する行番号。
			for ( int32 Y = 0; Y < View.Height && bImagesMatch; ++Y )
			{
				// 各画像の横位置。
				for ( int32 X = 0; X < View.Width; ++X )
				{
					// guide画像のRGBA成分。
					int32 GuideR = 0, GuideG = 0, GuideB = 0, GuideA = 0;
					// guide無効画素のRGBA値。
					int32 HiddenR = 0, HiddenG = 0, HiddenB = 0, HiddenA = 0;
					GetPixelSoftImage( BaselineSoft, X, Y, &GuideR, &GuideG, &GuideB, &GuideA );
					GetPixelSoftImage( HiddenSoft, X, Y, &HiddenR, &HiddenG, &HiddenB, &HiddenA );
					if ( GuideR != HiddenR || GuideG != HiddenG || GuideB != HiddenB || GuideA != HiddenA ) { bImagesMatch = false; break; }
				}
			}
		}
		C.Check( bImagesMatch, "guide非表示は同じcameraの旧previewと全画素を一致させる" );
		// 球・矢印が投影される中央領域に変化があるか。
		bool bGuidePixelsDiffer = false;
		// wire guideの投影外で基準画像を保つか。
		bool bGuideOutsidePixelsMatch = bGuideImageRead && BaselineSoft >= 0;
		if ( bGuideImageRead && BaselineSoft >= 0 )
		{
			// 既知cameraと球半径に対する、余裕を含む中央投影領域の行。
			for ( int32 Y = 0; Y < View.Height; ++Y )
			{
				// 今回調べる画像の横位置。
				for ( int32 X = 0; X < View.Width; ++X )
				{
					// guideと基準画像のRGBA成分。
					int32 GuideR = 0, GuideG = 0, GuideB = 0, GuideA = 0;
					// guideなし基準画素のRGBA値。
					int32 BaseR = 0, BaseG = 0, BaseB = 0, BaseA = 0;
					GetPixelSoftImage( GuideSoft, X, Y, &GuideR, &GuideG, &GuideB, &GuideA );
					GetPixelSoftImage( BaselineSoft, X, Y, &BaseR, &BaseG, &BaseB, &BaseA );
					// 既知guideが必ず収まる中央の矩形内か。
					const bool bInsideGuideRegion = X >= 35 && X <= 145 && Y >= 5 && Y <= 115;
					if ( GuideR != BaseR || GuideG != BaseG || GuideB != BaseB || GuideA != BaseA )
					{
						if ( bInsideGuideRegion ) bGuidePixelsDiffer = true;
						else bGuideOutsidePixelsMatch = false;
					}
				}
			}
		}
		C.Check( bGuideImageRead && bGuidePixelsDiffer && bGuideOutsidePixelsMatch, "有効なguideは既知の中央ROIに描かれ、投影外の全画素を変えない" );
		if ( BaselineSoft >= 0 ) DeleteSoftImage( BaselineSoft );
		if ( GuideSoft >= 0 ) DeleteSoftImage( GuideSoft );
		if ( HiddenSoft >= 0 ) DeleteSoftImage( HiddenSoft );
		// resize直前にsurfaceが所有する画面。
		const int32 PreviousHandle = Surface.GetHandle();
		View.Width = 240;
		C.Check( Surface.Render( Camera, View, nullptr, Executor, EmptySpan ) && Surface.GetHandle() != PreviousHandle, "overlay付きsurfaceはresize時に新画面へ切り替える" );
		Surface.Release();
		Surface.Release();
		C.Check( Surface.GetHandle() < 0 && GetDrawScreen() == CallerScreen, "overlay付きsurfaceの繰り返しRelease後に所有画面を解放する" );
	}
}
