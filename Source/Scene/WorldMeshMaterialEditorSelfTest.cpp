// SPDX-License-Identifier: NOASSERTION
#include "WorldMeshMaterialSelfTestSupport.h"

#include "Library/DebugUI/WorldEditorInternal.h"
#include "Library/DebugUI/WorldEditorMeshMaterialPanel.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/WorldAsset.h"
#include "Library/Framework/Asset/EditorAssetInfo.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/Render/MeshMaterialEffectResources.h"
#include "Library/Framework/World/Mesh/EditMeshPrimitives.h"
#include "Library/Framework/World/WorldRender.h"
#include "Library/Platform/FileSystem.h"
#include "VendorModules/ImGui/imgui_internal.h"

namespace
{
	// AssetManagerのrootを自己確認用fixtureへ切り替え、scope終了時に戻す。
	struct FMeshMaterialEditorAssetRoot
	{
		// fixtureへ切り替える前のasset root。
		FString Previous;
		// 対象worldを所有するasset manager。
		CAssetManager& Assets;

		// 現在rootを保存してfixture用rootを選ぶ。
		FMeshMaterialEditorAssetRoot( CAssetManager& InAssets, const FString& Root ) : Previous( InAssets.GetRootDirectory() ), Assets( InAssets ) { Assets.SetRootDirectory( Root ); }
		// fixture操作後に呼出元のasset rootへ戻す。
		~FMeshMaterialEditorAssetRoot() { Assets.SetRootDirectory( Previous ); }
	};

	// 自己確認が作った画像fileをasset参照解放後に片付ける。
	struct FMeshMaterialEditorFixtureFiles
	{
		// 画像参照を保持するasset manager。
		CAssetManager& Assets;
		// fixture作成時に専用rootへ保存した画像file paths。
		TArray<FString> Paths;

		// 画像asset参照を解放し、作成fileだけを削除する。
		~FMeshMaterialEditorFixtureFiles()
		{
			Assets.ReleaseUnused();
			Assets.Tick( 0.0f );
			for ( const FString& Path : Paths ) NFileSystem::RemoveFile( Path );
		}
	};

