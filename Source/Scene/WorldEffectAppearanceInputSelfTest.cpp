// SPDX-License-Identifier: NOASSERTION
#include "WorldSelfTestSupport.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/EffectEditorInternal.h"
#include "VendorModules/ImGui/imgui_internal.h"

#include <cmath>
#include <limits>

namespace
{
	// 実画面と別のImGui文脈でパネルへ入力し、iniや描画backendに依存させない。
	class FAppearanceInputContext
	{
	public:
		// 独立した入力contextとフォントを作る。失敗時はIsReadyで呼び出し側へ返す。
		FAppearanceInputContext() : Previous( ImGui::GetCurrentContext() ), Context( ImGui::CreateContext() )
		{
			ImGui::SetCurrentContext( Context );
			ImGuiIO& Io = ImGui::GetIO();
			Io.DisplaySize = ImVec2( 1800.0f, 1800.0f );
			Io.DeltaTime = 1.0f / 60.0f;
			Io.IniFilename = nullptr;
			Io.LogFilename = nullptr;
			Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			Io.ConfigInputTrickleEventQueue = false;
			Io.Fonts->AddFontDefaultBitmap();
			bFontsReady = Io.Fonts->Build();
		}

		// 独立contextを破棄し、呼び出し元のcontextへ戻す。
		~FAppearanceInputContext()
		{
			ImGui::DestroyContext( Context );
			ImGui::SetCurrentContext( Previous );
		}
		// ImGui contextは複製できない資源のため、コピーを禁止する。
		FAppearanceInputContext( const FAppearanceInputContext& ) = delete;
		// ImGui contextの代入を禁止する。
		FAppearanceInputContext& operator=( const FAppearanceInputContext& ) = delete;

		// font atlasが作れ、UIを描ける状態かを返す。
		bool IsReady() const { return bFontsReady; }
		// すべてのBegin/EndとNewFrame/Renderが釣り合ったかを返す。
		bool FramesBalanced() const { return bFramesBalanced; }

		// 実際のDefinitionPanelを描いて、tab移動と描画frameを確認する。
		bool Frame( FEffectEditorState& State, bool bFocusName = false )
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos( ImVec2( 0.0f, 0.0f ), ImGuiCond_Always );
			ImGui::SetNextWindowSize( ImVec2( 1100.0f, 1700.0f ), ImGuiCond_Always );
			if ( ImGui::Begin( "EffectAppearanceInputSelfTest", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize ) )
			{
				Window = ImGui::GetCurrentWindow();
				if ( bFocusName ) ImGui::SetKeyboardFocusHere();
				NEffectEditorUI::DrawDefinitionPanel( State );
				if ( !ImGui::IsAnyItemActive() ) State.Document.EndEditSession();
			}
			ImGui::End();
			ImGui::Render();
			bFramesBalanced = bFramesBalanced && !Context->WithinFrameScope && Context->CurrentWindowStack.Size == 0;
			return bFramesBalanced;
		}

		// 指定キーを押して離し、両方の入力frameでpanelを更新する。
		void Tap( FEffectEditorState& State, ImGuiKey Key )
		{
			ImGui::GetIO().AddKeyEvent( Key, true );
			Frame( State );
			ImGui::GetIO().AddKeyEvent( Key, false );
			Frame( State );
		}

		// 指定入力をtab巡回で探し、固定画面座標を使わずfocusする。
		bool TabTo( FEffectEditorState& State, ImGuiID Id )
		{
			for ( int32 Attempt = 0; Attempt < 512; ++Attempt )
			{
				if ( Context->NavId == Id ) return true;
				Tap( State, ImGuiKey_Tab );
			}
			return Context->NavId == Id;
		}

