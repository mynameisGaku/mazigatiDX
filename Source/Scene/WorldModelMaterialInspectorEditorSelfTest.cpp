// SPDX-License-Identifier: NOASSERTION
#include "WorldModelMaterialInspectorSelfTest.h"

#include <cmath>

#include "AssetSelfTestModelSurface.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/WorldEditorInternal.h"
#include "Library/DebugUI/WorldEditorModelMaterialPanel.h"
#include "Library/DebugUI/WorldEditorModelPreview.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/EditorAssetInfo.h"
#include "Library/Framework/Asset/StaticModelSurface.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/World/WorldEdit.h"
#include "Library/Platform/FileSystem.h"
#include "VendorModules/ImGui/imgui_internal.h"

namespace
{
	// 自己確認中だけasset rootをfixtureへ切り替え、最後に元へ戻す。
	struct FModelInspectorRootScope
	{
		// fixtureへ切り替える前のroot。
		FString Previous;
		// 操作対象のasset manager。
		CAssetManager& Assets;
		// rootを退避してfixture rootへ切り替える。
		FModelInspectorRootScope( CAssetManager& InAssets, const FString& Root ) : Previous( InAssets.GetRootDirectory() ), Assets( InAssets ) { Assets.SetRootDirectory( Root ); }
		// rootを自己確認開始前へ戻す。
		~FModelInspectorRootScope() { Assets.SetRootDirectory( Previous ); }
	};

	// 自己確認が作ったmodel/world fileだけをcache解放後に消す。
	struct FModelInspectorFilesScope
	{
		// file解放後にcacheを掃除するasset manager。
		CAssetManager& Assets;
		// このscopeが作成したabsolute file paths。
		TArray<FString> Paths;
		// model参照を解放してからfixture fileを削除する。
		~FModelInspectorFilesScope()
		{
			Assets.ReleaseUnused();
			Assets.Tick( 0.0f );
			for ( const FString& Path : Paths ) NFileSystem::RemoveFile( Path );
		}
	};

	// 実Inspector widgetへkeyboard操作を送り、production panelを毎frame描く。
	class FModelInspectorKeyboard
	{
	public:
		// 既存ImGui文脈を保ち、独立したkeyboard navigation文脈を作る。
		FModelInspectorKeyboard() : Previous( ImGui::GetCurrentContext() ), Context( ImGui::CreateContext() )
		{
			ImGui::SetCurrentContext( Context );
			Io = &ImGui::GetIO();
			// keyboard文脈へ初期画面設定を渡す。
			Io->DisplaySize = ImVec2( 1200.0f, 900.0f );
			Io->DeltaTime = 1.0f / 60.0f;
			Io->IniFilename = nullptr;
			Io->LogFilename = nullptr;
			Io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			Io->ConfigInputTrickleEventQueue = false;
			Io->Fonts->AddFontDefaultBitmap();
			bFontsReady = Io->Fonts->Build();
		}
		// 独立文脈を捨て、呼出元のImGui文脈へ戻す。
		~FModelInspectorKeyboard()
		{
			ImGui::DestroyContext( Context );
			ImGui::SetCurrentContext( Previous );
		}
		// 入力文脈の複製を禁止する。
		FModelInspectorKeyboard( const FModelInspectorKeyboard& ) = delete;
		// 入力文脈の代入を禁止する。
		FModelInspectorKeyboard& operator=( const FModelInspectorKeyboard& ) = delete;