	// 2x2の不透明白TGAを作り、source・mask・noise・組み込み白に使う。
	std::vector<uint8> MakeMeshMaterialEditorTga()
	{
		// 無圧縮32bit TGA headerと4つの白pixel。
		std::vector<uint8> Bytes = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 32, 0x28 };
		for ( int32 Index = 0; Index < 4; ++Index ) { Bytes.push_back( 255 ); Bytes.push_back( 255 ); Bytes.push_back( 255 ); Bytes.push_back( 255 ); }
		return Bytes;
	}

	// draw command列から指定種別の数を返す。
	int32 CountMeshMaterialEditorCommands( const FDrawBuffer& Buffer, EDrawCommand Type )
	{
		// 一致したmesh commandの数。
		int32 Count = 0;
		// draw buffer内のcommandを数える処理。
		const auto CountCommand = [&]( const FDrawCommandView& Command ) { if ( Command.Type == Type ) ++Count; };
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, CountCommand );
		return Count;
	}

	// 実際のmesh material panelを独立ImGui文脈で描き、world文書へ戻す。
	class FMeshMaterialInputContext
	{
	public:
		// 他のUI状態を引き継がないkeyboard入力用文脈を作る。
		FMeshMaterialInputContext() : Previous( ImGui::GetCurrentContext() ), Context( ImGui::CreateContext() )
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

		// 独立文脈を破棄し、開始前に選ばれていた文脈を戻す。
		~FMeshMaterialInputContext()
		{
			ImGui::DestroyContext( Context );
			ImGui::SetCurrentContext( Previous );
		}
		// ImGui contextの複製を禁止する。
		FMeshMaterialInputContext( const FMeshMaterialInputContext& ) = delete;
		// ImGui contextの代入を禁止する。
		FMeshMaterialInputContext& operator=( const FMeshMaterialInputContext& ) = delete;

		// material panelの変更を履歴付きworld documentへ書き戻して描画する。
		bool Frame( FWorldEditorState& State, int32 ObjectId = 1 )
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos( ImVec2( 0.0f, 0.0f ), ImGuiCond_Always );
			ImGui::SetNextWindowSize( ImVec2( 1000.0f, 800.0f ), ImGuiCond_Always );
			if ( ImGui::Begin( "WorldMeshMaterialEditorSelfTest", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize ) )
			{
				Window = ImGui::GetCurrentWindow();
				const FWorldObject* Object = State.Session.Document.Get().FindObject( ObjectId );
				if ( Object != nullptr && Object->Mesh.IsSet() && !Object->Mesh.GetValue().Materials.IsEmpty() )
				{
					ImGui::PushID( "Mesh" );
					NWorldEditorUI::DrawMeshComponentBody( State, *Object );
					ImGui::PopID();
					if ( !ImGui::IsAnyItemActive() ) State.Session.Document.EndEditSession();
				}
			}
			ImGui::End();
			ImGui::Render();
			bFramesBalanced = bFramesBalanced && !Context->WithinFrameScope && Context->CurrentWindowStack.Size == 0;
			return bFramesBalanced;
		}

		// 指定keyを押して離し、両frameでpanelを更新する。
		void Tap( FWorldEditorState& State, ImGuiKey Key, int32 ObjectId = 1 )
		{
			ImGui::GetIO().AddKeyEvent( Key, true );
			Frame( State, ObjectId );
			ImGui::GetIO().AddKeyEvent( Key, false );
			Frame( State, ObjectId );
		}

		// tab巡回で指定widgetへkeyboard focusを移す。
		bool TabTo( FWorldEditorState& State, ImGuiID Id, int32 ObjectId = 1 )
		{
			for ( int32 Attempt = 0; Attempt < 256; ++Attempt )
			{
				if ( Context->NavId == Id ) return true;
				Tap( State, ImGuiKey_Tab, ObjectId );
			}
			return Context->NavId == Id;
		}

		// 現在のtest window内でpanel widgetのIDを得る。
		ImGuiID ItemId( const char* Label, int32 SlotIndex = 0 ) const
		{
			// 実際のInspectorがMesh componentへ積むID seed。
			const ImGuiID MeshId = ImHashStr( "Mesh", 0, Window->ID );
			// DrawMeshComponentBodyが材質slotへ積むID種。
			const ImGuiID SlotId = ImHashData( &SlotIndex, sizeof( SlotIndex ), MeshId );
			// material TreeNodeまでを含むImGui ID seed。
			const ImGuiID MaterialId = ImHashStr( "##Material", 0, SlotId );
			return ImHashStr( Label, 0, MaterialId );
		}

		// curve入力欄の実widget IDを返す。
		ImGuiID CurveItemId( const char* CurveLabel, const char* ItemLabel, int32 SlotIndex = 0, int32 KeyIndex = -1 ) const
		{
			const ImGuiID MeshId = ImHashStr( "Mesh", 0, Window->ID );
			const ImGuiID SlotId = ImHashData( &SlotIndex, sizeof( SlotIndex ), MeshId );
			const ImGuiID MaterialId = ImHashStr( "##Material", 0, SlotId );
			ImGuiID CurveId = ImHashStr( CurveLabel, 0, MaterialId );
			if ( KeyIndex >= 0 ) CurveId = ImHashData( &KeyIndex, sizeof( KeyIndex ), CurveId );
			return ImHashStr( ItemLabel, 0, CurveId );
		}

		// 数値dragをtext入力に切替え、指定文字列を実widgetへ入れる。
		bool ReplaceNumeric( FWorldEditorState& State, ImGuiID Id, const char* Text, int32 ObjectId = 1 )
		{
			Tap( State, ImGuiKey_Enter, ObjectId );
			const bool bActivated = Context->TempInputId == Id && Context->ActiveId == Id;
			ImGui::GetIO().AddKeyEvent( ImGuiMod_Ctrl, true );
			ImGui::GetIO().AddKeyEvent( ImGuiKey_A, true );
			Frame( State, ObjectId );
			ImGui::GetIO().AddKeyEvent( ImGuiKey_A, false );
			ImGui::GetIO().AddKeyEvent( ImGuiMod_Ctrl, false );
			ImGui::GetIO().AddInputCharactersUTF8( Text );
			Frame( State, ObjectId );
			return bActivated && Context->TempInputId == Id && Context->ActiveId == Id;
		}
		// 全frameのwindowとNewFrame/Renderの対応を返す。
		bool FramesBalanced() const { return bFramesBalanced; }
		// 独立font atlasを構築できたかを返す。
		bool IsReady() const { return bFontsReady; }

	private:
		// harness開始前に選ばれていたImGui文脈。
		ImGuiContext* Previous = nullptr;
		// 入力確認が所有するImGui文脈。
		ImGuiContext* Context = nullptr;
		// 最後に描いたtest windowからwidget IDを得る。
		ImGuiWindow* Window = nullptr;
		// font atlasが構築済みか。
		bool bFontsReady = false;
		// frame scopeがすべて閉じたままか。
		bool bFramesBalanced = true;
	};

	// UIのpreset操作、Undo/Redoと保存後のfresh loadを一続きで確認する。
	void TestMeshMaterialEditorSave( NAssetTest::FChecker& C, CAssetManager& Assets, FMeshMaterialInputContext& Input, const FString& TestDirectory )
	{
		C.Group = "world-mesh-material-editor-input-save";
		// material effect画像を保存する専用asset root。
		const FString FixtureRoot = TestDirectory / "world-mesh-material-editor-cache";
		FMeshMaterialEditorAssetRoot AssetRoot( Assets, FixtureRoot );
		// presetと組み込み白が参照するfixture image paths。
		const FString SourcePath = "Effects/mesh_surface_lava.tga";
		const FString MaskPath = "Effects/mesh_surface_barrier.tga";
		const FString NoisePath = "Effects/uv_distortion_flow.tga";
		const FString WhitePath = "Effects/material_white.tga";
		FMeshMaterialEditorFixtureFiles FixtureFiles{ Assets };
		FixtureFiles.Paths.Add( FixtureRoot / SourcePath );
		FixtureFiles.Paths.Add( FixtureRoot / MaskPath );
		FixtureFiles.Paths.Add( FixtureRoot / NoisePath );
		FixtureFiles.Paths.Add( FixtureRoot / WhitePath );
		const bool bImagesSaved = NFileSystem::EnsureDirectory( FixtureRoot / "Effects" ) && NAssetTest::WriteFileBytes( FixtureRoot / SourcePath, MakeMeshMaterialEditorTga() ) && NAssetTest::WriteFileBytes( FixtureRoot / MaskPath, MakeMeshMaterialEditorTga() ) && NAssetTest::WriteFileBytes( FixtureRoot / NoisePath, MakeMeshMaterialEditorTga() ) && NAssetTest::WriteFileBytes( FixtureRoot / WhitePath, MakeMeshMaterialEditorTga() );
		C.Check( bImagesSaved, "mesh material editor cache用のsource・mask・noise・白画像をfixtureへ作成する" );
		if ( !bImagesSaved ) return;
		// 保存先として使う、既存でないtest専用world path。
		const FString CopyPath = "Worlds/selftest_mesh_material_effect.world";
		const bool bCopyPathAvailable = !Assets.ResolvePath( CopyPath ).IsSet();
		C.Check( bCopyPathAvailable, "SaveAs用world pathが空いていて既存fileを上書きしない" );
		if ( !bCopyPathAvailable ) return;
		// material slotを持つ編集用world。
		FWorldDefinition World;
		World.Name = "Mesh material effect editor self-test";
		World.NextId = 3;
		// mesh部品とmaterial slotを持つfixture object。
		FWorldObject Object;
		Object.Id = 1;
		Object.Name = "Effect target";
		Object.Mesh = NEditMesh::MakePlane( 2.0f, 2.0f );
		FEditMeshMaterial Material;
		Material.Name = "Base slot";
		Object.Mesh.GetValue().Materials.Add( Material );
		// 別slotの履歴識別を確認する二つ目の材質。
		FEditMeshMaterial OverlayMaterial;
		OverlayMaterial.Name = "Overlay slot";
		Object.Mesh.GetValue().Materials.Add( OverlayMaterial );
		World.Objects.Add( Object );
		// 同じslot番号でも別objectの履歴キーが独立するfixture。
		FWorldObject OtherObject = Object;
		OtherObject.Id = 2;
		OtherObject.Name = "Second effect target";
		World.Objects.Add( OtherObject );
		// 実UIの履歴を受けるeditor state。
		FWorldEditorState State;
		State.Session.Reset( World, FString() );
		State.Session.Document.MarkSaved();
		State.AssetInfo = MakeUnique<FAssetEditorInfo>( Assets );
		// preset buttonを押す前にImGui navigationを初期化する。
		C.Check( Input.Frame( State ), "material effect panelを初回描画する" );
		C.Check( Input.TabTo( State, Input.ItemId( "Flowing lava" ) ), "Flowing lava presetへkeyboard focusする" );
		Input.Tap( State, ImGuiKey_Enter );
		const FWorldObject* EditedObject = State.Session.Document.Get().FindObject( 1 );
		const bool bHasEditedSlot = EditedObject != nullptr && EditedObject->Mesh.IsSet() && EditedObject->Mesh.GetValue().Materials.IsValidIndex( 0 );
		C.Check( bHasEditedSlot, "preset後も編集対象material slotが存在する" );
		if ( !bHasEditedSlot ) return;
		const FEditMeshMaterial& EditedMaterial = EditedObject->Mesh.GetValue().Materials[0];
		C.Check( EditedMaterial.Texture == "Effects/mesh_surface_lava.tga" && EditedMaterial.Effect.Material.bEnabled && EditedMaterial.Effect.Blend == EDrawBlend::Add && EditedMaterial.Effect.Material.Distortion.bEnabled, "Flowing lava presetがsource・発光・歪みを同じslotへ設定する" );
		C.Check( State.Session.Document.IsDirty() && State.Session.Document.GetNumUndo() == 1, "preset適用がdirtyな一つのworld履歴になる" );
		C.Check( State.Session.Document.Undo() && State.Session.Document.Get().FindObject( 1 )->Mesh.GetValue().Materials[0].Effect.IsDefault() && State.Session.Document.Get().FindObject( 1 )->Mesh.GetValue().Materials[0].Texture.IsEmpty(), "preset適用全体を一度でUndoする" );
		C.Check( State.Session.Document.Redo() && State.Session.Document.Get().FindObject( 1 )->Mesh.GetValue().Materials[0].Effect.Material.bEnabled && State.Session.Document.Get().FindObject( 1 )->Mesh.GetValue().Materials[0].Texture == "Effects/mesh_surface_lava.tga", "preset適用をRedoする" );
		// sliderの一時文字入力からproduction component bodyの書戻しを通す。
		const bool bOpacityFocused = Input.TabTo( State, Input.ItemId( "Opacity" ) );
		const bool bOpacityTyped = bOpacityFocused && Input.ReplaceNumeric( State, Input.ItemId( "Opacity" ), "0.73" );
		Input.Tap( State, ImGuiKey_Tab );
		const FWorldObject* NumericEditedObject = State.Session.Document.Get().FindObject( 1 );
		const float32 NumericOpacity = NumericEditedObject != nullptr && NumericEditedObject->Mesh.IsSet() && NumericEditedObject->Mesh.GetValue().Materials.IsValidIndex( 0 ) ? NumericEditedObject->Mesh.GetValue().Materials[0].Effect.Opacity : -1.0f;
		C.Check( bOpacityTyped && std::fabs( NumericOpacity - 0.73f ) < 0.002f && State.Session.Document.IsDirty(), "Opacityを文字入力しworld inspectorから定義へ書き戻す" );
		C.Check( State.Session.Document.Undo() && std::fabs( State.Session.Document.Get().FindObject( 1 )->Mesh.GetValue().Materials[0].Effect.Opacity - 1.0f ) < 0.002f && State.Session.Document.Redo() && std::fabs( State.Session.Document.Get().FindObject( 1 )->Mesh.GetValue().Materials[0].Effect.Opacity - 0.73f ) < 0.002f, "Opacity入力の終了でpreset履歴と分かれUndo/Redoできる" );
		const int32 BeforeSecondSlot = State.Session.Document.GetNumUndo();
		const bool bSecondSlotFocused = Input.TabTo( State, Input.ItemId( "Pulsing barrier", 1 ) );
		if ( bSecondSlotFocused ) Input.Tap( State, ImGuiKey_Enter );
		const FWorldObject* MultiSlotObject = State.Session.Document.Get().FindObject( 1 );
		C.Check( bSecondSlotFocused && MultiSlotObject != nullptr && MultiSlotObject->Mesh.GetValue().Materials[1].Effect.Material.bDissolve && State.Session.Document.GetNumUndo() == BeforeSecondSlot + 1, "二つ目のslotを実UIで変更して別のworld履歴を作る" );
		C.Check( State.Session.Document.Undo() && State.Session.Document.Get().FindObject( 1 )->Mesh.GetValue().Materials[1].Effect.IsDefault() && State.Session.Document.Get().FindObject( 1 )->Mesh.GetValue().Materials[0].Effect.Material.bEnabled, "Undoは対象slotだけを戻す" );
		C.Check( State.Session.Document.Redo(), "二つ目のslot設定をRedoする" );
		const int32 BeforeSecondObject = State.Session.Document.GetNumUndo();
		const bool bSecondObjectFocused = Input.TabTo( State, Input.ItemId( "Pulsing barrier" ), 2 );
		if ( bSecondObjectFocused ) Input.Tap( State, ImGuiKey_Enter, 2 );
		const FWorldObject* SecondObject = State.Session.Document.Get().FindObject( 2 );
		C.Check( bSecondObjectFocused && SecondObject != nullptr && SecondObject->Mesh.GetValue().Materials[0].Effect.Material.bDissolve && State.Session.Document.GetNumUndo() == BeforeSecondObject + 1, "同じslot番号の別objectを実UIで変更して履歴を分ける" );
		C.Check( State.Session.Document.Undo() && State.Session.Document.Get().FindObject( 2 )->Mesh.GetValue().Materials[0].Effect.IsDefault() && State.Session.Document.Get().FindObject( 1 )->Mesh.GetValue().Materials[0].Effect.Material.bEnabled, "Undoは別objectだけを戻す" );
		C.Check( State.Session.Document.Redo(), "別objectの設定をRedoする" );
		const int32 BeforeCurve = State.Session.Document.GetNumUndo();
		const bool bCurveAddFocused = Input.TabTo( State, Input.CurveItemId( "Emission curve", "Add key" ) );
		if ( bCurveAddFocused ) Input.Tap( State, ImGuiKey_Enter );
		const FWorldObject* CurveObject = State.Session.Document.Get().FindObject( 1 );
		const bool bCurveAdded = CurveObject != nullptr && CurveObject->Mesh.GetValue().Materials[0].Effect.Material.Emission.Keys.Num() == 1;
		C.Check( bCurveAddFocused && bCurveAdded && State.Session.Document.GetNumUndo() == BeforeCurve + 1, "実curve UIのAdd keyを一つのworld履歴へ記録する" );
		const int32 BeforeCurveValue = State.Session.Document.GetNumUndo();
		const ImGuiID CurveValueId = Input.CurveItemId( "Emission curve", "Value", 0, 0 );
		const bool bCurveValueFocused = Input.TabTo( State, CurveValueId );
		const bool bCurveValueTyped = bCurveValueFocused && Input.ReplaceNumeric( State, CurveValueId, "2.35" );
		Input.Tap( State, ImGuiKey_Tab );
		CurveObject = State.Session.Document.Get().FindObject( 1 );
		const bool bCurveValueSaved = CurveObject != nullptr && CurveObject->Mesh.GetValue().Materials[0].Effect.Material.Emission.Keys.Num() == 1 && std::fabs( CurveObject->Mesh.GetValue().Materials[0].Effect.Material.Emission.Keys[0].Value - 2.35f ) < 0.002f;
		C.Check( bCurveValueTyped && bCurveValueSaved && State.Session.Document.GetNumUndo() == BeforeCurveValue + 1, "曲線値の文字入力をreleaseで独立した履歴へ確定する" );
		C.Check( State.Session.Document.Undo() && std::fabs( State.Session.Document.Get().FindObject( 1 )->Mesh.GetValue().Materials[0].Effect.Material.Emission.Keys[0].Value - 1.0f ) < 0.002f && State.Session.Document.Redo() && std::fabs( State.Session.Document.Get().FindObject( 1 )->Mesh.GetValue().Materials[0].Effect.Material.Emission.Keys[0].Value - 2.35f ) < 0.002f, "曲線値のrelease履歴をUndo/Redoする" );
		// 世界asset managerがSaveWorldFileから使われる実装依存先と一致する。
		C.Check( GetAppSubsystem<CAssetManager>() == &Assets, "SaveAs対象とtest asset managerが同じ" );
		if ( GetAppSubsystem<CAssetManager>() != &Assets ) return;
		// 未保存world copyをasset manager経由で別名保存する。
		const bool bSaved = NDebugUI::SaveWorldFile( State, CopyPath );
		C.Check( bSaved && State.Session.FilePath == CopyPath && !State.Session.Document.IsDirty(), "SaveAsでFilePathと保存済み状態を更新する" );
		if ( !bSaved ) return;
		// 保存したworldを新しいassetとして読み直す。
		TSharedPtr<CWorldAsset> Fresh = Assets.LoadWorld( CopyPath );
		const FWorldObject* FreshObject = Fresh.IsValid() ? Fresh->GetDefinition().FindObject( 1 ) : nullptr;
		const bool bFreshSlotExists = FreshObject != nullptr && FreshObject->Mesh.IsSet() && FreshObject->Mesh.GetValue().Materials.IsValidIndex( 0 );
		C.Check( bFreshSlotExists, "SaveAs worldをfresh loadするとmaterial slotが残る" );
		if ( bFreshSlotExists )
		{
			const FEditMeshMaterial& FreshMaterial = FreshObject->Mesh.GetValue().Materials[0];
			const FEditMeshMaterial& SavedMaterial = State.Session.Document.Get().FindObject( 1 )->Mesh.GetValue().Materials[0];
			C.Check( FreshMaterial.Effect.ComputeHash() == SavedMaterial.Effect.ComputeHash() && FreshMaterial.Effect.ToJson() == SavedMaterial.Effect.ToJson() && FreshMaterial.Texture == SavedMaterial.Texture, "SaveAsからfresh loadしたEffect設定とsourceが完全一致する" );
			C.Check( FreshObject->Mesh.GetValue().Materials[1].Effect.ToJson() == State.Session.Document.Get().FindObject( 1 )->Mesh.GetValue().Materials[1].Effect.ToJson() && Fresh->GetDefinition().FindObject( 2 )->Mesh.GetValue().Materials[0].Effect.ToJson() == State.Session.Document.Get().FindObject( 2 )->Mesh.GetValue().Materials[0].Effect.ToJson(), "別slotと別objectの設定も同じworldへ保存する" );
			// fresh loaded world objectが実MeshView cacheを作る境界を確認する。
			C.Group = "world-mesh-material-editor-mesh-view-cache";
			FDrawBuffer FirstBuffer;
			FDrawContext FirstDraw( FirstBuffer );
			const FTransform Placed;
			const bool bRecorded = NWorldEditorUI::RecordMeshObject( State, *FreshObject, Placed, FirstDraw, 1 );
			FWorldEditorState::FMeshView* FirstView = State.MeshViews.Find( 1 );
			C.Check( bRecorded && FirstView != nullptr && FirstView->bBuilt && FirstView->Renderer.IsValid() && CountMeshMaterialEditorCommands( FirstBuffer, EDrawCommand::MaterialMesh3D ) > 0, "fresh loadしたmeshを実world editor MeshViewから材質mesh commandへ記録する" );
			if ( FirstView != nullptr && FirstView->Renderer.IsValid() )
			{
				NWorldEditorUI::UpdateMeshMaterialPreviews( State, 0.25f );
				const double FirstTime = FirstView->Renderer.GetEffectTime();
				FDrawBuffer StableBufferA;
				FDrawContext StableDrawA( StableBufferA );
				const bool bStableRecordA = NWorldEditorUI::RecordMeshObject( State, *FreshObject, Placed, StableDrawA, 1 );
				FDrawBuffer StableBufferB;
				FDrawContext StableDrawB( StableBufferB );
				const bool bStableRecordB = NWorldEditorUI::RecordMeshObject( State, *FreshObject, Placed, StableDrawB, 1 );
				C.Check( std::fabs( FirstTime - 0.25 ) < 1.0e-6 && std::fabs( FirstView->Renderer.GetEffectTime() - FirstTime ) < 1.0e-6 && bStableRecordA && bStableRecordB && StableBufferA.Words == StableBufferB.Words, "preview時計を一度進めた後のRecord反復は時計とdraw wordsを変えない" );
				// mesh definitionのhashが変わると同じownerのrendererを作り直す。
				FWorldObject ChangedObject = *FreshObject;
				ChangedObject.Mesh.GetValue().Materials[0].Effect.Opacity = 0.5f;
				FDrawBuffer ChangedBuffer;
				FDrawContext ChangedDraw( ChangedBuffer );
				NWorldEditorUI::RecordMeshObject( State, ChangedObject, Placed, ChangedDraw, 1 );
				FirstView = State.MeshViews.Find( 1 );
				C.Check( FirstView != nullptr && FirstView->Hash == ChangedObject.Mesh.GetValue().ComputeHash() && std::fabs( FirstView->Renderer.GetEffectTime() ) < 1.0e-6, "mesh definition hash変更はMeshView rendererを先頭時刻で再構築する" );
				// 複製とprefab instanceのsynthetic keyに別のrenderer ownerを割り当てる。
				FWorldObject DuplicateObject = ChangedObject;
				DuplicateObject.Id = 2;
				FDrawBuffer DuplicateBuffer;
				FDrawContext DuplicateDraw( DuplicateBuffer );
				const bool bDuplicateRecorded = NWorldEditorUI::RecordMeshObject( State, DuplicateObject, Placed, DuplicateDraw, DuplicateObject.Id );
				FWorldObject PrefabObject = DuplicateObject;
				FDrawBuffer PrefabBuffer;
				FDrawContext PrefabDraw( PrefabBuffer );
				const bool bPrefabRecorded = NWorldEditorUI::RecordMeshObject( State, PrefabObject, Placed, PrefabDraw, 7001 );
				NWorldEditorUI::UpdateMeshMaterialPreviews( State, 0.25f );
				FWorldEditorState::FMeshView* DuplicateView = State.MeshViews.Find( 2 );
				FWorldEditorState::FMeshView* PrefabView = State.MeshViews.Find( 7001 );
				if ( DuplicateView != nullptr ) DuplicateView->Renderer.PauseEffects();
				NWorldEditorUI::UpdateMeshMaterialPreviews( State, 0.1f );
				FirstView = State.MeshViews.Find( 1 );
				C.Check( bDuplicateRecorded && bPrefabRecorded && FirstView != nullptr && DuplicateView != nullptr && PrefabView != nullptr && FirstView->Renderer.GetEffectTime() > DuplicateView->Renderer.GetEffectTime() && DuplicateView->Renderer.IsEffectsPaused() && PrefabView->Renderer.GetEffectTime() > 0.25, "duplicateとsynthetic prefab keyは個別clock ownerとなりPauseも分離する" );
				TSet<int32> VisibleObjects;
				VisibleObjects.Add( 1 );
				VisibleObjects.Add( 7001 );
				NWorldEditorUI::PruneMeshViews( State, VisibleObjects );
				C.Check( State.MeshViews.Contains( 1 ) && State.MeshViews.Contains( 7001 ) && !State.MeshViews.Contains( 2 ), "Pruneは見えないduplicate ownerを捨てsynthetic prefab ownerを保つ" );
				VisibleObjects.Remove( 7001 );
				NWorldEditorUI::PruneMeshViews( State, VisibleObjects );
				C.Check( State.MeshViews.Contains( 1 ) && !State.MeshViews.Contains( 7001 ), "synthetic prefab keyもSeenから外れた時にpruneする" );
				VisibleObjects.Empty();
				NWorldEditorUI::PruneMeshViews( State, VisibleObjects );
				C.Check( State.MeshViews.IsEmpty(), "削除されたmesh objectのMeshView ownerをpruneして解放する" );
			}
			State.AssetInfo->Clear();
			Assets.ReleaseUnused();
			Assets.Tick( 0.0f );
		}
		// 自分が作ったtest worldだけを実pathから片付ける。
		const TOptional<FString> CopyFullPath = Assets.ResolvePath( CopyPath );
		C.Check( CopyFullPath.IsSet() && NFileSystem::RemoveFile( CopyFullPath.GetValue() ), "test専用world copyを削除する" );
		C.Check( Input.FramesBalanced(), "全ImGui frameのBegin/EndとNewFrame/Renderが対応する" );
	}

	// 複製rendererがslot定義を共有しても再生時計とtransportは別々に保つ。
	void TestIndependentMeshEffectClock( NAssetTest::FChecker& C )
	{
		C.Group = "world-mesh-material-editor-independent-clock";
		// renderer複製間で共有する不変設定。
		TSharedPtr<FMeshMaterialEffectResources> SharedResources = MakeShared<FMeshMaterialEffectResources>();
		SharedResources->Definition.Material.bEnabled = true;
		SharedResources->Definition.Duration = 0.5f;
		SharedResources->Definition.bLoop = false;
		// 1つのrendererへ設定を共有するeffect batchを作る。
		FMeshDrawBatch FirstBatch;
		FirstBatch.bEffectRequested = true;
		FirstBatch.EffectResources = SharedResources;
		// 二つ目のrendererへ設定だけを複製したeffect batch。
		FMeshDrawBatch SecondBatch = FirstBatch;
		// 同じslot定義を使う個体別renderer。
		FMeshRenderer FirstRenderer;
		FMeshRenderer SecondRenderer;
		FirstRenderer.Batches.Add( FirstBatch );
		SecondRenderer.Batches.Add( SecondBatch );
		FirstRenderer.PlayEffects();
		FirstRenderer.Update( 0.25f );
		SecondRenderer.PlayEffects();
		SecondRenderer.Update( 0.12f );
		C.Check( std::fabs( FirstRenderer.GetEffectTime() - 0.25 ) < 1.0e-6 && std::fabs( SecondRenderer.GetEffectTime() - 0.12 ) < 1.0e-6, "複製rendererは共有definitionから独立したslot時刻を持つ" );
		FirstRenderer.PauseEffects();
		FirstRenderer.Update( 0.1f );
		C.Check( FirstRenderer.IsEffectsPaused() && std::fabs( FirstRenderer.GetEffectTime() - 0.25 ) < 1.0e-6, "Pause中はmesh effectの表示時刻を保持する" );
		FirstRenderer.PlayEffects();
		FirstRenderer.Update( 0.3f );
		C.Check( FirstRenderer.IsEffectsFinished() && std::fabs( FirstRenderer.GetEffectTime() - 0.5 ) < 1.0e-6, "非loop effectは終端表示を保持してFinishedになる" );
		FirstRenderer.PlayEffects();
		C.Check( FirstRenderer.IsEffectsPlaying() && std::fabs( FirstRenderer.GetEffectTime() ) < 1.0e-6 && std::fabs( SecondRenderer.GetEffectTime() - 0.12 ) < 1.0e-6, "FinishedからのPlayは先頭へ戻り他rendererを変えない" );
		FirstRenderer.StopEffects();
		C.Check( !FirstRenderer.IsEffectsPlaying() && !FirstRenderer.IsEffectsFinished() && FirstRenderer.GetEffectTime() == 0.0, "Stopはeffect clockを0へ戻してlegacy表示へ戻す" );
	}
}

namespace NWorldTest
{
	// 実world editorのmesh材質効果入力と保存後の利用定義を確認する。
	void RunWorldMeshMaterialEditorTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& TestDirectory )
	{
		C.Group = "world-mesh-material-editor-input-context";
		// 呼出側が使用していたImGui文脈。
		ImGuiContext* Previous = ImGui::GetCurrentContext();
		{
			// editor panelを別文脈で操作する入力器。
			FMeshMaterialInputContext Input;
			C.Check( Input.IsReady(), "mesh material editor用font atlasを構築する" );
			if ( Input.IsReady() ) TestMeshMaterialEditorSave( C, Assets, Input, TestDirectory );
			C.Check( Input.FramesBalanced(), "mesh material入力確認のframeが対応する" );
		}
		TestIndependentMeshEffectClock( C );
		C.Check( ImGui::GetCurrentContext() == Previous, "mesh material入力確認後に元のImGui文脈へ戻る" );
	}
}
