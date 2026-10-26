// SPDX-License-Identifier: NOASSERTION
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/EffectEditorInternal.h"
#include "VendorModules/ImGui/imgui_internal.h"
#include "AssetSelfTestSupport.h"

#include <cmath>
#include <limits>

namespace
{
	// ゲーム設定と入力先を隔離して、Effect Editor の実パネルだけを操作する。
	class FMaterialInputContext
	{
	public:
		// 独立文脈を作り、ini・log・画面出力を使わずにフォントを用意する。
		FMaterialInputContext() : Previous( ImGui::GetCurrentContext() ), Context( ImGui::CreateContext() )
		{
			ImGui::SetCurrentContext( Context );
			ImGuiIO& Input = ImGui::GetIO();
			Input.DisplaySize = ImVec2( 1800.0f, 1400.0f );
			Input.DeltaTime = 1.0f / 60.0f;
			Input.IniFilename = nullptr;
			Input.LogFilename = nullptr;
			Input.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			Input.ConfigInputTrickleEventQueue = false;
			Input.Fonts->AddFontDefaultBitmap();
			bFontsReady = Input.Fonts->Build();
		}

		// この文脈だけを破棄し、テスト前に使われていた文脈へ戻す。
		~FMaterialInputContext()
		{
			ImGui::DestroyContext( Context );
			ImGui::SetCurrentContext( Previous );
		}
		// 文脈を複製せず、フォントと入力状態の所有を一つにする。
		FMaterialInputContext( const FMaterialInputContext& ) = delete;
		// 文脈を代入せず、ImGui の共有所有を防ぐ。
		FMaterialInputContext& operator=( const FMaterialInputContext& ) = delete;

		// フォントが構築され、パネルを描ける状態かを返す。
		bool IsReady() const { return bFontsReady; }

		// 全入力フレームで NewFrame と End の対応が保たれたかを返す。
		bool FramesBalanced() const { return bAllFramesBalanced; }

		// 実際の DefinitionPanel を描き、フレームと窓の後片付けも毎回確認する。
		bool Frame( FEffectEditorState& State, bool bFocusName = false )
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos( ImVec2( 0.0f, 0.0f ), ImGuiCond_Always );
			ImGui::SetNextWindowSize( ImVec2( 760.0f, 1320.0f ), ImGuiCond_Always );
			if ( ImGui::Begin( "EffectMaterialInputSelfTest", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize ) )
			{
				Window = ImGui::GetCurrentWindow();
				if ( bFocusName ) ImGui::SetKeyboardFocusHere();
				NEffectEditorUI::DrawDefinitionPanel( State );
				if ( !ImGui::IsAnyItemActive() ) State.Document.EndEditSession();
			}
			ImGui::End();
			ImGui::Render();
			bAllFramesBalanced = bAllFramesBalanced && !Context->WithinFrameScope && Context->CurrentWindowStack.Size == 0;
			return bAllFramesBalanced;
		}

		// 指定キーを一度押して離し、両方の入力段階でパネルを描く。
		void Tap( FEffectEditorState& State, ImGuiKey Key )
		{
			ImGui::GetIO().AddKeyEvent( Key, true );
			Frame( State );
			ImGui::GetIO().AddKeyEvent( Key, false );
			Frame( State );
		}

		// 数値欄を文字入力へ切り替え、実際の文字イベントで値を置き換える。
		bool ReplaceNumericInput( FEffectEditorState& State, ImGuiID Id, const char* Text )
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

		// 描画した窓の実 ID にキーボード選択が移ったかを調べる。
		bool IsFocused( ImGuiID Id ) const { return Context->NavId == Id; }

		// 固定座標を使わず Tab で実際の UI 項目へ移動する。
		bool TabTo( FEffectEditorState& State, ImGuiID Id )
		{
			// 無効項目を飛ばしながら、表示順の範囲内で対象へ移動する試行回数。
			for ( int32 Attempt = 0; Attempt < 256; ++Attempt )
			{
				if ( IsFocused( Id ) ) return true;
				Tap( State, ImGuiKey_Tab );
			}
			return IsFocused( Id );
		}