		// preview更新を任意で行い、対象Modelの実Inspectorを描いてdocument境界を反映する。
		bool Frame( FWorldEditorState& State, IDrawExecutor& Executor, bool bUpdate = true )
		{
			if ( bUpdate ) NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.0f, &Executor );
			ImGui::NewFrame();
			ImGui::SetNextWindowPos( ImVec2( 0.0f, 0.0f ), ImGuiCond_Always );
			ImGui::SetNextWindowSize( ImVec2( 1000.0f, 850.0f ), ImGuiCond_Always );
			if ( ImGui::Begin( "WorldModelMaterialInspectorEditorSelfTest", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse ) )
			{
				Window = ImGui::GetCurrentWindow();
				NWorldEditorUI::DrawModelMaterialEffectPanel( State, 1 );
				if ( !ImGui::IsAnyItemActive() ) State.Session.Document.EndEditSession();
			}
			ImGui::End();
			ImGui::Render();
			bFramesBalanced = bFramesBalanced && !Context->WithinFrameScope && Context->CurrentWindowStack.Size == 0;
			return bFramesBalanced;
		}
		// button press/releaseを別frameで実panelへ送る。
		void Tap( FWorldEditorState& State, IDrawExecutor& Executor, ImGuiKey Key, bool bUpdate = true )
		{
			ImGui::GetIO().AddKeyEvent( Key, true );
			Frame( State, Executor, bUpdate );
			ImGui::GetIO().AddKeyEvent( Key, false );
			Frame( State, Executor, bUpdate );
		}
		// Tab巡回でproduction widgetへfocusする。
		bool TabTo( FWorldEditorState& State, IDrawExecutor& Executor, const char* Label, bool bUpdate = true )
		{
			// Tab巡回の到達先widget ID。
			const ImGuiID Target = ItemId( Label );
			LastTabTarget = Target;
			LastTabReached = Context->NavId == Target;
			LastTabAttempts = 0;
			// 到達先までのTab巡回回数。
			for ( int32 Attempt = 0; Attempt < 256; ++Attempt )
			{
				if ( Context->NavId == Target ) { LastTabReached = true; return true; }
				LastTabAttempts = Attempt + 1;
				Tap( State, Executor, ImGuiKey_Tab, bUpdate );
			}
			LastTabReached = Context->NavId == Target;
			return LastTabReached;
		}
		// keyboard activationで実widgetのbuttonを押す。
		bool Activate( FWorldEditorState& State, IDrawExecutor& Executor, const char* Label, bool bUpdate = true )
		{
			if ( !TabTo( State, Executor, Label, bUpdate ) ) return false;
			Tap( State, Executor, ImGuiKey_Enter, bUpdate );
			return true;
		}
		// checkboxへfocusし、spaceで実値を一度切り替える。
		bool Toggle( FWorldEditorState& State, IDrawExecutor& Executor, const char* Label )
		{
			if ( !TabTo( State, Executor, Label ) ) return false;
			Tap( State, Executor, ImGuiKey_Space );
			return true;
		}
		// numeric widgetをkeyboard text inputで置換してTabでedit境界を閉じる。
		bool SetNumber( FWorldEditorState& State, IDrawExecutor& Executor, const char* Label, const char* Value, bool bCloseEdit = true, bool bUpdate = true )
		{
			if ( !TabTo( State, Executor, Label, bUpdate ) ) return false;
			// 入力対象widgetのID。
			const ImGuiID Target = ItemId( Label );
			// Tabで始まったInputInt入力を維持し、DragFloatだけEnterでtextへ切り替える。
			const bool bAlreadyTyping = Context->ActiveId == Target && ImGui::GetInputTextState( Target ) != nullptr;
			if ( !bAlreadyTyping ) Tap( State, Executor, ImGuiKey_Enter, bUpdate );
			// 数値のtext入力が実際に始まったか。
			const bool bTextActivated = Context->ActiveId == Target && ( Context->TempInputId == Target || ImGui::GetInputTextState( Target ) != nullptr );
			bLastNumberTextActivated = bTextActivated;
			ImGui::GetIO().AddKeyEvent( ImGuiMod_Ctrl, true );
			ImGui::GetIO().AddKeyEvent( ImGuiKey_A, true );
			Frame( State, Executor, bUpdate );
			ImGui::GetIO().AddKeyEvent( ImGuiKey_A, false );
			ImGui::GetIO().AddKeyEvent( ImGuiMod_Ctrl, false );
			ImGui::GetIO().AddInputCharactersUTF8( Value );
			Frame( State, Executor, bUpdate );
			if ( bCloseEdit ) Tap( State, Executor, ImGuiKey_Tab, bUpdate );
			return bTextActivated;
		}
		// 数値text入力中の全選択置換を継続し、連続編集が1履歴か調べる。
		bool ReplaceActiveNumber( FWorldEditorState& State, IDrawExecutor& Executor, const char* Label, const char* Value )
		{
			// 入力対象widgetのID。
			const ImGuiID Target = ItemId( Label );
			if ( Context->ActiveId != Target || Context->TempInputId != Target ) return false;
			ImGui::GetIO().AddKeyEvent( ImGuiMod_Ctrl, true );
			ImGui::GetIO().AddKeyEvent( ImGuiKey_A, true );
			Frame( State, Executor );
			ImGui::GetIO().AddKeyEvent( ImGuiKey_A, false );
			ImGui::GetIO().AddKeyEvent( ImGuiMod_Ctrl, false );
			ImGui::GetIO().AddInputCharactersUTF8( Value );
			Frame( State, Executor );
			return Context->ActiveId == Target && Context->TempInputId == Target;
		}
		// Inspector panelが崩さず描画できたか返す。
		bool FramesBalanced() const { return bFramesBalanced; }
		// 独立font atlasを構築できたか返す。
		bool IsReady() const { return bFontsReady; }
		// 現windowの直接子widget IDを計算する。
		ImGuiID ItemId( const char* Label ) const { return Window == nullptr ? 0 : ImHashStr( Label, 0, Window->ID ); }
		// 最後にTab巡回で照合したNav ID。
		ImGuiID LastNavId() const { return Context->NavId; }
		// 最後にTab巡回で照合したActive ID。
		ImGuiID LastActiveId() const { return Context->ActiveId; }
		// 最後にTab巡回で照合したwindow ID。
		ImGuiID CurrentWindowId() const { return Window == nullptr ? 0 : Window->ID; }
		// 最後にTab巡回したID。
		ImGuiID LastTabTargetId() const { return LastTabTarget; }
		// 最後のTab巡回回数。
		int32 LastTabAttemptCount() const { return LastTabAttempts; }
		// 最後のTab巡回が到達したか。
		bool LastTabWasReached() const { return LastTabReached; }
		// 最後の数値入力が文字列欄を有効化したか。
		bool LastNumberTextWasActivated() const { return bLastNumberTextActivated; }
		// panel内でPushIDするcurveの子widget IDを計算する。
		ImGuiID NestedItemId( const char* Parent, const char* Label ) const { const ImGuiID ParentId = ImHashStr( Parent, 0, Window->ID ); return ImHashStr( Label, 0, ParentId ); }
		// 実curve内Add key buttonへkeyboard focusする。
		bool TabToNested( FWorldEditorState& State, IDrawExecutor& Executor, const char* Parent, const char* Label )
		{
			// Add key widgetのID。
			const ImGuiID Target = NestedItemId( Parent, Label );
			// widgetへ到達するまでのTab回数。
			// 到達先までのTab巡回回数。
			for ( int32 Attempt = 0; Attempt < 256; ++Attempt ) { if ( Context->NavId == Target ) return true; Tap( State, Executor, ImGuiKey_Tab ); }
			return Context->NavId == Target;
		}
	private:
		// 入力開始前に存在したImGui文脈。
		ImGuiContext* Previous = nullptr;
		// 独立contextのkeyboard設定。
		ImGuiIO* Io = nullptr;
		// keyboard navigation専用文脈。
		ImGuiContext* Context = nullptr;
		// 直近frameで実panelを描いたwindow。
		ImGuiWindow* Window = nullptr;
		// Tab巡回と数値入力の診断値。
		ImGuiID LastTabTarget = 0;
		// Tabが目的widgetへ到達した印。
		bool LastTabReached = false;
		// 最後の巡回で送ったTab数。
		int32 LastTabAttempts = 0;
		// 数値widgetがtext入力へ入った印。
		bool bLastNumberTextActivated = false;
		// font atlasを構築できた印。
		bool bFontsReady = false;
		// Begin/NewFrame/Renderの対応が保たれた印。
		bool bFramesBalanced = true;
	};

	// command streamから通常Modelと材質効果Modelの個数を数える。
	struct FModelInspectorCommandCounts
	{
		// 通常経路へ残ったModel command数。
		int32 Native = 0;
		// 選択材質効果command数。
		int32 Effect = 0;
		// 効果commandが参照した個体handle。
		int32 EffectHandle = -1;
	};
	// Model command列を検査し、効果commandの固定payloadを控える。
	FModelInspectorCommandCounts InspectModelCommands( const FDrawBuffer& Buffer )
	{
		FModelInspectorCommandCounts Counts;
		// 各commandの種別とpayloadを読むcallback。
		auto Visit = [&Counts]( const FDrawCommandView& Command )
		{
			if ( Command.Type == EDrawCommand::Model3D ) ++Counts.Native;
			else if ( Command.Type == EDrawCommand::ModelMaterial3D )
			{
				++Counts.Effect;
				// 効果commandが保持する個体handle。
				const FDrawModelMaterialData Data = Command.Read<FDrawModelMaterialData>();
				Counts.EffectHandle = Data.Handle;
			}
		};
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, Visit );
		return Counts;
	}

		// 対象objectのbindingを安全に読み、optional未設定ならnullptrを返す。
	const FWorldModelMaterialEffect* FindModelBinding( const FWorldEditorState& State, int32 ObjectId )
	{
		// bindingを読む対象object。
		const FWorldObject* Object = State.Session.Document.Get().FindObject( ObjectId );
		if ( Object == nullptr || !Object->Model.IsSet() || !Object->Model.GetValue().MaterialEffect.IsSet() ) return nullptr;
		return &Object->Model.GetValue().MaterialEffect.GetValue();
	}

	// pathとoptional bindingを持つ二個体worldをfixture用に作る。
	FWorldDefinition MakeModelInspectorWorld( const FString& ModelPath )
	{
		// rootとsiblingを入れるfixture world。
		FWorldDefinition World;
		// Inspectorで編集するroot object。
		FWorldObject Root;
		// 同pathの独立instanceを持つsibling object。
		FWorldObject Sibling;
		World.Name = "Model material inspector self-test";
		World.NextId = 3;
		// Inspectorで編集するroot model object。
		Root.Id = 1;
		Root.Name = "Inspector target";
		Root.Model = FWorldModel();
		Root.Model.GetValue().Path = ModelPath;
		World.Objects.Add( Root );
		// 同pathで別instanceを持つ比較用object。
		Sibling.Id = 2;
		Sibling.Name = "Same path sibling";
		Sibling.Model = FWorldModel();
		Sibling.Model.GetValue().Path = ModelPath;
		Sibling.Model.GetValue().MaterialEffect = FWorldModelMaterialEffect();
		Sibling.Model.GetValue().MaterialEffect.GetValue().MaterialIndex = 3;
		Sibling.Model.GetValue().MaterialEffect.GetValue().Effect.Material.bEnabled = true;
		World.Objects.Add( Sibling );
		return World;
	}
}