		// 通常項目のIDを最後に描いたpanelから得る。
		ImGuiID ItemId( const char* Label ) const { return Window->GetID( Label ); }
		// curve全体に属するAdd/Reset項目のIDを得る。
		ImGuiID CurveButtonId( const char* CurveLabel, const char* ItemLabel ) const
		{
			const ImGuiID CurveScope = Window->GetID( CurveLabel );
			return ImHashStr( ItemLabel, 0, CurveScope );
		}

		// curve labelとkey indexに入れ子になった項目IDを得る。
		ImGuiID CurveKeyId( const char* CurveLabel, int32 Index, const char* ItemLabel ) const
		{
			const ImGuiID CurveScope = Window->GetID( CurveLabel );
			const ImGuiID KeyScope = ImHashData( &Index, sizeof( Index ), CurveScope );
			return ImHashStr( ItemLabel, 0, KeyScope );
		}

		// ColorEdit4のRGBA component IDを計算する。Componentは0=R..3=A。
		ImGuiID CurveColorComponentId( const char* CurveLabel, int32 Index, int32 Component ) const
		{
			static const char* ComponentLabels[] = { "##X", "##Y", "##Z", "##W" };
			const ImGuiID CurveScope = Window->GetID( CurveLabel );
			const ImGuiID KeyScope = ImHashData( &Index, sizeof( Index ), CurveScope );
			const ImGuiID ColorScope = ImHashStr( "Multiplier", 0, KeyScope );
			return ImHashStr( ComponentLabels[Component], 0, ColorScope );
		}

		// DragFloatを文字入力へ切り替え、Ctrl+Aと文字eventで値を書き換える。
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