		// Curve 内の PushID(Label) を含めて、実際のボタン識別子を作る。
		ImGuiID CurveId( const char* CurveLabel, const char* ItemLabel ) const
		{
			const ImGuiID CurveScope = Window->GetID( CurveLabel ); // Curve のラベルが作る入力範囲。
			return ImHashStr( ItemLabel, 0, CurveScope );
		}

		// Curve の行番号とボタン名を含めて、同名 Remove の識別子を作る。
		ImGuiID CurveKeyId( const char* CurveLabel, int32 Index, const char* ItemLabel ) const
		{
			const ImGuiID CurveScope = Window->GetID( CurveLabel ); // Curve のラベルが作る入力範囲。
			const ImGuiID KeyScope = ImHashData( &Index, sizeof( Index ), CurveScope ); // 行番号で分けた入力範囲。
			return ImHashStr( ItemLabel, 0, KeyScope );
		}

		// DataPanel が描いた窓から、通常項目の識別子を返す。
		ImGuiID ItemId( const char* ItemLabel ) const { return Window->GetID( ItemLabel ); }

		// DragFloat2 が PushID(Label) と PushID(index) で作る実入力の識別子を返す。
		ImGuiID DragComponentId( const char* ItemLabel, int32 Component ) const
		{
			const ImGuiID LabelScope = Window->GetID( ItemLabel ); // DragFloat2 のラベル範囲。
			const ImGuiID ComponentScope = ImHashData( &Component, sizeof( Component ), LabelScope ); // X/Y の区別。
			return ImHashStr( "", 0, ComponentScope );
		}