namespace NWorldTest
{
	// 実Inspector操作でmodel binding、履歴、preview transport、保存再読込を確認する。
	void RunWorldModelInspectorEditorChecks( NAssetTest::FChecker& Checker, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Directory )
	{
		// 呼出元のchecker group文字列を保存する。
		const char* PreviousGroup = Checker.Group;
		Checker.Group = "world-model-material-inspector-editor";
		// Inspector操作を送る独立keyboard context。
		FModelInspectorKeyboard Input;
		// 自己確認専用asset root。
		const FString FixtureRoot = Directory / "WorldModelInspectorEditor";
		// 実widgetを描くeditor state。
		FWorldEditorState State;
		// fresh open結果を保持する独立state。
		FWorldEditorState Fresh;
		// fixture中だけasset manager rootを切り替えるowner。
		FModelInspectorRootScope RootScope( Assets, FixtureRoot );
		// fixtureをcache解放後に削除するowner。
		FModelInspectorFilesScope Files{ Assets };
		Files.Paths.Add( FixtureRoot / "inspector_model.x" );
		Files.Paths.Add( FixtureRoot / "probe_2x2.tga" );
		Files.Paths.Add( FixtureRoot / "Worlds/model_material_inspector_saved.world" );
		// 実4材質native modelと借用diffuse画像を書けたか。
		const bool bFixtureWritten = NFileSystem::EnsureDirectory( FixtureRoot ) && NFileSystem::EnsureDirectory( FixtureRoot / "Worlds" ) && NAssetTest::WriteStaticModelSurfaceFixture( FixtureRoot, "inspector_model.x" );
		Checker.Check( bFixtureWritten, "実4材質modelとsource image fixtureを作る" );
		// ImGui fontとfixtureが揃わなければ残りのkeyboard確認はできない。
		if ( !Input.IsReady() || !bFixtureWritten ) { Checker.Check( false, "editor input contextとfixtureを準備する" ); Checker.Group = PreviousGroup; return; }
		// SaveAs先の既存fileを誤って上書きしない。
		// SaveAsとfresh openに使う新規world path。
		const FString SavedPath = "Worlds/model_material_inspector_saved.world";
		// SaveAs先が既存assetを上書きしないこと。
		const bool bSavePathAvailable = !Assets.ResolvePath( SavedPath ).IsSet();
		Checker.Check( bSavePathAvailable, "SaveAsのfixture pathが既存fileと衝突しない" );
		if ( !bSavePathAvailable ) { Checker.Group = PreviousGroup; return; }
		// fixture load結果を検査中保持するsource。
		TSharedPtr<CModel> LoadedSource = Assets.LoadModel( "inspector_model.x" );
		// 材質slot確認に使う不変surface snapshot。
		TSharedPtr<const FStaticModelSurface> Prepared;
		State.Session.Reset( MakeModelInspectorWorld( "inspector_model.x" ), FString() );
		State.Session.Document.MarkSaved();
		State.AssetInfo = MakeUnique<FAssetEditorInfo>( Assets );
		// 共有EditorAssetInfoで準備した不変snapshot。
		Prepared = State.AssetInfo->PrepareModelSurface( "inspector_model.x" );
		// 実sourceと4材質snapshotがslot 3を持つか。
		// 実sourceと4材質snapshotがslot 3を持つか。
		const bool bRealFixtureReady = LoadedSource.IsValid() && Prepared.IsValid() && Prepared->GetData().Materials.Num() == 4 && Prepared->GetData().Sections.Num() >= 2 && Prepared->GetData().Sections[1].MaterialIndex == 3;
		Checker.Check( bRealFixtureReady, "native fixtureが実slot 3を持つ4材質snapshotを準備する" );
		// 共有asset cacheにnative instanceがあるか。
		// 共有asset cacheにnative instanceがあるか。
		const bool bSharedInstanceReady = State.AssetInfo->GetModelInstance( "inspector_model.x" ).IsValid();
		Checker.Check( bSharedInstanceReady, "共有asset viewも準備済みmodel instanceを持つ" );
		// GPU効果命令を実行できるbackendか。
		// GPU効果命令を実行できるbackendか。
		const bool bEffectExecutorReady = Executor.SupportsModelMaterialEffects();
		Checker.Check( bEffectExecutorReady, "実描画executorがmodel material effectを提供する" );
		if ( !bRealFixtureReady || !bSharedInstanceReady || !bEffectExecutorReady ) { NDebugUI::ReleaseWorldEditor( State ); Checker.Group = PreviousGroup; return; }
		// rootと同path siblingへ別々のpreview ownerを作る。
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.0f, &Executor );
		// 編集対象root objectのpreview key。
		FWorldEditorModelPreviewKey RootKey;
		RootKey.ObjectIds.Add( 1 );
		// 同path sibling objectのpreview key。
		FWorldEditorModelPreviewKey SiblingKey;
		SiblingKey.ObjectIds.Add( 2 );
		// root専用preview owner。
		FWorldEditorModelPreview* RootPreview = State.ModelPreviews.Find( RootKey );
		// sibling専用preview owner。
		FWorldEditorModelPreview* SiblingPreview = State.ModelPreviews.Find( SiblingKey );
		// 共有cache instanceとの非共有確認用参照。
		const TSharedPtr<CModelInstance> SharedInstance = State.AssetInfo->GetModelInstance( "inspector_model.x" );
		// 未設定rootを記録する初期buffer。
		FDrawBuffer InitialBuffer;
		// 初期bufferへの描画context。
		FDrawContext InitialDraw( InitialBuffer );
		NWorldEditorUI::RecordModelObject( State, RootKey, FTransform(), InitialDraw );
		// 初期root記録の通常/effect command数。
		const FModelInspectorCommandCounts InitialCounts = InspectModelCommands( InitialBuffer );
		Checker.Check( RootPreview != nullptr && SiblingPreview != nullptr && RootPreview->Animator.Instance.IsValid() && SiblingPreview->Animator.Instance.IsValid() && RootPreview->Animator.Instance.Get() != SiblingPreview->Animator.Instance.Get() && RootPreview->Animator.Instance.Get() != SharedInstance.Get(), "同pathのroot・sibling・共有cacheが別々のmodel instanceを使う" );
		Checker.Check( InitialCounts.Native == 1 && InitialCounts.Effect == 0, "未設定rootは通常Model commandだけを記録する" );
		// optional binding追加と履歴境界を実buttonへ送る。
		Checker.Check( Input.Frame( State, Executor ) && Input.Activate( State, Executor, "Add material effect (disabled)###ModelMaterialBinding" ), "実Inspector buttonからoptional material bindingを追加する" );
		// 追加後に保存されたroot object。
		const FWorldObject* ObjectAfterAdd = State.Session.Document.Get().FindObject( 1 );
		// 追加直後のoptional bindingが既定disabledか。
		const bool bAddedDefault = ObjectAfterAdd != nullptr && ObjectAfterAdd->Model.IsSet() && ObjectAfterAdd->Model.GetValue().MaterialEffect.IsSet() && !ObjectAfterAdd->Model.GetValue().MaterialEffect.GetValue().Effect.Material.bEnabled;
		Checker.Check( bAddedDefault && State.Session.Document.IsDirty() && State.Session.Document.GetNumUndo() == 1, "Addは初期disabled値を保ち、一つのdirty履歴にする" );
		// Undoで再出現したAdd buttonをImGuiへ一度登録する。
		const bool bAddUndone = State.Session.Document.Undo();
		Checker.Check( bAddUndone && FindModelBinding( State, 1 ) == nullptr, "optional binding追加をUndoする" );
		Input.Frame( State, Executor );
		// Redo後に新しい編集widget群をImGuiへ登録する。
		const bool bAddRedone = State.Session.Document.Redo();
		Checker.Check( bAddRedone && FindModelBinding( State, 1 ) != nullptr, "optional binding追加をRedoする" );
		Input.Frame( State, Executor );
		// slot番号と両面設定を実widgetへ入力する。
		// 実InputIntで入力したslot番号。
		const bool bIndexTyped = Input.SetNumber( State, Executor, "Material index", "3" );
		// InputInt後のbinding値。
		const FWorldModelMaterialEffect* BindingAfterIndex = FindModelBinding( State, 1 );
		Checker.Check( bIndexTyped && BindingAfterIndex != nullptr && BindingAfterIndex->MaterialIndex == 3, "Material index widgetで実4材質fixtureのslot 3を選ぶ (typed=%d reached=%d tabs=%d nav=%08X target=%08X active=%08X window=%08X text=%d value=%d)", bIndexTyped ? 1 : 0, Input.LastTabWasReached() ? 1 : 0, Input.LastTabAttemptCount(), static_cast<unsigned int>( Input.LastNavId() ), static_cast<unsigned int>( Input.LastTabTargetId() ), static_cast<unsigned int>( Input.LastActiveId() ), static_cast<unsigned int>( Input.CurrentWindowId() ), Input.LastNumberTextWasActivated() ? 1 : 0, BindingAfterIndex != nullptr ? BindingAfterIndex->MaterialIndex : -999 );
		Checker.Check( Input.Toggle( State, Executor, "Two-sided" ) && FindModelBinding( State, 1 ) != nullptr && FindModelBinding( State, 1 )->bTwoSided, "Two-sided widgetがbinding値を変更する" );
		Checker.Check( Input.Toggle( State, Executor, "Enable material effect" ) && FindModelBinding( State, 1 ) != nullptr && FindModelBinding( State, 1 )->Effect.Material.bEnabled, "共通Enable widgetがmodel bindingを有効にする" );
		// 実DragFloatで入力したduration。
		const bool bDurationTyped = Input.SetNumber( State, Executor, "Duration (seconds)", "3.25" );
		// Duration入力後のbinding値。
		const FWorldModelMaterialEffect* BindingAfterDuration = FindModelBinding( State, 1 );
		Checker.Check( bDurationTyped && BindingAfterDuration != nullptr && NMath::Abs( BindingAfterDuration->Effect.Duration - 3.25f ) < 0.002f, "Duration widgetの値がmodel definitionへ保存される" );
		// 一つの連続text editへ複数値を送り履歴coalesceを確認する。
		// curve追加前のundo履歴数。
		const int32 UndoBeforeCurve = State.Session.Document.GetNumUndo();
		// Emission curveのAdd keyへ到達したか。
		const bool bCurveAddFocused = Input.TabToNested( State, Executor, "Emission curve", "Add key" );
		if ( bCurveAddFocused ) Input.Tap( State, Executor, ImGuiKey_Enter );
		// curve key追加後のbinding値。
		const FWorldModelMaterialEffect* BindingAfterCurve = FindModelBinding( State, 1 );
		Checker.Check( bCurveAddFocused && BindingAfterCurve != nullptr && BindingAfterCurve->Effect.Material.Emission.Keys.Num() == 1 && State.Session.Document.GetNumUndo() == UndoBeforeCurve + 1, "Emission curveの実Add keyをmodel値と独立履歴へ反映する" );
		// Opacity連続入力前のundo履歴数。
		const int32 UndoBeforeOpacity = State.Session.Document.GetNumUndo();
		// Opacity入力欄へ到達したか。
		const bool bOpacityFocused = Input.TabTo( State, Executor, "Opacity" );
		// 連続Opacity入力の開始値。
		const bool bOpacityFirst = bOpacityFocused && Input.SetNumber( State, Executor, "Opacity", "0.63", false );
		// 同じ入力session内の中間値。
		const bool bOpacitySecond = Input.ReplaceActiveNumber( State, Executor, "Opacity", "0.51" );
		// 同じ入力session内の最終値。
		const bool bOpacityThird = Input.ReplaceActiveNumber( State, Executor, "Opacity", "0.47" );
		Input.Tap( State, Executor, ImGuiKey_Tab );
		// 連続Opacity入力後のbinding値。
		const FWorldModelMaterialEffect* BindingAfterOpacity = FindModelBinding( State, 1 );
		Checker.Check( bOpacityFirst && bOpacitySecond && bOpacityThird && BindingAfterOpacity != nullptr && NMath::Abs( BindingAfterOpacity->Effect.Opacity - 0.47f ) < 0.002f && State.Session.Document.GetNumUndo() == UndoBeforeOpacity + 1, "連続Opacity入力は最終値0.47を一つの履歴へまとめる" );
		// 直前のOpacity editをUndoできたか。
		const bool bOpacityUndone = State.Session.Document.Undo();
		// Undo直後にbindingが存在し、値が既定へ戻ったか。
		const FWorldModelMaterialEffect* BindingAfterOpacityUndo = FindModelBinding( State, 1 );
		const bool bOpacityUndoMatches = BindingAfterOpacityUndo != nullptr && NMath::Abs( BindingAfterOpacityUndo->Effect.Opacity - 1.0f ) < 0.002f;
		Checker.Check( bOpacityUndone && bOpacityUndoMatches, "連続Opacity editのUndoで既定値1.0へ戻す" );
		// Redoはdocument全体を再配置するため古いbinding参照を使わない。
		const bool bOpacityRedone = State.Session.Document.Redo();
		// Redo後に新しいbindingからOpacity値を読む。
		const FWorldModelMaterialEffect* BindingAfterOpacityRedo = FindModelBinding( State, 1 );
		const bool bOpacityRedoMatches = BindingAfterOpacityRedo != nullptr && NMath::Abs( BindingAfterOpacityRedo->Effect.Opacity - 0.47f ) < 0.002f;
		Checker.Check( bOpacityRedone && bOpacityRedoMatches, "連続Opacity editのRedoで最終値0.47へ戻す" );
		State.Session.Document.MarkSaved();
		// 更新後の専用previewだけをroot transportで操作する。
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.0f, &Executor );
		RootPreview = State.ModelPreviews.Find( RootKey );
		SiblingPreview = State.ModelPreviews.Find( SiblingKey );
		// rootとsiblingの効果準備状態。
		const bool bBothPrepared = RootPreview != nullptr && SiblingPreview != nullptr && RootPreview->Animator.GetMaterialEffect().IsPrepared() && SiblingPreview->Animator.GetMaterialEffect().IsPrepared();
		if ( SiblingPreview != nullptr ) SiblingPreview->Animator.GetMaterialEffect().Stop();
		Checker.Check( bBothPrepared && Input.Activate( State, Executor, "Play" ), "一致するslot 3 bindingでrootだけPlay controlを表示する" );
		RootPreview = State.ModelPreviews.Find( RootKey );
		SiblingPreview = State.ModelPreviews.Find( SiblingKey );
		// rootだけが再生中か。
		const bool bRootPlaying = RootPreview != nullptr && RootPreview->Animator.GetMaterialEffect().GetPlaybackState() == FMeshMaterialEffectPlayback::EEffectPlaybackState::Playing;
		// siblingの再生時計が停止中か。
		const bool bSiblingStopped = SiblingPreview != nullptr && SiblingPreview->Animator.GetMaterialEffect().GetPlaybackState() == FMeshMaterialEffectPlayback::EEffectPlaybackState::Stopped;
		Checker.Check( bRootPlaying && bSiblingStopped && !State.Session.Document.IsDirty(), "Playはroot専用時計だけを進め、documentを変更しない" );
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.25f, &Executor );
		RootPreview = State.ModelPreviews.Find( RootKey );
		// Record前のroot clock。
		const float64 RootTimeBeforeRecord = RootPreview != nullptr ? RootPreview->Animator.GetMaterialEffect().GetEffectTime() : -1.0;
		// 再生中のroot記録buffer。
		FDrawBuffer PlayingBuffer;
		// 再生中bufferへの描画context。
		FDrawContext PlayingDraw( PlayingBuffer );
		NWorldEditorUI::RecordModelObject( State, RootKey, FTransform(), PlayingDraw );
		// 再生中root記録のcommand数とhandle。
		const FModelInspectorCommandCounts PlayingCounts = InspectModelCommands( PlayingBuffer );
		Checker.Check( RootPreview != nullptr && NMath::Abs( RootTimeBeforeRecord - 0.25 ) < 0.002 && PlayingCounts.Native == 0 && PlayingCounts.Effect == 1 && PlayingCounts.EffectHandle >= 0 && SharedInstance.IsValid() && PlayingCounts.EffectHandle != SharedInstance->GetHandle(), "record repeatは専用instanceの材質効果命令を出し時計を読取だけにする" );
		// siblingの独立記録buffer。
		FDrawBuffer SiblingBuffer;
		// sibling bufferへの描画context。
		FDrawContext SiblingDraw( SiblingBuffer );
		NWorldEditorUI::RecordModelObject( State, SiblingKey, FTransform(), SiblingDraw );
		// sibling記録のcommand数。
		const FModelInspectorCommandCounts SiblingCounts = InspectModelCommands( SiblingBuffer );
		Checker.Check( SiblingCounts.Native == 1 && SiblingCounts.Effect == 0 && SiblingPreview != nullptr && SiblingPreview->Animator.GetMaterialEffect().GetPlaybackState() == FMeshMaterialEffectPlayback::EEffectPlaybackState::Stopped, "同path siblingはroot再生へ共有時計を持ち込まない" );
		Checker.Check( Input.Activate( State, Executor, "Pause" ), "Pauseを実Inspectorのtransport buttonから送る" );
		RootPreview = State.ModelPreviews.Find( RootKey );
		// Pause直後のroot clock。
		const float64 PausedTime = RootPreview != nullptr ? RootPreview->Animator.GetMaterialEffect().GetEffectTime() : -1.0;
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.5f, &Executor );
		RootPreview = State.ModelPreviews.Find( RootKey );
		Checker.Check( RootPreview != nullptr && RootPreview->Animator.GetMaterialEffect().GetPlaybackState() == FMeshMaterialEffectPlayback::EEffectPlaybackState::Paused && NMath::Abs( RootPreview->Animator.GetMaterialEffect().GetEffectTime() - PausedTime ) < 0.002, "Pause後の更新でroot時計を保持する" );
		Checker.Check( Input.Activate( State, Executor, "Stop" ), "Stopを実Inspectorのtransport buttonから送る" );
		RootPreview = State.ModelPreviews.Find( RootKey );
		// Stop後のroot clock。
		const float64 StoppedTime = RootPreview != nullptr ? RootPreview->Animator.GetMaterialEffect().GetEffectTime() : -1.0;
		// 停止中のnative root記録buffer。
		FDrawBuffer StoppedBuffer;
		// 停止中bufferへの描画context。
		FDrawContext StoppedDraw( StoppedBuffer );
		NWorldEditorUI::RecordModelObject( State, RootKey, FTransform(), StoppedDraw );
		// 停止中root記録のnative復帰確認。
		const FModelInspectorCommandCounts StoppedCounts = InspectModelCommands( StoppedBuffer );
		Checker.Check( RootPreview != nullptr && NMath::Abs( StoppedTime ) < 0.002 && StoppedCounts.Native == 1 && StoppedCounts.Effect == 0, "Stopは時計0へ戻し同じ専用個体をnative Model commandへ復帰する" );
		// transportがdocument履歴を増やさない基準。
		const int32 UndoBeforeRestart = State.Session.Document.GetNumUndo();
		Checker.Check( Input.Activate( State, Executor, "Restart" ), "Restartを実Inspectorのtransport buttonから送る" );
		RootPreview = State.ModelPreviews.Find( RootKey );
		Checker.Check( RootPreview != nullptr && RootPreview->Animator.GetMaterialEffect().GetPlaybackState() == FMeshMaterialEffectPlayback::EEffectPlaybackState::Playing && NMath::Abs( RootPreview->Animator.GetMaterialEffect().GetEffectTime() ) < 0.002 && State.Session.Document.GetNumUndo() == UndoBeforeRestart && !State.Session.Document.IsDirty(), "Restartはroot専用時計だけを0から再開し履歴へ入らない" );
		// 保存値を変更したframeに古いownerのtransportを隠す。
		// preview更新を止めたままdurationだけ編集する結果。
		const bool bStaleEdit = Input.SetNumber( State, Executor, "Duration (seconds)", "4.5", true, false );
		RootPreview = State.ModelPreviews.Find( RootKey );
		// 保存変更前ownerのclock。
		const float64 OldOwnerTime = RootPreview != nullptr ? RootPreview->Animator.GetMaterialEffect().GetEffectTime() : -1.0;
		Checker.Check( bStaleEdit && !Input.TabTo( State, Executor, "Play", false ) && RootPreview != nullptr && RootPreview->Definition.MaterialEffect.IsSet() && NMath::Abs( RootPreview->Definition.MaterialEffect.GetValue().Effect.Duration - 3.25f ) < 0.002f && NMath::Abs( RootPreview->Animator.GetMaterialEffect().GetEffectTime() - OldOwnerTime ) < 0.002, "保存定義が変わった後は前回ownerのtransportを操作できない" );
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.0f, &Executor );
		RootPreview = State.ModelPreviews.Find( RootKey );
		Checker.Check( RootPreview != nullptr && RootPreview->Definition.MaterialEffect.IsSet() && NMath::Abs( RootPreview->Definition.MaterialEffect.GetValue().Effect.Duration - 4.5f ) < 0.002f && RootPreview->Animator.GetMaterialEffect().GetPlaybackState() == FMeshMaterialEffectPlayback::EEffectPlaybackState::Playing, "次updateが新durationのownerを作り直し再生時計0から始める" );
				// 保存設定が切り替わったときpath一致もtransportを閉じる条件に含める。
		// 同じfileを異なる保存文字列で指すpath。
		const FString EquivalentModelPath = "./inspector_model.x";
		// stale owner検査中だけpathを書き換えるobject。
		FWorldObject* PathEditable = NWorldEdit::EditObject( State.Session.Document, 1, "Inspector.Model.Path" );
		// Pathだけを別保存文字列へ編集できたか。
		const bool bPathEdited = PathEditable != nullptr && PathEditable->Model.IsSet();
		if ( bPathEdited ) PathEditable->Model.GetValue().Path = EquivalentModelPath;
		State.Session.Document.EndEditSession();
		RootPreview = State.ModelPreviews.Find( RootKey );
		// Pathだけ変える前後で保つclock。
		const float64 TimeBeforePathCheck = RootPreview != nullptr ? RootPreview->Animator.GetMaterialEffect().GetEffectTime() : -1.0;
		// 古いownerの再生buttonが隠れたか。
		const bool bOldPathControlsHidden = !Input.TabTo( State, Executor, "Play", false );
		Checker.Check( bPathEdited && bOldPathControlsHidden && RootPreview != nullptr && RootPreview->Definition.Path == "inspector_model.x" && NMath::Abs( RootPreview->Animator.GetMaterialEffect().GetEffectTime() - TimeBeforePathCheck ) < 0.002, "同じfileを指すPath文字列だけを変えても旧ownerのtransportを無効にする" );
		PathEditable = NWorldEdit::EditObject( State.Session.Document, 1, "Inspector.Model.Path.Restore" );
		// previewの元Pathへ復帰できたか。
		const bool bPathRestored = PathEditable != nullptr && PathEditable->Model.IsSet();
		if ( bPathRestored ) PathEditable->Model.GetValue().Path = "inspector_model.x";
		State.Session.Document.EndEditSession();
		State.Session.Document.MarkSaved();
		Checker.Check( bPathRestored, "Path-only stale設定を元のmodel pathへ戻す" );
		// Reset bindingはoptionalを残し、既定値へ一括で戻す。
		Checker.Check( Input.Activate( State, Executor, "Reset binding" ), "Reset bindingを実Inspector buttonから実行する" );
		// Reset後のoptional binding値。
		const FWorldModelMaterialEffect* ResetBinding = FindModelBinding( State, 1 );
		Checker.Check( ResetBinding != nullptr && ResetBinding->MaterialIndex == 0 && !ResetBinding->bTwoSided && !ResetBinding->Effect.Material.bEnabled && NMath::Abs( ResetBinding->Effect.Duration - 2.0f ) < 0.002f && NMath::Abs( ResetBinding->Effect.Opacity - 1.0f ) < 0.002f, "Reset bindingはoptionalを残して各保存値を既定へ戻す" );
		// Reset操作をUndoできたか。
		const bool bResetUndone = State.Session.Document.Undo();
		// Reset Undo後に復帰したbinding。
		const FWorldModelMaterialEffect* BindingAfterResetUndo = FindModelBinding( State, 1 );
		Checker.Check( bResetUndone && BindingAfterResetUndo != nullptr && BindingAfterResetUndo->MaterialIndex == 3 && BindingAfterResetUndo->Effect.Material.bEnabled, "Reset bindingを一度のUndoで元の設定へ戻す" );
		// Reset操作をRedoできたか。
		const bool bResetRedone = State.Session.Document.Redo();
		// Reset Redo後の既定binding。
		const FWorldModelMaterialEffect* BindingAfterResetRedo = FindModelBinding( State, 1 );
		Checker.Check( bResetRedone && BindingAfterResetRedo != nullptr && BindingAfterResetRedo->MaterialIndex == 0 && !BindingAfterResetRedo->Effect.Material.bEnabled, "Reset bindingのRedoで既定値を復元する" );
		Checker.Check( State.Session.Document.Undo(), "Remove前に元のbinding設定へ戻す" );
		// Remove操作直後の対象object。
		const bool bRemoveClicked = Input.Activate( State, Executor, "Remove material effect###ModelMaterialBinding" );
		const FWorldObject* ObjectAfterRemove = State.Session.Document.Get().FindObject( 1 );
		const FWorldModelMaterialEffect* BindingAfterRemove = FindModelBinding( State, 1 );
		Checker.Check( bRemoveClicked && ObjectAfterRemove != nullptr && ObjectAfterRemove->Model.IsSet() && BindingAfterRemove == nullptr, "Remove buttonでoptional binding自体を消す" );
		// Remove操作をUndoできたか。
		const bool bRemoveUndone = State.Session.Document.Undo();
		const FWorldObject* ObjectAfterRemoveUndo = State.Session.Document.Get().FindObject( 1 );
		const FWorldModelMaterialEffect* BindingAfterRemoveUndo = FindModelBinding( State, 1 );
		Checker.Check( bRemoveUndone && ObjectAfterRemoveUndo != nullptr && ObjectAfterRemoveUndo->Model.IsSet() && BindingAfterRemoveUndo != nullptr && BindingAfterRemoveUndo->MaterialIndex == 3, "RemoveをUndoして以前のbinding全体を戻す" );
		// Disableでもslot・二面・duration・opacityの保存値を残す。
		Checker.Check( Input.Toggle( State, Executor, "Enable material effect" ), "effectを共通Enableからdisabledへ切り替える" );
		// disabled後も維持されたbinding値。
		const FWorldModelMaterialEffect* DisabledBinding = FindModelBinding( State, 1 );
		// 無効化後もbinding値が保持されたか。
		const bool bDisabledValuesKept = DisabledBinding != nullptr && !DisabledBinding->Effect.Material.bEnabled && DisabledBinding->MaterialIndex == 3 && DisabledBinding->bTwoSided && NMath::Abs( DisabledBinding->Effect.Duration - 4.5f ) < 0.002f && NMath::Abs( DisabledBinding->Effect.Opacity - 0.47f ) < 0.002f;
		Checker.Check( bDisabledValuesKept, "disabledでもslot 3・Two-sided・Duration・Opacityを保存する" );
		// SaveAsが成功しdocumentがcleanになったか。
		const bool bSaved = NDebugUI::SaveWorldFile( State, SavedPath );
		Checker.Check( bSaved && !State.Session.Document.IsDirty(), "実SaveAs経路がmodel bindingを保存してcleanにする" );
		// 保存結果を独立に開き直す新editor state。
		Fresh.AssetInfo = MakeUnique<FAssetEditorInfo>( Assets );
		// 保存したworldを独立stateで開けたか。
		const bool bFreshOpened = bSaved && NDebugUI::OpenWorldFile( Fresh, SavedPath );
		// diskから開いたroot object。
		const FWorldObject* FreshObject = bFreshOpened ? Fresh.Session.Document.Get().FindObject( 1 ) : nullptr;
		// diskから戻ったmaterial binding。
		const FWorldModelMaterialEffect* FreshBinding = FreshObject != nullptr && FreshObject->Model.IsSet() && FreshObject->Model.GetValue().MaterialEffect.IsSet() ? &FreshObject->Model.GetValue().MaterialEffect.GetValue() : nullptr;
		Checker.Check( bFreshOpened && FreshBinding != nullptr && !FreshBinding->Effect.Material.bEnabled && FreshBinding->MaterialIndex == 3 && FreshBinding->bTwoSided && NMath::Abs( FreshBinding->Effect.Duration - 4.5f ) < 0.002f && NMath::Abs( FreshBinding->Effect.Opacity - 0.47f ) < 0.002f && !Fresh.Session.Document.IsDirty() && Fresh.Session.Document.GetNumUndo() == 0, "fresh OpenWorldのdisk値はdisabled binding全fieldを保ち履歴を持たない" );
		Checker.Check( Input.FramesBalanced(), "全ImGui keyboard frameがNewFrame/Renderとwindow stackを閉じる" );
		NDebugUI::ReleaseWorldEditor( Fresh );
		NDebugUI::ReleaseWorldEditor( State );
		Checker.Group = PreviousGroup;
	}
}