// SPDX-License-Identifier: NOASSERTION
#include "Library/DebugUI/EffectEditorInternal.h"
#include "VendorModules/ImGui/imgui_internal.h"
#include "AssetSelfTestSupport.h"

namespace
{
	// ゲームの ImGui 状態を保存し、別の文脈だけで入力と描画データの生成を行う。
	class FEffectInputContext
	{
	public:
		// 独立したフォントを用意し、設定やログをディスクへ書かない入力用画面を作る。
		FEffectInputContext() : Previous( ImGui::GetCurrentContext() ), Context( ImGui::CreateContext() )
		{
			ImGui::SetCurrentContext( Context );
			// ウィンドウや描画バックエンドに依存しない入力の設定。
			ImGuiIO& Input = ImGui::GetIO();
			Input.DisplaySize = ImVec2( 1600.0f, 1100.0f );
			Input.DeltaTime = 1.0f / 60.0f;
			Input.IniFilename = nullptr;
			Input.LogFilename = nullptr;
			Input.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			Input.ConfigInputTrickleEventQueue = false;
			Input.Fonts->AddFontDefaultBitmap();
			bFontsReady = Input.Fonts->Build();
		}

		// この文脈だけを破棄し、呼び出し元が使っていたゲームの文脈へ戻す。
		~FEffectInputContext()
		{
			ImGui::DestroyContext( Context );
			ImGui::SetCurrentContext( Previous );
		}
		// 文脈の所有権を複製しない。
		FEffectInputContext( const FEffectInputContext& ) = delete;
		// 文脈の所有権を代入しない。
		FEffectInputContext& operator=( const FEffectInputContext& ) = delete;

		// フォントの準備が成功し、フレームを開始できるかを返す。
		bool IsReady() const { return bFontsReady; }

		// 実際の編集パネルを一フレーム描く。最初だけ Name にキーボードの位置を合わせられる。
		void Frame( FEffectEditorState& State, bool bFocusName = false )
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos( ImVec2( 0.0f, 0.0f ), ImGuiCond_Always );
			ImGui::SetNextWindowSize( ImVec2( 580.0f, 1050.0f ), ImGuiCond_Always );
			if ( ImGui::Begin( "EffectInputSelfTest", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize ) )
			{
				Window = ImGui::GetCurrentWindow();
				if ( bFocusName ) ImGui::SetKeyboardFocusHere();
				NEffectEditorUI::DrawDefinitionPanel( State );
				if ( !ImGui::IsAnyItemActive() ) State.Document.EndEditSession();
			}
			ImGui::End();
			ImGui::Render();
		}

		// 一つのキーを押して離す。OS のキーやウィンドウには触らない。
		void Tap( FEffectEditorState& State, ImGuiKey Key )
		{
			ImGui::GetIO().AddKeyEvent( Key, true );
			Frame( State );
			ImGui::GetIO().AddKeyEvent( Key, false );
			Frame( State );
		}

		// 現在の文字入力を全選択して、文字イベントで置き換える。
		void ReplaceInput( FEffectEditorState& State, const char* Text )
		{
			ImGui::GetIO().AddKeyEvent( ImGuiMod_Ctrl, true );
			ImGui::GetIO().AddKeyEvent( ImGuiKey_A, true );
			Frame( State );
			ImGui::GetIO().AddKeyEvent( ImGuiKey_A, false );
			ImGui::GetIO().AddKeyEvent( ImGuiMod_Ctrl, false );
			ImGui::GetIO().AddInputCharactersUTF8( Text );
			Frame( State );
		}

		// パネルと同じ窓から求めた識別子が、現在のキーボードの移動先になっているか。
		bool IsFocused( const char* Label ) const
		{
			return Window != nullptr && Context->NavId == Window->GetID( Label );
		}

		// 固定座標を使わず、実際の Tab 入力で指定ラベルのボタンへ移る。
		bool TabTo( FEffectEditorState& State, const char* Label )
		{
			// 上限を設け、表示順が壊れた場合もテストを止めず失敗として返す。
			for ( int32 Attempt = 0; Attempt < 32; ++Attempt )
			{
				if ( IsFocused( Label ) ) return true;
				Tap( State, ImGuiKey_Tab );
			}
			return IsFocused( Label );
		}