	private:
		// このtest開始前に選択されていたImGui context。
		ImGuiContext* Previous = nullptr;
		// input testだけが所有する独立ImGui context。
		ImGuiContext* Context = nullptr;
		// 最終frameでcurve IDを計算するwindow。
		ImGuiWindow* Window = nullptr;
		// font atlasを正常に構築できたか。
		bool bFontsReady = false;
		// 描画した全frameのImGui stackが空へ戻ったか。
		bool bFramesBalanced = true;
	};

	// Size/RGBA曲線を同じ実panelから編集し、入力欄の境界でUndo履歴を分ける。
	void TestAppearanceTextInput( NAssetTest::FChecker& C, FAppearanceInputContext& Input )
	{
		C.Group = "effect-appearance-input-values";
		FParticleEffectDefinition Definition;
		FParticleEmitterDefinition Emitter;
		Emitter.StartSize = { 1.0f, 3.0f };
		Emitter.EndSize = { 2.0f, 4.0f };
		Emitter.StartColor = FColor32( 255, 255, 255, 255 );
		Emitter.EndColor = FColor32( 255, 255, 255, 255 );
		Emitter.Appearance.Size.Keys.Add( { 0.0f, 1.0f } );
		Emitter.Appearance.Color.Keys.Add( { 0.0f, FColor::White } );
		Definition.Emitters.Add( Emitter );
		FEffectEditorState State;
		State.Document.Reset( Definition );
		const bool bFirstFrame = Input.Frame( State, true );
		C.Check( bFirstFrame && Input.Frame( State ) && ImGui::GetCurrentContext()->NavId == Input.ItemId( "Name" ), "独立contextで実パネルを描き、Nameへ初期focusする" );
		const ImGuiID SizeValue = Input.CurveKeyId( "Size multiplier", 0, "Value" );
		C.Check( Input.TabTo( State, SizeValue ), "Size倍率の実入力へTabで移る" );
		const bool bSizeTyped = Input.ReplaceNumeric( State, SizeValue, "2.0" );
		Input.Tap( State, ImGuiKey_Tab );
		C.Check( bSizeTyped && NMath::Abs( State.Document.Get().Emitters[0].Appearance.Size.Keys[0].Value - 2.0f ) < 1.0e-5f, "Size倍率へ文字入力しfloat値を保存する" );
		const ImGuiID ColorRed = Input.CurveColorComponentId( "Color multiplier", 0, 0 );
		C.Check( Input.TabTo( State, ColorRed ), "ColorEdit4の赤成分へTabで移る" );
		const bool bRedTyped = Input.ReplaceNumeric( State, ColorRed, "0.5" );
		Input.Tap( State, ImGuiKey_Tab );
		C.Check( bRedTyped && NMath::Abs( State.Document.Get().Emitters[0].Appearance.Color.Keys[0].Value.r - 0.5f ) < 0.002f, "ColorEdit4の赤floatを文字入力で変更する" );
		const ImGuiID ColorAlpha = Input.CurveColorComponentId( "Color multiplier", 0, 3 );
		C.Check( Input.TabTo( State, ColorAlpha ), "ColorEdit4のalpha成分へTabで移る" );
		const bool bAlphaTyped = Input.ReplaceNumeric( State, ColorAlpha, "0.25" );
		Input.Tap( State, ImGuiKey_Tab );
		C.Check( bAlphaTyped && NMath::Abs( State.Document.Get().Emitters[0].Appearance.Color.Keys[0].Value.a - 0.25f ) < 0.002f, "ColorEdit4のalpha floatを文字入力で変更する" );
		C.Check( State.Document.GetNumUndo() >= 3, "Size/RGBAの入力欄を離れるたびに別々の履歴になる" );
		const bool bUndoAlpha = State.Document.Undo();
		C.Check( bUndoAlpha && NMath::Abs( State.Document.Get().Emitters[0].Appearance.Color.Keys[0].Value.a - 1.0f ) < 0.002f && NMath::Abs( State.Document.Get().Emitters[0].Appearance.Color.Keys[0].Value.r - 0.5f ) < 0.002f, "最後のalpha入力だけをUndoする" );
		C.Check( State.Document.Redo() && NMath::Abs( State.Document.Get().Emitters[0].Appearance.Color.Keys[0].Value.a - 0.25f ) < 0.002f, "alpha入力をRedoする" );
	}

	// Add/Remove/Resetが倍率曲線だけを編集し、base colorや材質を残す。
	void TestAppearanceCurveButtons( NAssetTest::FChecker& C, FAppearanceInputContext& Input )
	{
		C.Group = "effect-appearance-input-buttons";
		FParticleEffectDefinition Definition;
		FParticleEmitterDefinition Emitter;
		Emitter.StartColor = FColor32( 200, 150, 100, 220 );
		Emitter.EndColor = FColor32( 180, 160, 120, 200 );
		Emitter.Material.UVScroll = FVector2( 0.25f, -0.5f );
		Emitter.Appearance.Color.Keys.Add( { 0.0f, FColor( 0.1f, 0.3f, 0.5f, 0.8f ) } );
		Emitter.Appearance.Color.Keys.Add( { 1.0f, FColor( 0.9f, 0.7f, 0.5f, 0.2f ) } );
		Definition.Emitters.Add( Emitter );
		FEffectEditorState State;
		State.Document.Reset( Definition );
		Input.Frame( State, true );
		Input.Frame( State );
		const FColor MiddleBefore = State.Document.Get().Emitters[0].Appearance.Color.Evaluate( 0.5f, FColor::White );
		const ImGuiID AddColor = Input.CurveButtonId( "Color multiplier", "Add key" );
		C.Check( Input.TabTo( State, AddColor ), "Color curveのAdd keyへTabで移る" );
		Input.Tap( State, ImGuiKey_Enter );
		const FEffectColorCurve& AddedCurve = State.Document.Get().Emitters[0].Appearance.Color;
		C.Check( AddedCurve.Keys.Num() == 3 && AddedCurve.Keys[1].Time == 0.5f && AddedCurve.Keys[1].Value == MiddleBefore, "Add keyは最大gap中央に既存曲線の評価色を置き、表示を飛ばさない" );
		const ImGuiID RemoveAdded = Input.CurveKeyId( "Color multiplier", 1, "Remove" );
		C.Check( Input.TabTo( State, RemoveAdded ), "追加したColor keyのRemoveへTabで移る" );
		Input.Tap( State, ImGuiKey_Enter );
		C.Check( State.Document.Get().Emitters[0].Appearance.Color.Keys.Num() == 2, "Removeは選んだColor keyだけ削除する" );
		const ImGuiID ResetAppearance = Input.ItemId( "Reset appearance" );
		C.Check( Input.TabTo( State, ResetAppearance ), "Reset appearanceへTabで移る" );
		Input.Tap( State, ImGuiKey_Enter );
		const FParticleEmitterDefinition& Reset = State.Document.Get().Emitters[0];
		C.Check( Reset.Appearance.Size.Keys.IsEmpty() && Reset.Appearance.Color.Keys.IsEmpty() && Reset.StartColor == Emitter.StartColor && Reset.EndColor == Emitter.EndColor && Reset.Material.UVScroll == Emitter.Material.UVScroll, "Reset appearanceは倍率だけを初期化してbase色と材質を保つ" );
	}

	// 壊れた時刻順やNaNを入力へ渡さずResetで修復する。
	void TestAppearanceInvalidRepair( NAssetTest::FChecker& C, FAppearanceInputContext& Input )
	{
		C.Group = "effect-appearance-input-invalid";
		FParticleEffectDefinition Definition;
		FParticleEmitterDefinition Emitter;
		Emitter.Appearance.Size.Keys.Add( { 0.8f, 1.0f } );
		Emitter.Appearance.Size.Keys.Add( { 0.2f, 2.0f } );
		Emitter.Appearance.Color.Keys.Add( { 0.8f, FColor( TNumericLimits<float32>::QuietNaN(), 0.2f, 0.4f, 1.0f ) } );
		Emitter.Appearance.Color.Keys.Add( { 0.2f, FColor::White } );
		Definition.Emitters.Add( Emitter );
		FEffectEditorState State;
		State.Document.Reset( Definition );
		const uint64 Before = State.Document.Get().ComputeHash();
		Input.Frame( State, true );
		Input.Frame( State );
		C.Check( !State.Document.Get().Emitters[0].Appearance.Validate().IsEmpty() && State.Document.Get().ComputeHash() == Before && State.Document.GetNumUndo() == 0, "不正時刻順とNaN色を描いても入力へ渡さず元値を保つ" );
		const ImGuiID ColorReset = Input.CurveButtonId( "Color multiplier", "Reset" );
		C.Check( Input.TabTo( State, ColorReset ), "不正Color keyをResetで修復するbuttonへ移れる" );
		Input.Tap( State, ImGuiKey_Enter );
		C.Check( State.Document.Get().Emitters[0].Appearance.Color.Validate().IsEmpty() && State.Document.GetNumUndo() == 1, "Color曲線ResetでNaNと逆順を一履歴で修復する" );
	}
}

namespace NWorldTest
{
	// 実Appearance panelへ文字入力し、曲線操作・履歴境界・不正値修復を確認する。
	void RunEffectAppearanceInputTests( NAssetTest::FChecker& C )
	{
		C.Group = "effect-appearance-input-context";
		ImGuiContext* Previous = ImGui::GetCurrentContext();
		{
			FAppearanceInputContext Input;
			C.Check( Input.IsReady(), "Appearance入力用ImGui contextのfontを構築する" );
			if ( Input.IsReady() )
			{
				TestAppearanceTextInput( C, Input );
				TestAppearanceCurveButtons( C, Input );
				TestAppearanceInvalidRepair( C, Input );
				C.Check( Input.FramesBalanced(), "すべてのAppearance入力frameでImGuiのbegin/endを対応させる" );
			}
		}
		C.Check( ImGui::GetCurrentContext() == Previous, "Appearance入力後に元のImGui contextへ戻る" );
	}
}