	private:
		// テスト呼び出し元が使っていた ImGui 文脈。
		ImGuiContext* Previous = nullptr;
		// 入力確認だけが所有する独立文脈。
		ImGuiContext* Context = nullptr;
		// 直近に描画したパネルの窓。
		ImGuiWindow* Window = nullptr;
		// 独立文脈の標準フォントが作れたか。
		bool bFontsReady = false;
		// 呼び出した全フレームの開始・終了が対応していたか。
		bool bAllFramesBalanced = true;
	};

	// キーボードでドラッグ入力の値を増やし、入力の境界で別履歴になることを確かめる。
	void TestMaterialNumericHistory( NAssetTest::FChecker& C, FMaterialInputContext& Input )
	{
		C.Group = "effect-material-input-numeric";
		FParticleEffectDefinition Definition; // 材質操作を受ける最小の定義。
		FParticleEmitterDefinition Emitter; // UI から操作するエミッター。
		Emitter.Material.bEnabled = false;
		Emitter.Material.bDissolve = true;
		Emitter.Material.EdgeEmission = 1.0f;
		Definition.Emitters.Add( Emitter );
		FEffectEditorState State; // 独立した材質編集画面の状態。
		State.Document.Reset( Definition );
		const bool bFrameReady = Input.Frame( State, true ); // 入力欄へ初期フォーカスを置く。
		C.Check( bFrameReady && Input.Frame( State ) && Input.IsFocused( Input.ItemId( "Name" ) ), "別 ImGui 文脈で実パネルを描き Name へ移動できる" );
		const bool bFoundEdge = Input.TabTo( State, Input.ItemId( "Edge emission" ) ); // 材質無効時にも表示される境界欄。
		C.Check( bFoundEdge, "材質を有効化しなくても実際の Edge emission 欄へ Tab で移れる" );
		if ( !bFoundEdge ) return;
		const ImGuiID EdgeId = Input.ItemId( "Edge emission" ); // 実際の境界強度欄。
		const bool bEdgeActive = Input.ReplaceNumericInput( State, EdgeId, "2.5" ); // Ctrl+A と文字イベントで値を置換。
		const ImGuiID EdgeActiveId = ImGui::GetCurrentContext()->ActiveId; // 文字入力中に実際に有効な項目。
		const ImGuiID EdgeNavId = ImGui::GetCurrentContext()->NavId; // 文字入力中のキーボード選択先。
		Input.Tap( State, ImGuiKey_Tab ); // 数値編集を確定して一履歴へまとめる。
		const float32 EdgeAfterInput = State.Document.Get().Emitters[0].Material.EdgeEmission; // 連続入力後の境界強度。
		const int32 HistoryAfterEdge = State.Document.GetNumUndo(); // 確定後の履歴数。
		C.Check( bEdgeActive && EdgeAfterInput == 2.5f && HistoryAfterEdge == 1, "数値文字入力で境界発光値を一履歴に確定する (found=%d text=%d active=%u nav=%u edge=%.6f undo=%d)", bFoundEdge, bEdgeActive, EdgeActiveId, EdgeNavId, EdgeAfterInput, HistoryAfterEdge );
		const int32 HistoryBeforeScroll = State.Document.GetNumUndo(); // 境界入力一件の履歴数。
		const bool bFoundScroll = Input.TabTo( State, Input.DragComponentId( "UV scroll / second", 0 ) ); // 秒ごとの UV 移動入力。
		C.Check( bFoundScroll, "実際の UV scroll / second 欄へ Tab で移れる" );
		if ( !bFoundScroll ) return;
		const ImGuiID ScrollId = Input.DragComponentId( "UV scroll / second", 0 ); // DragFloat2 の横成分 ID。
		const bool bScrollActive = Input.ReplaceNumericInput( State, ScrollId, "0.25" ); // 横成分を文字イベントで置換。
		const ImGuiID ScrollActiveId = ImGui::GetCurrentContext()->ActiveId; // 文字入力中に実際に有効な成分。
		const ImGuiID ScrollNavId = ImGui::GetCurrentContext()->NavId; // 文字入力中のキーボード選択先。
		Input.Tap( State, ImGuiKey_Tab ); // 数値編集を確定。
		const float32 ScrollAfterInput = State.Document.Get().Emitters[0].Material.UVScroll.x; // キー入力後の横スクロール量。
		const bool bFoundNext = Input.TabTo( State, Input.ItemId( "Mask scale" ) ); // 次の実入力でセッションを閉じる。
		const int32 HistoryAfterScroll = State.Document.GetNumUndo(); // UV 確定後の履歴数。
		C.Check( bScrollActive && bFoundNext && ScrollAfterInput == 0.25f && HistoryAfterScroll == HistoryBeforeScroll + 1, "UV 入力は独立した一履歴となり、次の欄で確定する (found=%d next=%d text=%d active=%u nav=%u uv=%.6f undo=%d before=%d)", bFoundScroll, bFoundNext, bScrollActive, ScrollActiveId, ScrollNavId, ScrollAfterInput, HistoryAfterScroll, HistoryBeforeScroll );
		const bool bUndoScroll = State.Document.Undo();
		C.Check( bUndoScroll && State.Document.Get().Emitters[0].Material.UVScroll.x == 0.0f && State.Document.Get().Emitters[0].Material.EdgeEmission == EdgeAfterInput, "Undo は UV だけを戻し、境界強度を残す (ok=%d uv=%.6f edge=%.6f)", bUndoScroll, State.Document.Get().Emitters[0].Material.UVScroll.x, State.Document.Get().Emitters[0].Material.EdgeEmission );
		const bool bUndoEdge = State.Document.Undo();
		C.Check( bUndoEdge && State.Document.Get().Emitters[0].Material.EdgeEmission == 1.0f && State.Document.Get().Emitters[0].Material.UVScroll.x == 0.0f, "次の Undo で境界強度だけを元へ戻す (ok=%d edge=%.6f uv=%.6f)", bUndoEdge, State.Document.Get().Emitters[0].Material.EdgeEmission, State.Document.Get().Emitters[0].Material.UVScroll.x );
		const bool bRedoEdge = State.Document.Redo();
		const bool bRedoScroll = State.Document.Redo();
		C.Check( bRedoEdge && bRedoScroll && State.Document.Get().Emitters[0].Material.EdgeEmission == EdgeAfterInput && State.Document.Get().Emitters[0].Material.UVScroll.x == ScrollAfterInput, "Redo が別々の材質入力を元の精度で再適用する (edge=%d scroll=%d emission=%.6f uv=%.6f)", bRedoEdge, bRedoScroll, State.Document.Get().Emitters[0].Material.EdgeEmission, State.Document.Get().Emitters[0].Material.UVScroll.x );
	}

	// Curve の実ボタンで追加・削除・初期化を行い、各操作の履歴を確かめる。
	void TestMaterialCurveButtons( NAssetTest::FChecker& C, FMaterialInputContext& Input )
	{
		C.Group = "effect-material-input-curve-buttons";
		FParticleEffectDefinition Definition; // Curve 操作を受ける最小の定義。
		FParticleEmitterDefinition Emitter; // 描画された Curve の所有元。
		Emitter.Material.Emission.Interpolation = FEffectScalarCurve::EInterpolation::Smooth;
		Definition.Emitters.Add( Emitter );
		FEffectEditorState State; // Curve の実 UI を持つ独立状態。
		State.Document.Reset( Definition );
		Input.Frame( State, true );
		Input.Frame( State );
		const ImGuiID AddId = Input.CurveId( "Emission curve", "Add key" ); // 実際の Curve 配下の Add key。
		const bool bFoundAdd = Input.TabTo( State, AddId );
		C.Check( bFoundAdd, "実際の Emission curve の Add key へ Tab で移れる" );
		if ( !bFoundAdd ) return;
		Input.Tap( State, ImGuiKey_Enter );
		const int32 AddedCount = State.Document.Get().Emitters[0].Material.Emission.Keys.Num(); // 追加後のキー数。
		C.Check( AddedCount == 1 && State.Document.GetNumUndo() == 1, "Add key は一つのキーと一つの Undo を作る" );
		C.Check( State.Document.Undo() && State.Document.Get().Emitters[0].Material.Emission.Keys.IsEmpty() && State.Document.Redo() && State.Document.Get().Emitters[0].Material.Emission.Keys.Num() == 1, "Add key の Undo / Redo が同じ内容を復元する" );

		FParticleEffectDefinition WithKey; // Remove / Reset の初期値を個別に確定する定義。
		FParticleEmitterDefinition KeyEmitter; // 一つの有限なキーを持つエミッター。
		KeyEmitter.Material.Emission.Interpolation = FEffectScalarCurve::EInterpolation::Smooth;
		KeyEmitter.Material.Emission.Keys.Add( { 0.375f, 2.125f } );
		WithKey.Emitters.Add( KeyEmitter );
		State.Document.Reset( WithKey );
		Input.Frame( State, true );
		Input.Frame( State );
		const ImGuiID RemoveId = Input.CurveKeyId( "Emission curve", 0, "Remove" ); // 一行目の実際の Remove。
		const bool bFoundRemove = Input.TabTo( State, RemoveId );
		C.Check( bFoundRemove, "実際の Curve キーの Remove へ Tab で移れる" );
		if ( !bFoundRemove ) return;
		Input.Tap( State, ImGuiKey_Enter );
		C.Check( State.Document.Get().Emitters[0].Material.Emission.Keys.IsEmpty() && State.Document.GetNumUndo() == 1, "Remove は対象キーを一つの編集として削除する" );
		C.Check( State.Document.Undo() && State.Document.Get().Emitters[0].Material.Emission.Keys.Num() == 1 && State.Document.Get().Emitters[0].Material.Emission.Keys[0].Time == 0.375f && State.Document.Redo() && State.Document.Get().Emitters[0].Material.Emission.Keys.IsEmpty(), "Remove の Undo / Redo がキーの時刻と値を保つ" );

		State.Document.Reset( WithKey );
		Input.Frame( State, true );
		Input.Frame( State );
		const ImGuiID ResetId = Input.CurveId( "Emission curve", "Reset" ); // 曲線を既定へ戻す実ボタン。
		const bool bFoundReset = Input.TabTo( State, ResetId );
		C.Check( bFoundReset, "実際の Emission curve の Reset へ Tab で移れる" );
		if ( !bFoundReset ) return;
		Input.Tap( State, ImGuiKey_Enter );
		C.Check( State.Document.Get().Emitters[0].Material.Emission.Keys.IsEmpty() && State.Document.Get().Emitters[0].Material.Emission.Interpolation == FEffectScalarCurve::EInterpolation::Linear && State.Document.GetNumUndo() == 1, "Reset は曲線を既定へ戻す一履歴の編集になる" );
		C.Check( State.Document.Undo() && State.Document.Get().Emitters[0].Material.Emission.Keys.Num() == 1 && State.Document.Get().Emitters[0].Material.Emission.Keys[0].Value == 2.125f && State.Document.Redo() && State.Document.Get().Emitters[0].Material.Emission.Keys.IsEmpty(), "Reset の Undo / Redo が曲線精度を保つ" );
	}

	// 上限・不正な数・逆順を表示するだけでは、保存値が変わらないことを確認する。
	void TestInvalidMaterialCurve( NAssetTest::FChecker& C, FMaterialInputContext& Input )
	{
		C.Group = "effect-material-input-invalid-curve";
		FParticleEffectDefinition Definition; // 入力画面で表示する不正曲線の定義。
		FParticleEmitterDefinition Emitter; // 上限キーを持つエミッター。
		Emitter.Material.Emission.Interpolation = FEffectScalarCurve::EInterpolation::Linear;
		// UI の Add 上限と表示処理を検証するため、許可された数だけキーを並べる。
		for ( int32 Index = 0; Index < FEffectScalarCurve::MaxKeys; ++Index ) Emitter.Material.Emission.Keys.Add( { static_cast<float32>( Index ) / 31.0f, 1.0f } );
		Definition.Emitters.Add( Emitter );
		FEffectEditorState State; // 上限入力を確認する状態。
		State.Document.Reset( Definition );
		const uint64 BeforeHash = State.Document.Get().ComputeHash(); // UI を描く前の保存値。
		Input.Frame( State, true );
		Input.Frame( State );
		const bool bAddReachable = Input.TabTo( State, Input.CurveId( "Emission curve", "Add key" ) ); // 上限では無効である追加ボタン。
		C.Check( !bAddReachable && State.Document.Get().Emitters[0].Material.Emission.Keys.Num() == FEffectScalarCurve::MaxKeys && State.Document.Get().ComputeHash() == BeforeHash && State.Document.GetNumUndo() == 0, "32 キー上限で Add key は選べず、表示だけでは値と履歴を変えない" );

		FParticleEffectDefinition OutOfRangeDefinition; // 時刻範囲だけが壊れた曲線。
		FParticleEmitterDefinition OutOfRangeEmitter; // Add が拒否すべき既存キー。
		OutOfRangeEmitter.Material.Emission.Keys.Add( { -2.0f, 1.0f } );
		OutOfRangeDefinition.Emitters.Add( OutOfRangeEmitter );
		State.Document.Reset( OutOfRangeDefinition );
		const uint64 OutOfRangeHash = State.Document.Get().ComputeHash(); // Add 入力前の定義 hash。
		Input.Frame( State, true );
		Input.Frame( State );
		const bool bFoundOutOfRangeAdd = Input.TabTo( State, Input.CurveId( "Emission curve", "Add key" ) ); // 範囲外キーに対する実 Add ボタン。
		C.Check( bFoundOutOfRangeAdd, "時刻範囲外の曲線でも修復操作を表示する" );
		if ( bFoundOutOfRangeAdd ) Input.Tap( State, ImGuiKey_Enter );
		C.Check( State.Document.Get().Emitters[0].Material.Emission.Keys.Num() == 1 && State.Document.Get().ComputeHash() == OutOfRangeHash && State.Document.GetNumUndo() == 0, "時刻が範囲外の既存キーがあると Add は値と履歴を変えない" );

		FParticleEffectDefinition InvalidDefinition; // 非有限値と逆順を持つ定義。
		FParticleEmitterDefinition InvalidEmitter; // Emission 曲線の不正値。
		InvalidEmitter.Material.Emission.Keys.Add( { 0.8f, TNumericLimits<float32>::QuietNaN() } );
		InvalidEmitter.Material.Emission.Keys.Add( { 0.2f, 3.0f } );
		InvalidDefinition.Emitters.Add( InvalidEmitter );
		State.Document.Reset( InvalidDefinition );
		const uint64 InvalidHash = State.Document.Get().ComputeHash(); // 表示前の不正値も hash で保つ。
		Input.Frame( State, true );
		Input.Frame( State );
		C.Check( !State.Document.Get().Emitters[0].Material.Validate().IsEmpty() && State.Document.Get().ComputeHash() == InvalidHash && State.Document.GetNumUndo() == 0, "NaN・逆順の曲線を表示しても assert せず定義を変えない" );
		const bool bFoundReset = Input.TabTo( State, Input.CurveId( "Emission curve", "Reset" ) ); // 不正値を修復できる実ボタン。
		C.Check( bFoundReset, "不正な Curve でも Reset 操作を選べる" );
		if ( !bFoundReset ) return;
		Input.Tap( State, ImGuiKey_Enter );
		C.Check( State.Document.Get().Emitters[0].Material.Validate().IsEmpty() && State.Document.GetNumUndo() == 1, "Reset で不正な材質曲線を修復し一履歴を作る" );
		C.Check( State.Document.Undo() && !State.Document.Get().Emitters[0].Material.Validate().IsEmpty() && State.Document.Redo() && State.Document.Get().Emitters[0].Material.Validate().IsEmpty(), "不正曲線の修復を Undo / Redo できる" );

		FParticleEffectDefinition DescendingDefinition; // 時刻だけが逆順の有限曲線。
		FParticleEmitterDefinition DescendingEmitter; // clamp境界が逆転する3キー曲線。
		DescendingEmitter.Material.Emission.Keys.Add( { 0.8f, 1.0f } );
		DescendingEmitter.Material.Emission.Keys.Add( { 0.5f, 2.0f } );
		DescendingEmitter.Material.Emission.Keys.Add( { 0.2f, 3.0f } );
		DescendingDefinition.Emitters.Add( DescendingEmitter );
		State.Document.Reset( DescendingDefinition );
		const uint64 DescendingHash = State.Document.Get().ComputeHash(); // 無操作表示前の逆順曲線。
		Input.Frame( State, true );
		Input.Frame( State );
		C.Check( !State.Document.Get().Emitters[0].Material.Validate().IsEmpty() && State.Document.Get().ComputeHash() == DescendingHash && State.Document.GetNumUndo() == 0, "有限な逆順3キーを表示しても assert せず保存値を変えない" );
		const bool bFoundDescendingReset = Input.TabTo( State, Input.CurveId( "Emission curve", "Reset" ) ); // 逆順データを修復する実ボタン。
		C.Check( bFoundDescendingReset, "有限な逆順キーでも Curve Reset を選べる" );
		if ( bFoundDescendingReset ) Input.Tap( State, ImGuiKey_Enter );
		C.Check( State.Document.Get().Emitters[0].Material.Validate().IsEmpty() && State.Document.GetNumUndo() == 1, "逆順3キーを Reset で修復し、一履歴を作る" );
	}

	// JSON の型違いで材質全体が無効な場合も実 UI から初期化できることを確かめる。
	void TestInvalidMaterialReset( NAssetTest::FChecker& C, FMaterialInputContext& Input )
	{
		C.Group = "effect-material-input-invalid-material";
		const NlohmannJson InvalidJson = NlohmannJson::parse( R"({"Enabled":"bad"})" ); // 保存した Enabled の型を壊す入力。
		FEffectMaterialDefinition InvalidMaterial; // 型違いを無効状態へ残す読み込み先。
		C.Check( FEffectMaterialDefinition::FromJson( InvalidJson, InvalidMaterial ) && !InvalidMaterial.Dissolve.Validate().IsEmpty() && !InvalidMaterial.bDissolve, "無効な Enabled は材質の不正状態として残り、Dissolve 表示は無効のまま" );
		FParticleEffectDefinition Definition; // 不正材質を持つ UI 文書。
		FParticleEmitterDefinition Emitter; // Reset material 操作を行う対象。
		Emitter.Material = InvalidMaterial;
		Definition.Emitters.Add( Emitter );
		FEffectEditorState State; // 実際の材質編集欄を描く状態。
		State.Document.Reset( Definition );
		const uint64 InvalidHash = State.Document.Get().ComputeHash(); // 修復前に保持する不正値。
		Input.Frame( State, true );
		Input.Frame( State );
		C.Check( !State.Document.Get().Emitters[0].Material.Validate().IsEmpty() && State.Document.Get().ComputeHash() == InvalidHash && State.Document.GetNumUndo() == 0, "Enabled の型違いを UI に表示しても値や履歴を変えない" );
		const bool bFoundReset = Input.TabTo( State, Input.ItemId( "Reset material" ) ); // 不正値を全体初期化する実ボタン。
		C.Check( bFoundReset, "Dissolve が無効でも Reset material へ移動できる" );
		if ( !bFoundReset ) return;
		Input.Tap( State, ImGuiKey_Enter );
		C.Check( State.Document.Get().Emitters[0].Material.Validate().IsEmpty() && State.Document.GetNumUndo() == 1, "Reset material は sentinel を解消して一履歴を作る" );
		C.Check( State.Document.Undo() && !State.Document.Get().Emitters[0].Material.Validate().IsEmpty() && State.Document.Redo() && State.Document.Get().Emitters[0].Material.Validate().IsEmpty(), "無効材質全体の修復を Undo / Redo できる" );
	}
}

namespace NWorldTest
{
	// 実際の材質編集 UI の入力、履歴分離、上限と不正曲線の修復を別文脈で確認する。
	void RunEffectMaterialInputTests( NAssetTest::FChecker& C )
	{
		C.Group = "effect-material-input-context";
		ImGuiContext* Previous = ImGui::GetCurrentContext(); // 呼び出し元の文脈を最後に比較する。
		{
			FMaterialInputContext Input; // テスト専用の入力・描画文脈。
			C.Check( Input.IsReady(), "材質入力用 ImGui 文脈のフォントを初期化できる" );
			if ( Input.IsReady() )
			{
				// 材質値・履歴・曲線ボタン・不正値修復を実際のパネルから順に確かめる。
				TestMaterialNumericHistory( C, Input );
				TestMaterialCurveButtons( C, Input );
				TestInvalidMaterialCurve( C, Input );
				TestInvalidMaterialReset( C, Input );
				C.Check( Input.FramesBalanced(), "実入力を送った全 ImGui frame の Begin / End が対応する" );
			}
		}
		C.Check( ImGui::GetCurrentContext() == Previous, "材質入力確認の終了後に元の ImGui 文脈へ戻る" );
	}
}