	private:
		// テストを呼んだゲーム側の文脈。
		ImGuiContext* Previous = nullptr;
		// このテストだけが所有する文脈。
		ImGuiContext* Context = nullptr;
		// 描画で取得した、パネルの実際の窓。
		ImGuiWindow* Window = nullptr;
		// バックエンドを使わず、文字を描く準備ができたか。
		bool bFontsReady = false;
	};

	// 文字入力から Tab で数値入力へ移っても、項目ごとに取り消せることを確かめる。
	void TestInputUndoBoundaries( NAssetTest::FChecker& C, FEffectInputContext& Input, FEffectEditorState& State )
	{
		C.Group = "effect-editor-input-undo";
		Input.Frame( State, true );
		Input.Frame( State );
		C.Check( Input.IsFocused( "Name" ), "描画した Name の入力欄へキーボードで移れる" );
		Input.ReplaceInput( State, "Changed" );
		ImGui::GetIO().AddInputCharactersUTF8( " again" );
		Input.Frame( State );
		C.Check( State.Document.Get().Name == "Changed again" && State.Document.Get().Duration == 0.0f && State.Document.GetNumUndo() == 1, "連続する文字入力は Name だけを変え、履歴を一回にまとめる" );
		Input.Tap( State, ImGuiKey_Tab );
		C.Check( Input.IsFocused( "Duration (s)" ), "Tab 入力で Name から Duration の数値入力へ移る" );
		Input.ReplaceInput( State, "2.5" );
		// 数値の文字入力は、Tab で編集を終えたときに元の値へ反映される。
		Input.Tap( State, ImGuiKey_Tab );
		C.Check( State.Document.Get().Name == "Changed again" && State.Document.Get().Duration == 2.5f && State.Document.GetNumUndo() == 2, "Duration の編集確定は Name とは別の履歴になる（Name=%s, Duration=%.5f, Undo=%d）", *State.Document.Get().Name, State.Document.Get().Duration, State.Document.GetNumUndo() );
		C.Check( State.Document.Undo() && State.Document.Get().Name == "Changed again" && State.Document.Get().Duration == 0.0f, "一回の取り消しで Duration だけが戻り、編集した Name は残る" );
		C.Check( State.Document.Undo() && State.Document.Get().Name == "Original" && State.Document.Get().Duration == 0.0f && !State.Document.IsDirty(), "二回目の取り消しで Name も保存時の値に戻る" );
		C.Check( State.Document.Redo() && State.Document.Get().Name == "Changed again" && State.Document.Get().Duration == 0.0f && State.Document.Redo() && State.Document.Get().Duration == 2.5f, "やり直しも Name と Duration を別々に復元する" );
		Input.Frame( State );
	}

	// 実際のボタンをキーボードで押し、追加・削除が文書と履歴へ反映されることを確かめる。
	void TestEmitterButtons( NAssetTest::FChecker& C, FEffectInputContext& Input, FEffectEditorState& State )
	{
		C.Group = "effect-editor-input-buttons";
		// ボタンを押す前の個数。
		const int32 BeforeCount = State.Document.Get().Emitters.Num();
		// ボタンを押す前の履歴数。
		const int32 BeforeHistory = State.Document.GetNumUndo();
		// Tab の移動先は、描画された窓の識別子で確認する。
		const bool bFoundAdd = Input.TabTo( State, "Add emitter" );
		C.Check( bFoundAdd, "実際の Add emitter ボタンへ Tab で移れる" );
		if ( !bFoundAdd ) return;
		Input.Tap( State, ImGuiKey_Enter );
		C.Check( State.Document.Get().Emitters.Num() == BeforeCount + 1 && State.SelectedEmitter == BeforeCount && State.Document.GetNumUndo() == BeforeHistory + 1, "Add emitter を押すと一つ追加し、新しい項目を選び、一回の履歴を作る" );
		// 追加した項目を消す、同じパネル内の実際のボタン。
		const bool bFoundDelete = Input.TabTo( State, "Delete emitter" );
		C.Check( bFoundDelete, "実際の Delete emitter ボタンへ Tab で移れる" );
		if ( !bFoundDelete ) return;
		Input.Tap( State, ImGuiKey_Enter );
		C.Check( State.Document.Get().Emitters.Num() == BeforeCount && State.Document.GetNumUndo() == BeforeHistory + 2 && State.Document.Get().Emitters.IsValidIndex( State.SelectedEmitter ), "Delete emitter を押すと選択した項目を削除し、残った選択と履歴を保つ" );
		C.Check( State.Document.Undo() && State.Document.Get().Emitters.Num() == BeforeCount + 1 && State.Document.Redo() && State.Document.Get().Emitters.Num() == BeforeCount, "ボタンで削除した項目も、取り消しとやり直しで復元できる" );
	}
}

namespace NWorldTest
{
	// ゲームの ImGui 文脈や OS の入力に触れず、編集パネルの入力から履歴までを確認する。
	void RunEffectEditorInputTests( NAssetTest::FChecker& C )
	{
		C.Group = "effect-editor-input-context";
		// 終了後に同じ文脈へ戻ることを確認する、呼び出し元の状態。
		ImGuiContext* Previous = ImGui::GetCurrentContext();
		{
			// 初期化から破棄までを、この範囲だけで所有する入力用の文脈。
			FEffectInputContext Input;
			C.Check( Input.IsReady(), "独立した ImGui 文脈のフォントを描画バックエンドなしで初期化できる" );
			if ( Input.IsReady() )
			{
				// 保存済み状態から、実際のパネルで二つの入力欄と構造を編集する文書。
				FParticleEffectDefinition Original;
				Original.Name = "Original";
				Original.Emitters.Add( FParticleEmitterDefinition() );
				// プレビュー描画やアセットの読み込みを行わない、入力確認用の状態。
				FEffectEditorState State;
				State.Document.Reset( Original );
				TestInputUndoBoundaries( C, Input, State );
				TestEmitterButtons( C, Input, State );
			}
		}
		C.Group = "effect-editor-input-context";
		C.Check( ImGui::GetCurrentContext() == Previous, "入力確認の終了後は、ゲームが使っていた ImGui 文脈へ戻る" );
	}
}
