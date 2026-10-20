// SPDX-License-Identifier: NOASSERTION
#include "WorldModelMaterialSelfTest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "AssetSelfTestModelSurface.h"
#include "AssetSelfTestSupport.h"
#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Interfaces/Render/DrawModelMaterial.h"
#include "Library/Framework/Animation/ModelAnimator.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/StaticModelSurface.h"
#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Ecs/System.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/World/WorldDefinition.h"
#include "Library/Framework/World/WorldEdit.h"
#include "Library/Framework/World/WorldModelMaterialEffect.h"
#include "Library/Framework/World/WorldSpawner.h"
#include "Library/Framework/World/WorldValidate.h"
#include "Library/Platform/FileSystem.h"
#include "Library/Platform/DxLib/DxLibAssetBackend.h"

namespace NWorldTest
{
	namespace
	{
		// 保存・spawn試験に使う、単一材質の有効なModel効果。
		FWorldModelMaterialEffect MakeWorldModelMaterialBinding( int32 MaterialIndex, bool bEnabled = true )
		{
			FWorldModelMaterialEffect Binding; // ChildTriangleへ適用するbinding値
			Binding.MaterialIndex = MaterialIndex;
			Binding.Effect.Material.bEnabled = bEnabled;
			Binding.Effect.Duration = 2.0f;
			Binding.Effect.bLoop = true;
			Binding.Effect.Opacity = 0.5f;
			Binding.Effect.Blend = EDrawBlend::Add;
			return Binding; // ChildTriangleへ適用するbinding値
		}

		// Model material commandの個数をword列から数える。
		int32 CountWorldModelMaterialCommands( const FDrawBuffer& Buffer, EDrawCommand Type )
		{
			int32 Count = 0; // 指定command種別を数える途中値
			if ( !Buffer.Words.empty() ) ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == Type ) ++Count; } );
			return Count; // 指定command種別を数える途中値
		}

		// Model animatorの記録結果を単独bufferへ作る。
		FDrawBuffer RecordWorldModelAnimator( const FModelAnimator& Animator )
		{
			FDrawBuffer Buffer; // 一個体の描画記録を受ける出力buffer
			FDrawContext Draw( Buffer );
			Animator.Record( Draw, FMatrix4x4::Identity );
			return Buffer; // 一個体の描画記録を受ける出力buffer
		}

		// fake executorは命令を保存せず、model material対応だけを切り替える。
		class FWorldModelMaterialTestExecutor final : public IDrawExecutor
		{
		public:
			// 対応・未対応を切り替え、準備前後のfallbackを再現する値。
			bool bSupportsEffects = true;

			// fake testではnative描画を実行しない。
			void Execute( const FDrawSpan& Span ) override { (void)Span; }
			// model material effect commandの対応可否を返す。
			bool SupportsModelMaterialEffects() const override { return bSupportsEffects; }
		};

		// query記録でValidateが参照したasset用途を特定する。
		class FWorldModelMaterialAssetQuery final : public IWorldAssetQuery
		{
		public:
			// アセット確認要求を呼出順に残す一覧。
			mutable std::vector<FString> QueriedPaths;

			// 全pathを未存在として返し、optional resource検査を数える。
			bool Exists( const FString& Path ) const override { QueriedPaths.push_back( Path ); return Path == "model.mv1"; }
		};

		// fake backendへ2材質triangleと借用diffuse画像を渡す値を作る。
		FStaticModelSurfaceData MakeWorldFakeSurface( int32 DiffuseHandle )
		{
			FStaticModelSurfaceData Surface; // ChildTriangleと材質sectionの不変snapshot
			for ( int32 Triangle = 0; Triangle < 2; ++Triangle )	for ( int32 Corner = 0; Corner < 3; ++Corner )
			{
				FMeshVertex Vertex; // index列へ追加するfake頂点
				Vertex.Position[0] = static_cast<float32>( Triangle * 3 + Corner );
				Vertex.Position[1] = static_cast<float32>( Corner == 1 ? 1 : 0 );
				Vertex.Position[2] = 0.0f;
				Vertex.Normal[2] = 1.0f;
				Vertex.U = Corner == 2 ? 1.0f : 0.0f;
				Vertex.V = Corner == 1 ? 1.0f : 0.0f;
				Surface.Vertices.Add( Vertex );
				Surface.Indices.Add( static_cast<uint32>( Triangle * 3 + Corner ) );
			}
			for ( int32 MaterialIndex = 0; MaterialIndex < 2; ++MaterialIndex )
			{
				FStaticModelSurfaceSection Section; // surface snapshotの材質section
				Section.FirstIndex = MaterialIndex * 3;
				Section.NumIndices = 3;
				Section.MaterialIndex = MaterialIndex;
				Section.SourceFrameIndex = 0;
				Section.SourceMeshIndex = MaterialIndex;
				Surface.Sections.Add( Section );
				FStaticModelSurfaceMaterial Material; // sectionが参照する材質値
				Material.BorrowedDiffuseTexture = DiffuseHandle;
				Surface.Materials.Add( Material );
			}
			return Surface; // ChildTriangleと材質sectionの不変snapshot
		}

		// world objectへmodelとbindingを一緒に設定する。
		FWorldObject MakeWorldModelObject( int32 Id, const char* Name, const FString& ModelPath, const FWorldModelMaterialEffect* Binding )
		{
			FWorldObject Object; // model部品を持つworld object
			Object.Id = Id;
			Object.Name = Name;
			FWorldModel Model; // world objectのmodel設定
			Model.Path = ModelPath;
			if ( Binding != nullptr ) Model.MaterialEffect = *Binding;
			Object.Model = Model;
			return Object; // model部品を持つworld object
		}

	}

	void RunWorldModelMaterialDefinitionTests( NAssetTest::FChecker& Checker )
	{
		const char* OldGroup = Checker.Group; // 呼出元が設定していたcheck group
		Checker.Group = "world-model-material-definition";

		// 旧形式を読み書きしたときにMaterialEffect項目が増えない。
		FWorldDefinition LegacyWorld; // binding未設定の旧形式world
		LegacyWorld.Name = "Legacy";
		LegacyWorld.Objects.Add( MakeWorldModelObject( 1, "legacy-model", "model.mv1", nullptr ) );
		const std::string LegacyJson = LegacyWorld.ToJson().dump(); // 旧形式worldの保存文字列
		FWorldDefinition LegacyReloaded; // 旧world JSONを再読した値
		const bool bLegacyParsed = FWorldDefinition::FromJson( NlohmannJson::parse( LegacyJson ), LegacyReloaded ); // 旧形式JSONの読込結果
		Checker.Check( bLegacyParsed && LegacyReloaded.ToJson().dump() == LegacyJson && !LegacyReloaded.Objects[0].Model.GetValue().MaterialEffect.IsSet(), "未設定bindingは旧Model JSONをbyte同値で保ち、再読でも省略される" );

		// floatとcurveを既定以外にし全保存fieldのroundtripを確認する。
		FWorldModelMaterialEffect Complete = MakeWorldModelMaterialBinding( 7 ); // roundtrip対象の全field設定
		Complete.bTwoSided = true;
		Complete.Effect.Duration = 3.125f;
		Complete.Effect.bLoop = false;
		Complete.Effect.Opacity = 0.375f;
		Complete.Effect.Material.bDissolve = true;
		Complete.Effect.Material.MaskTexture = "Masks/model-mask.tga";
		Complete.Effect.Material.UVScale = FVector2( 1.25f, 0.75f );
		Complete.Effect.Material.UVScroll = FVector2( -0.125f, 0.0625f );
		Complete.Effect.Material.Emission.Keys[0].Value = 0.375f;
		Complete.Effect.Material.Emission.Keys[1].Value = 1.875f;
		Complete.Effect.Material.Distortion.bEnabled = true;
		Complete.Effect.Material.Distortion.Texture = "Noise/model-noise.tga";
		Complete.Effect.Material.Distortion.Strength.Keys[0].Value = 0.25f;
		Complete.Effect.Material.Distortion.Strength.Keys[1].Value = 0.75f;
		const NlohmannJson CompleteJson = Complete.ToJson(); // 完全bindingの保存JSON
		FWorldModelMaterialEffect CompleteReloaded; // 完全bindingを再読した値
		const bool bCompleteParsed = FWorldModelMaterialEffect::FromJson( CompleteJson, CompleteReloaded ); // 完全binding JSONの読込結果
		Checker.Check( bCompleteParsed && Complete.IsSameAs( CompleteReloaded ) && Complete.ComputeHash() == CompleteReloaded.ComputeHash() && CompleteJson == CompleteReloaded.ToJson(), "設定済みbindingは材質index・TwoSided・全効果curveとfloatを精度保持して往復する" );
		const FWorldModelMaterialEffect DefaultBinding; // 既定無効bindingのpositive control
		Checker.Check( !DefaultBinding.Effect.Material.bEnabled && DefaultBinding.IsValidSettings() && DefaultBinding.ToJson().is_object(), "既定bindingは無効のまま有効な保存値として扱う" );
		NlohmannJson NullBindingWorldJson = LegacyWorld.ToJson(); // null bindingを埋め込むworld JSON
		NullBindingWorldJson["Objects"][0]["Components"]["Model"]["MaterialEffect"] = nullptr;
		FWorldDefinition NullBindingWorld; // null binding markerの読込先world
		const bool bNullWorldParsed = FWorldDefinition::FromJson( NullBindingWorldJson, NullBindingWorld ); // null binding worldの読込結果
		const NlohmannJson NullBindingSaved = NullBindingWorld.ToJson(); // null markerを含む再保存JSON
		FWorldDefinition NullBindingReread; // null markerをもう一度読んだworld
		const bool bNullWorldReread = FWorldDefinition::FromJson( NullBindingSaved, NullBindingReread ); // null marker再読結果
		Checker.Check( bNullWorldParsed && bNullWorldReread && NullBindingReread.Objects[0].Model.GetValue().MaterialEffect.IsSet() && !NullBindingReread.Objects[0].Model.GetValue().MaterialEffect.GetValue().IsValidSettings() && NullBindingSaved["Objects"][0]["Components"]["Model"]["MaterialEffect"].is_null(), "null bindingは未設定へ消えずinvalid状態を保存再読で維持する" );

		// disabled bindingも手編集値を残し、未設定へ丸めない。
		FWorldModelMaterialEffect Disabled = Complete; // 編集値を残した無効binding
		Disabled.Effect.Material.bEnabled = false;
		const NlohmannJson DisabledJson = Disabled.ToJson(); // 無効bindingの保存JSON
		FWorldModelMaterialEffect DisabledReloaded; // 無効bindingを再読した値
		const bool bDisabledParsed = FWorldModelMaterialEffect::FromJson( DisabledJson, DisabledReloaded ); // 無効binding JSONの読込結果
		Checker.Check( bDisabledParsed && DisabledReloaded.IsSameAs( Disabled ) && !DisabledReloaded.Effect.Material.bEnabled && DisabledReloaded.Effect.Material.MaskTexture == "Masks/model-mask.tga", "無効化しても保存済みのbinding値を保持する" );

		// markerが再保存・再読後にも不正状態と元JSONを区別して残す。
		const auto CheckInvalidRoundTrip = [&]( const NlohmannJson& InvalidJson, const char* CaseName )
		{
			FWorldModelMaterialEffect Parsed; // 型不正JSONを読む一時binding
			FWorldModelMaterialEffect Reread; // marker保存後に読む一時binding
			const bool bParsed = FWorldModelMaterialEffect::FromJson( InvalidJson, Parsed ); // 型不正入力の初回読込結果
			const NlohmannJson SavedJson = Parsed.ToJson(); // marker付きbindingの保存JSON
			const bool bReread = FWorldModelMaterialEffect::FromJson( SavedJson, Reread ); // marker付きJSONの再読結果
			Checker.Check( bParsed && bReread && !Parsed.IsValidSettings() && !Reread.IsValidSettings() && Parsed.IsSameAs( Reread ) && SavedJson == Reread.ToJson(), "型不正はmarkerを保存再読後も保持する: %s", CaseName );
		};
		const uint64 TooLargeMaterialIndex = static_cast<uint64>( ( std::numeric_limits<int32>::max )() ) + 1ull; // int32最大値を超えるunsigned入力
		const int64 UnsignedWrapCandidate = 4294967296ll; // int32へwrapし得るsigned 2^32入力
		const int64 UnsignedWrapCandidatePlusTwo = 4294967298ll; // wrap後の小値が2となるsigned入力
		const uint64 UnsignedWrapCandidateUnsigned = 4294967296ull; // wrapし得るunsigned 2^32入力
		const uint64 UnsignedWrapCandidatePlusTwoUnsigned = 4294967298ull; // wrap後の小値が2となるunsigned入力
		for ( const NlohmannJson& BadIndex : { NlohmannJson( TooLargeMaterialIndex ), NlohmannJson( UnsignedWrapCandidate ), NlohmannJson( UnsignedWrapCandidatePlusTwo ), NlohmannJson( UnsignedWrapCandidateUnsigned ), NlohmannJson( UnsignedWrapCandidatePlusTwoUnsigned ), NlohmannJson( -1 ), NlohmannJson( 1.0 ), NlohmannJson( "1" ), NlohmannJson( true ), NlohmannJson( nullptr ) } )
		{
			NlohmannJson Invalid = CompleteJson; // 不正markerを作るためのJSON写し
			Invalid["MaterialIndex"] = BadIndex;
			CheckInvalidRoundTrip( Invalid, "MaterialIndex" );
		}
		for ( const NlohmannJson& BadEffect : { NlohmannJson( nullptr ), NlohmannJson( "effect" ), NlohmannJson::array() } )
		{
			NlohmannJson Invalid = CompleteJson; // 不正markerを作るためのJSON写し
			Invalid["Effect"] = BadEffect;
			CheckInvalidRoundTrip( Invalid, "Effect" );
		}
		for ( const NlohmannJson& BadTwoSided : { NlohmannJson( 1 ), NlohmannJson( "true" ), NlohmannJson( nullptr ) } )
		{
			NlohmannJson Invalid = CompleteJson; // 不正markerを作るためのJSON写し
			Invalid["TwoSided"] = BadTwoSided;
			CheckInvalidRoundTrip( Invalid, "TwoSided" );
		}
		for ( const NlohmannJson& BadRoot : { NlohmannJson( nullptr ), NlohmannJson::array(), NlohmannJson( "binding" ), NlohmannJson( false ) } ) CheckInvalidRoundTrip( BadRoot, "root" );
		NlohmannJson InvalidEffectRoot = CompleteJson; // Effect root型の不正JSON
		InvalidEffectRoot["Effect"] = 4;
		CheckInvalidRoundTrip( InvalidEffectRoot, "Effect root type" );

		// int32境界値は変換で欠落せず、負のsemantic値だけValidateが拒否する。
		for ( const int32 BoundaryIndex : { ( std::numeric_limits<int32>::min )(), ( std::numeric_limits<int32>::max )() } )
		{
			FWorldModelMaterialEffect Boundary = Complete; // int32端を試すbinding値
			Boundary.MaterialIndex = BoundaryIndex;
			FWorldModelMaterialEffect BoundaryReloaded; // 境界値を読み直したbinding
			const bool bBoundaryParsed = FWorldModelMaterialEffect::FromJson( Boundary.ToJson(), BoundaryReloaded ); // 境界値JSONの読込結果
			Checker.Check( bBoundaryParsed && Boundary.IsSameAs( BoundaryReloaded ) && ( BoundaryIndex >= 0 || !BoundaryReloaded.IsValidSettings() ), "int32境界を保持し負値だけ設定検証で拒否する: %d", BoundaryIndex );
		}

		// 不正設定はdisabledでもBadModelになり、使わない画像はqueryしない。
		FWorldDefinition InvalidDisabledWorld; // disabledでも不正値を検証するworld
		FWorldModelMaterialEffect InvalidDisabled = Disabled; // 負indexを持つ無効binding
		InvalidDisabled.MaterialIndex = -1;
		InvalidDisabled.Effect.Material.MaskTexture = "missing-mask.tga";
		InvalidDisabled.Effect.Material.Distortion.Texture = "missing-noise.tga";
		FWorldObject InvalidObject = MakeWorldModelObject( 17, "invalid-disabled", "model.mv1", &InvalidDisabled ); // binding不正object
		InvalidDisabledWorld.Objects.Add( InvalidObject );
		FWorldModelMaterialAssetQuery DisabledQuery; // disabled時のasset query記録先
		const TArray<FWorldIssue> DisabledIssues = NWorld::ValidateWorld( InvalidDisabledWorld, &DisabledQuery ); // disabled bindingのvalidation結果
		bool bBadModelForCorrectObject = false; // 正しいobject idのBadModel有無
		for ( const FWorldIssue& Issue : DisabledIssues ) bBadModelForCorrectObject = bBadModelForCorrectObject || ( Issue.Code == EWorldIssueCode::BadModel && Issue.ObjectId == 17 );
		Checker.Check( bBadModelForCorrectObject && DisabledQuery.QueriedPaths.size() == 1 && DisabledQuery.QueriedPaths[0] == "model.mv1", "disabledでも不正bindingを正しいObjectIdのBadModelで報告し、mask/noiseは問い合わせない" );

		// 有効時だけ消失maskと正強度noiseをoptional assetとして調べる。
		FWorldDefinition EnabledWorld; // 使用画像の検査用world
		FWorldModelMaterialEffect Enabled = Complete; // maskとnoiseを使う有効binding
		Enabled.Effect.Material.MaskTexture = "missing-mask.tga";
		Enabled.Effect.Material.Distortion.Texture = "missing-noise.tga";
		Enabled.Effect.Material.Distortion.Strength.Keys[0].Value = 0.25f;
		FWorldObject EnabledObject = MakeWorldModelObject( 23, "enabled", "model.mv1", &Enabled ); // 有効bindingを持つobject値
		EnabledWorld.Objects.Add( EnabledObject );
		FWorldModelMaterialAssetQuery EnabledQuery; // enabled時のasset query記録先
		const TArray<FWorldIssue> EnabledIssues = NWorld::ValidateWorld( EnabledWorld, &EnabledQuery ); // enabled bindingのvalidation結果
		int32 MissingOptionalAssets = 0; // 使用するoptional画像のMissingAsset数
		for ( const FWorldIssue& Issue : EnabledIssues ) if ( Issue.Code == EWorldIssueCode::MissingAsset && Issue.ObjectId == 23 ) ++MissingOptionalAssets;
		Checker.Check( MissingOptionalAssets == 2 && EnabledQuery.QueriedPaths.size() == 3 && EnabledQuery.QueriedPaths[0] == "model.mv1" && EnabledQuery.QueriedPaths[1] == "missing-mask.tga" && EnabledQuery.QueriedPaths[2] == "missing-noise.tga", "有効なDissolveと正noise強度だけmask/noiseのasset queryを追加する" );

		// 履歴snapshotはbinding全体を一編集として戻し、やり直す。
		TWorldDocument Document; // world edit履歴の保存先
		Document.Reset( LegacyWorld );
		FWorldObject* Editable = NWorldEdit::EditObject( Document, 1 ); // 履歴へ積んで編集するobject
		if ( Editable != nullptr ) Editable->Model.GetValue().MaterialEffect = Complete;
		const bool bEditSavedBinding = Editable != nullptr && Document.IsDirty() && Document.Get().Objects[0].Model.GetValue().MaterialEffect.IsSet(); // 編集でbindingとdirty状態が保存されたか
		const bool bUndoRestoresLegacy = Document.Undo() && !Document.Get().Objects[0].Model.GetValue().MaterialEffect.IsSet(); // undoで旧未設定へ戻ったか
		const bool bRedoRestoresBinding = Document.Redo() && Document.Get().Objects[0].Model.GetValue().MaterialEffect.IsSet() && Document.Get().Objects[0].Model.GetValue().MaterialEffect.GetValue().IsSameAs( Complete ); // redoで全binding値が戻ったか
		Checker.Check( bEditSavedBinding && bUndoRestoresLegacy && bRedoRestoresBinding, "TWorldDocumentとEditObjectのundo/redoでbinding設定全体を保持する" );

		Checker.Group = OldGroup;
	}

	void RunWorldModelMaterialSpawnFakeChecks( NAssetTest::FChecker& Checker, CAssetManager& Assets, NAssetTest::FFakeAssetBackend& Fake, const FString& Directory )
	{
		const char* OldGroup = Checker.Group; // 呼出元が設定していたcheck group
		Checker.Group = "world-model-material-spawn-fake";
		const bool bOldSurfaceSupport = Fake.bSupportStaticModelSurface; // 呼出元のsurface対応設定
		const FStaticModelSurfaceData OldSurface = Fake.StaticModelSurfaceData; // 呼出元のsurface fixture
		const NAssetTest::FFakeAssetBackend::FModelData OldModelData = Fake.NextModelData; // 呼出元のfake model情報
		const bool bOldGeometrySupport = Fake.bSupportModelMaterialGeometry; // 呼出元のgeometry対応設定
		const int32 OldGeometryCreateCall = Fake.FailModelMaterialGeometryCreateCall; // 呼出元のgeometry失敗注入番号
		const int32 OldQueries = Fake.NumStaticModelSurfaceQueries; // fake surface queryの開始数
		const int32 OldGeometryCreates = Fake.NumModelMaterialGeometryCreates; // fake geometry作成の開始数
		const size_t OldDeletedGeometryCount = Fake.DeletedModelMaterialGeometries.size(); // fake geometry解放履歴の開始位置
		const char* ModelPath = "world_model_material_fake.mv1"; // fake worldで共有するmodel相対path
		const FString ModelFile = Directory / ModelPath; // fake model fileの出力先
		const FString DiffuseFile = Directory / "world_model_material_fake.tga"; // fake borrowed diffuse画像の出力先
		const std::vector<uint8> TinyTga = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 24, 0, 0, 255, 0 }; // 1pixel diffuse fixtureのbyte列
		const bool bFixtureWritten = WriteFileText( ModelFile, "fake model" ) && WriteFileBytes( DiffuseFile, TinyTga ); // real .xと画像fixtureの書込結果
		Assets.SetRootDirectory( Directory );
		TSharedPtr<CTexture> Diffuse = bFixtureWritten ? Assets.LoadTexture( "world_model_material_fake.tga" ) : TSharedPtr<CTexture>(); // fake model materialへ貸す画像asset
		Fake.bSupportStaticModelSurface = true;
		Fake.bSupportModelMaterialGeometry = true;
		Fake.NextModelData = NAssetTest::FFakeAssetBackend::FModelData();
		Fake.NextModelData.MeshCount = 1;
		if ( Diffuse.IsValid() ) Fake.StaticModelSurfaceData = MakeWorldFakeSurface( Diffuse->GetHandle() );

		CScene ProbeScene; // spawn試験だけに使うisolated scene
		FWorldModelMaterialTestExecutor TestExecutor; // effect対応を切り替えるfake executor
		IDrawExecutor* DefaultExecutor = ProbeScene.GetDrawExecutor(); // override解除時に返るscene既定executor
		ProbeScene.SetDrawExecutor( &TestExecutor );
		const bool bOverrideSelected = ProbeScene.GetDrawExecutor() == &TestExecutor; // sceneが指定executorを選んだか
		ProbeScene.SetDrawExecutor( nullptr );
		const bool bDefaultRestored = ProbeScene.GetDrawExecutor() == DefaultExecutor; // null overrideで既定へ戻ったか
		ProbeScene.SetDrawExecutor( &TestExecutor );
		Checker.Check( bOverrideSelected && bDefaultRestored, "CSceneのexecutor overrideとnullptr時default参照を切り替えて元へ戻す" );

		FWorldDefinition SpawnWorld; // 複数個体とfallbackを含む定義
		SpawnWorld.Name = "World model material fake";
		const FWorldModelMaterialEffect ActiveBinding = MakeWorldModelMaterialBinding( 0 ); // 有効な対象material設定
		const FWorldModelMaterialEffect DisabledBinding = MakeWorldModelMaterialBinding( 0, false ); // 資源準備不要な無効設定
		FWorldModelMaterialEffect InvalidBinding = ActiveBinding; // validation拒否する負index設定
		InvalidBinding.MaterialIndex = -1;
		const int32 SecondActiveObjectId = 5; // 独立clockを持つ二つ目の個体id
		SpawnWorld.Objects.Add( MakeWorldModelObject( 1, "effect-individual", ModelPath, &ActiveBinding ) );
		SpawnWorld.Objects.Add( MakeWorldModelObject( 2, "native-sibling", ModelPath, nullptr ) );
		SpawnWorld.Objects.Add( MakeWorldModelObject( 3, "disabled-effect", ModelPath, &DisabledBinding ) );
		SpawnWorld.Objects.Add( MakeWorldModelObject( 4, "invalid-effect", ModelPath, &InvalidBinding ) );
		SpawnWorld.Objects.Add( MakeWorldModelObject( SecondActiveObjectId, "independent-effect-clock", ModelPath, &ActiveBinding ) );
		const int32 QueriesBeforeSpawn = Fake.NumStaticModelSurfaceQueries; // spawn直前surface query数
		const int32 CreatesBeforeSpawn = Fake.NumModelMaterialGeometryCreates; // spawn直前geometry作成数
		const int32 ExpectedNextGeometry = Fake.NextModelMaterialGeometry; // 次の成功geometryへ割当てるfake handle
		NWorld::FWorldSpawnOptions Options; // fake asset managerを使うspawn設定
		Options.Assets = &Assets;
		FWorldInstance Instance = NWorld::Spawn( ProbeScene, MakeShared<FWorldDefinition>( SpawnWorld ), FWorldBindings(), Options ); // 実worldのspawn結果と弱い参照群
		TSharedPtr<CGameObject> ActiveObject = Instance.FindById( 1 ); // effect対象モデルobject
		TSharedPtr<CGameObject> NativeObject = Instance.FindById( 2 ); // native siblingのreal model object
		TSharedPtr<CGameObject> DisabledObject = Instance.FindById( 3 ); // effect無効object
		TSharedPtr<CGameObject> InvalidObject = Instance.FindById( 4 ); // binding不正object
		TSharedPtr<CGameObject> OtherEffectObject = Instance.FindById( SecondActiveObjectId ); // 独立clock確認用のeffect object
		FModelAnimator* ActiveAnimator = ActiveObject.IsValid() ? ActiveObject->GetData<FModelAnimator>() : nullptr; // effect対象のmodel animator
		FModelAnimator* NativeAnimator = NativeObject.IsValid() ? NativeObject->GetData<FModelAnimator>() : nullptr; // native siblingのreal model animator
		FModelAnimator* DisabledAnimator = DisabledObject.IsValid() ? DisabledObject->GetData<FModelAnimator>() : nullptr; // disabled objectのmodel animator
		FModelAnimator* InvalidAnimator = InvalidObject.IsValid() ? InvalidObject->GetData<FModelAnimator>() : nullptr; // invalid objectのmodel animator
		FModelAnimator* OtherEffectAnimator = OtherEffectObject.IsValid() ? OtherEffectObject->GetData<FModelAnimator>() : nullptr; // 独立clock側のmodel animator
		const int32 SourceModelHandle = ActiveAnimator != nullptr && ActiveAnimator->Instance.IsValid() && ActiveAnimator->Instance->GetSource().IsValid() ? ActiveAnimator->Instance->GetSource()->GetHandle() : -1; // effect対象が参照する元model handle
		const int32 BorrowedDiffuseHandle = Diffuse.IsValid() ? Diffuse->GetHandle() : -1; // fake surfaceが借りるtexture handle
		const bool bValidSelectionPrepared = ActiveAnimator != nullptr && ActiveAnimator->GetMaterialEffect().IsPrepared(); // 有効bindingのrenderer準備結果
		const FDrawBuffer ActiveEffectBuffer = ActiveAnimator != nullptr ? RecordWorldModelAnimator( *ActiveAnimator ) : FDrawBuffer(); // effect対象個体の描画命令
		const FDrawBuffer NativeSiblingBuffer = NativeAnimator != nullptr ? RecordWorldModelAnimator( *NativeAnimator ) : FDrawBuffer(); // native sibling個体の描画命令
		const FDrawBuffer DisabledFallbackBuffer = DisabledAnimator != nullptr ? RecordWorldModelAnimator( *DisabledAnimator ) : FDrawBuffer(); // disabled binding個体のnative描画
		const FDrawBuffer InvalidFallbackBuffer = InvalidAnimator != nullptr ? RecordWorldModelAnimator( *InvalidAnimator ) : FDrawBuffer(); // invalid binding個体のnative描画
		const bool bOnlySelectedIndividualUsesEffect = CountWorldModelMaterialCommands( ActiveEffectBuffer, EDrawCommand::ModelMaterial3D ) == 1 && CountWorldModelMaterialCommands( ActiveEffectBuffer, EDrawCommand::Model3D ) == 0 && CountWorldModelMaterialCommands( NativeSiblingBuffer, EDrawCommand::ModelMaterial3D ) == 0 && CountWorldModelMaterialCommands( NativeSiblingBuffer, EDrawCommand::Model3D ) == 1; // 対象個体だけがeffect命令か
		const bool bDisabledAndInvalidUseNative = CountWorldModelMaterialCommands( DisabledFallbackBuffer, EDrawCommand::Model3D ) == 1 && CountWorldModelMaterialCommands( InvalidFallbackBuffer, EDrawCommand::Model3D ) == 1 && CountWorldModelMaterialCommands( DisabledFallbackBuffer, EDrawCommand::ModelMaterial3D ) == 0 && CountWorldModelMaterialCommands( InvalidFallbackBuffer, EDrawCommand::ModelMaterial3D ) == 0; // disabledとinvalidがnative fallbackか
		const bool bGeometryOwnedForBothEffectInstances = Fake.ModelMaterialGeometries.count( ExpectedNextGeometry ) == 1 && Fake.ModelMaterialGeometries.count( ExpectedNextGeometry + 1 ) == 1; // 二つのeffect個体geometryが作成されたか
		Checker.Check( bFixtureWritten && Diffuse.IsValid() && bValidSelectionPrepared && bOnlySelectedIndividualUsesEffect && bDisabledAndInvalidUseNative && OtherEffectAnimator != nullptr && OtherEffectAnimator->GetMaterialEffect().IsPrepared(), "world spawnは設定済み個体だけModelMaterial命令を使い、Sibling・disabled・invalidはnativeへ戻す" );
		Checker.Check( Fake.NumStaticModelSurfaceQueries == QueriesBeforeSpawn + 2 && Fake.NumModelMaterialGeometryCreates == CreatesBeforeSpawn + 2 && bGeometryOwnedForBothEffectInstances, "enabled bindingごとにsurface・geometryを準備しdisabled/invalid/Siblingはqueryしない" );

		const int32 QueriesBeforeRecord = Fake.NumStaticModelSurfaceQueries; // 反復Record前のsurface query数
		const int32 CreatesBeforeRecord = Fake.NumModelMaterialGeometryCreates; // 反復Record前のgeometry作成数
		FDrawBuffer RepeatedRecord; // 反復Record後の最後のbuffer
		if ( ActiveAnimator != nullptr ) for ( int32 Iteration = 0; Iteration < 6; ++Iteration ) { RepeatedRecord = RecordWorldModelAnimator( *ActiveAnimator ); }
		const bool bRecordDoesNotLoadOrPrepare = Fake.NumStaticModelSurfaceQueries == QueriesBeforeRecord && Fake.NumModelMaterialGeometryCreates == CreatesBeforeRecord; // Record中にresource準備が増えなかったか
		Checker.Check( bRecordDoesNotLoadOrPrepare && CountWorldModelMaterialCommands( RepeatedRecord, EDrawCommand::ModelMaterial3D ) == 1, "Record反復はresource再照会せず準備済み個体命令を維持する" );

		const double TimeBeforeUpdate = ActiveAnimator != nullptr ? ActiveAnimator->GetMaterialEffect().GetEffectTime() : -1.0; // real PreUpdate前のeffect clock
		if ( OtherEffectAnimator != nullptr ) OtherEffectAnimator->GetMaterialEffect().Pause();
		ProbeScene.GetSystems().Run( NEcs::EPhase::PreUpdate, 0.25f, ~0ull, ProbeScene.GetEcsWorld(), ProbeScene.GetTransformWorld(), nullptr );
		const double TimeAfterUpdate = ActiveAnimator != nullptr ? ActiveAnimator->GetMaterialEffect().GetEffectTime() : -1.0; // real PreUpdate後のeffect clock
		const double OtherPausedTime = OtherEffectAnimator != nullptr ? OtherEffectAnimator->GetMaterialEffect().GetEffectTime() : -1.0; // pause側個体のPreUpdate後clock
		Checker.Check( ActiveAnimator != nullptr && OtherEffectAnimator != nullptr && std::fabs( TimeBeforeUpdate ) < 1.0e-9 && std::fabs( TimeAfterUpdate - 0.25 ) < 1.0e-6 && std::fabs( OtherPausedTime ) < 1.0e-9, "ECS PreUpdateは一個体の時計を進め、pauseした別個体時計を独立保持する" );

		FDrawBuffer NativeBeforeStop; // Stop後に復帰したreal native command
		if ( ActiveAnimator != nullptr ) { ActiveAnimator->GetMaterialEffect().Stop(); NativeBeforeStop = RecordWorldModelAnimator( *ActiveAnimator ); }
		const bool bStopRestoresNative = CountWorldModelMaterialCommands( NativeBeforeStop, EDrawCommand::ModelMaterial3D ) == 0 && CountWorldModelMaterialCommands( NativeBeforeStop, EDrawCommand::Model3D ) == 1; // StopでModel3Dへ戻ったか
		Checker.Check( bStopRestoresNative, "world-spawnした個体のStopは従来Model3D命令へ戻す" );

		// executor非対応時はsnapshot/geometryに触れずnativeへfallbackする。
		FWorldDefinition UnsupportedWorld; // executor未対応時の検証world
		UnsupportedWorld.Objects.Add( MakeWorldModelObject( 1, "unsupported", ModelPath, &ActiveBinding ) );
		TestExecutor.bSupportsEffects = false;
		const int32 QueriesBeforeUnsupported = Fake.NumStaticModelSurfaceQueries; // unsupported spawn前のsurface query数
		const int32 CreatesBeforeUnsupported = Fake.NumModelMaterialGeometryCreates; // unsupported spawn前のgeometry数
		FWorldInstance UnsupportedInstance = NWorld::Spawn( ProbeScene, MakeShared<FWorldDefinition>( UnsupportedWorld ), FWorldBindings(), Options ); // unsupported worldのspawn結果
		TSharedPtr<CGameObject> UnsupportedObject = UnsupportedInstance.FindById( 1 ); // unsupported objectの弱い参照
		FModelAnimator* UnsupportedAnimator = UnsupportedObject.IsValid() ? UnsupportedObject->GetData<FModelAnimator>() : nullptr; // unsupported objectのmodel animator
		const FDrawBuffer UnsupportedBuffer = UnsupportedAnimator != nullptr ? RecordWorldModelAnimator( *UnsupportedAnimator ) : FDrawBuffer(); // unsupported objectのnative命令
		Checker.Check( UnsupportedAnimator != nullptr && CountWorldModelMaterialCommands( UnsupportedBuffer, EDrawCommand::Model3D ) == 1 && CountWorldModelMaterialCommands( UnsupportedBuffer, EDrawCommand::ModelMaterial3D ) == 0 && Fake.NumStaticModelSurfaceQueries == QueriesBeforeUnsupported && Fake.NumModelMaterialGeometryCreates == CreatesBeforeUnsupported, "executor未対応はnative命令へ戻り面queryとgeometry作成を行わない" );
		TestExecutor.bSupportsEffects = true;

		// disabled中はsurface取得も画像読込も追加しない。
		FWorldDefinition DisabledWorld; // disabled単独確認world
		DisabledWorld.Objects.Add( MakeWorldModelObject( 1, "disabled-only", ModelPath, &DisabledBinding ) );
		const int32 QueriesBeforeDisabled = Fake.NumStaticModelSurfaceQueries; // disabled spawn前のsurface query数
		const int32 CreatesBeforeDisabled = Fake.NumModelMaterialGeometryCreates; // disabled spawn前のgeometry数
		FWorldInstance DisabledInstance = NWorld::Spawn( ProbeScene, MakeShared<FWorldDefinition>( DisabledWorld ), FWorldBindings(), Options ); // disabled worldのspawn結果
		Checker.Check( Fake.NumStaticModelSurfaceQueries == QueriesBeforeDisabled && Fake.NumModelMaterialGeometryCreates == CreatesBeforeDisabled, "disabled bindingだけのworldはsurface・geometry queryを増やさない" );

		// geometry作成失敗時も同じ個体のnative描画を残す。
		const FString FailedModelFile = Directory / "world_model_material_geometry_fail.mv1"; // geometry失敗model fixtureのpath
		WriteFileText( FailedModelFile, "fake model with failed geometry" );
		Fake.NextModelData = NAssetTest::FFakeAssetBackend::FModelData();
		Fake.NextModelData.MeshCount = 1;
		Fake.FailModelMaterialGeometryCreateCall = Fake.NumModelMaterialGeometryCreates + 1;
		FWorldDefinition GeometryFailureWorld; // geometry作成失敗確認world
		GeometryFailureWorld.Objects.Add( MakeWorldModelObject( 1, "geometry-failure", "world_model_material_geometry_fail.mv1", &ActiveBinding ) );
		const int32 QueriesBeforeGeometryFailure = Fake.NumStaticModelSurfaceQueries; // 失敗準備前のsurface query数
		FWorldInstance GeometryFailureInstance = NWorld::Spawn( ProbeScene, MakeShared<FWorldDefinition>( GeometryFailureWorld ), FWorldBindings(), Options ); // geometry失敗worldのspawn結果
		TSharedPtr<CGameObject> GeometryFailureObject = GeometryFailureInstance.FindById( 1 ); // geometry失敗object参照
		FModelAnimator* GeometryFailureAnimator = GeometryFailureObject.IsValid() ? GeometryFailureObject->GetData<FModelAnimator>() : nullptr; // geometry失敗objectのanimator
		const FDrawBuffer GeometryFailureBuffer = GeometryFailureAnimator != nullptr ? RecordWorldModelAnimator( *GeometryFailureAnimator ) : FDrawBuffer(); // geometry失敗後のnative命令
		const bool bGeometryFailureFallsBack = GeometryFailureAnimator != nullptr && !GeometryFailureAnimator->GetMaterialEffect().IsPrepared() && CountWorldModelMaterialCommands( GeometryFailureBuffer, EDrawCommand::Model3D ) == 1; // geometry失敗後native fallbackの成否
		Checker.Check( bGeometryFailureFallsBack && Fake.NumStaticModelSurfaceQueries == QueriesBeforeGeometryFailure + 1 && Fake.ModelMaterialGeometries.count( Fake.NextModelMaterialGeometry ) == 0, "geometry準備失敗は一度のsnapshot query後にnativeへ戻り中途資源を残さない" );
		Fake.FailModelMaterialGeometryCreateCall = OldGeometryCreateCall;

		// animation clipを再生中の個体はmaterial effect準備から除外する。
		const FString AnimatedModelFile = Directory / "world_model_material_animated_fake.mv1"; // animation付きmodel fixtureのpath
		WriteFileText( AnimatedModelFile, "fake animated model" );
		Fake.NextModelData = NAssetTest::FFakeAssetBackend::FModelData();
		Fake.NextModelData.MeshCount = 1;
		Fake.NextModelData.Names = { "Idle" };
		Fake.NextModelData.Lengths = { 30.0f };
		FWorldDefinition AnimatedWorld; // animation attached確認world
		FWorldObject AnimatedObjectDefinition = MakeWorldModelObject( 1, "animated-effect", "world_model_material_animated_fake.mv1", &ActiveBinding ); // clipを再生するmodel object定義
		AnimatedObjectDefinition.Model.GetValue().Clip = "Idle";
		AnimatedWorld.Objects.Add( AnimatedObjectDefinition );
		const int32 QueriesBeforeAnimatedSpawn = Fake.NumStaticModelSurfaceQueries; // animation spawn前のsurface query数
		const int32 CreatesBeforeAnimatedSpawn = Fake.NumModelMaterialGeometryCreates; // animation spawn前のgeometry数
		FWorldInstance AnimatedInstance = NWorld::Spawn( ProbeScene, MakeShared<FWorldDefinition>( AnimatedWorld ), FWorldBindings(), Options ); // animation worldのspawn結果
		TSharedPtr<CGameObject> AnimatedObject = AnimatedInstance.FindById( 1 ); // animation object参照
		FModelAnimator* AnimatedAnimator = AnimatedObject.IsValid() ? AnimatedObject->GetData<FModelAnimator>() : nullptr; // animation objectのmodel animator
		const FDrawBuffer AnimatedBuffer = AnimatedAnimator != nullptr ? RecordWorldModelAnimator( *AnimatedAnimator ) : FDrawBuffer(); // animation objectのnative命令
		const bool bAnimatedModelFallsBack = AnimatedAnimator != nullptr && AnimatedAnimator->Instance.IsValid() && AnimatedAnimator->Instance->GetNumAttached() > 0 && CountWorldModelMaterialCommands( AnimatedBuffer, EDrawCommand::Model3D ) == 1; // attached animationのnative fallback有無
		Checker.Check( bAnimatedModelFallsBack && Fake.NumStaticModelSurfaceQueries == QueriesBeforeAnimatedSpawn && Fake.NumModelMaterialGeometryCreates == CreatesBeforeAnimatedSpawn, "animation attached個体はsnapshotを問い合わせずnative Model3Dを使う" );
		Fake.NextModelData = OldModelData;

		// 所有者を外してから遅延解放を進め、fake geometryとsource handleを確認する。
		const size_t DeletedBeforeCleanup = Fake.DeletedModelMaterialGeometries.size(); // cleanup前のfake geometry解放数
		Instance.Despawn(); UnsupportedInstance.Despawn(); DisabledInstance.Despawn(); GeometryFailureInstance.Despawn(); AnimatedInstance.Despawn();
		ActiveObject.Reset(); NativeObject.Reset(); DisabledObject.Reset(); InvalidObject.Reset(); OtherEffectObject.Reset(); UnsupportedObject.Reset(); GeometryFailureObject.Reset(); AnimatedObject.Reset();
		ActiveAnimator = nullptr; NativeAnimator = nullptr; DisabledAnimator = nullptr; InvalidAnimator = nullptr; OtherEffectAnimator = nullptr; UnsupportedAnimator = nullptr; GeometryFailureAnimator = nullptr; AnimatedAnimator = nullptr;
		ProbeScene.DestroyAllObjects();
		const bool bSourceReleased = SourceModelHandle >= 0 && Fake.Models.count( SourceModelHandle ) == 0; // spawn元model handleが解放されたか
		Diffuse.Reset();
		Assets.ReleaseUnused();
		Assets.Tick( 0.0f );
		const bool bBorrowedImageReleased = BorrowedDiffuseHandle >= 0 && std::find( Fake.DeletedTextures.begin(), Fake.DeletedTextures.end(), BorrowedDiffuseHandle ) != Fake.DeletedTextures.end(); // borrowed texture handleが解放されたか
		Checker.Check( Fake.DeletedModelMaterialGeometries.size() > DeletedBeforeCleanup && Fake.ModelMaterialGeometries.empty() && bSourceReleased && bBorrowedImageReleased, "DestroyAllObjects・ReleaseUnused・Tick後にgeometry・元model・借用diffuse ownerを解放する" );

		Fake.bSupportStaticModelSurface = bOldSurfaceSupport;
		Fake.StaticModelSurfaceData = OldSurface;
		Fake.NextModelData = OldModelData;
		Fake.bSupportModelMaterialGeometry = bOldGeometrySupport;
		Fake.FailModelMaterialGeometryCreateCall = OldGeometryCreateCall;
		Checker.Check( Fake.NumStaticModelSurfaceQueries >= OldQueries && Fake.NumModelMaterialGeometryCreates >= OldGeometryCreates && Fake.DeletedModelMaterialGeometries.size() >= OldDeletedGeometryCount, "fake countersは検査中に進み既存backend設定は片付け後に戻る" );
		Checker.Group = OldGroup;
	}

	void RunWorldModelMaterialSpawnRealChecks( NAssetTest::FChecker& Checker, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Directory )
	{
		const char* OldGroup = Checker.Group; // 呼出元が設定していたcheck group
		Checker.Group = "world-model-material-spawn-real";
		const FString FixtureDirectory = Directory / "world_model_material_spawn"; // 実backend fixtureを置くsandbox directory
		const bool bFixtureWritten = NFileSystem::EnsureDirectory( FixtureDirectory ) && NAssetTest::WriteStaticModelSurfaceFixture( FixtureDirectory, "world_model_material.x" ); // real .xと画像fixtureの書込結果
		Assets.SetRootDirectory( Directory );
		const TSharedPtr<CModel> Source = bFixtureWritten ? Assets.LoadModel( "world_model_material_spawn/world_model_material.x" ) : TSharedPtr<CModel>(); // 実fixtureから読み込む元model asset
		const TSharedPtr<const FStaticModelSurface> Surface = Source.IsValid() ? Source->CreateStaticSurfaceSnapshot() : TSharedPtr<const FStaticModelSurface>(); // ChildTriangleと材質sectionの不変snapshot
		bool bChildMaterialFound = false; // ChildTriangleに対応する材質3があるか
		if ( Surface.IsValid() ) for ( const FStaticModelSurfaceSection& Section : Surface->GetData().Sections ) bChildMaterialFound = bChildMaterialFound || Section.MaterialIndex == 3;
		Checker.Check( bFixtureWritten && Surface.IsValid() && bChildMaterialFound, "実fixtureのChildTriangleに対応するmaterial 3のsurface sectionを取得する" );
		if ( !bFixtureWritten || !Surface.IsValid() || !bChildMaterialFound )
		{
			Checker.Group = OldGroup;
			return;
		}

		// world fileへ保存し、asset managerで新しい定義を読み直す。
		FWorldDefinition Definition; // save/loadするworld定義
		Definition.Name = "World model material spawn";
		FWorldModelMaterialEffect Binding = MakeWorldModelMaterialBinding( 3 ); // ChildTriangleへ適用するbinding値
		Binding.bTwoSided = true; // 実commandでTwoSidedの保存値を確認する設定
		Definition.Objects.Add( MakeWorldModelObject( 1, "effect-instance", "world_model_material_spawn/world_model_material.x", &Binding ) );
		Definition.Objects.Add( MakeWorldModelObject( 2, "native-sibling", "world_model_material_spawn/world_model_material.x", nullptr ) );
		const bool bSaved = Assets.SaveWorld( "world_model_material_spawn.world", Definition ); // world定義をassetへ保存できたか
		const TSharedPtr<CWorldAsset> FreshWorld = bSaved ? Assets.LoadWorld( "world_model_material_spawn.world" ) : TSharedPtr<CWorldAsset>(); // 保存後に別読込したworld asset
		const bool bFreshBinding = FreshWorld.IsValid() && FreshWorld->GetDefinition().FindObject( 1 ) != nullptr && FreshWorld->GetDefinition().FindObject( 1 )->Model.IsSet() && FreshWorld->GetDefinition().FindObject( 1 )->Model.GetValue().MaterialEffect.IsSet() && FreshWorld->GetDefinition().FindObject( 1 )->Model.GetValue().MaterialEffect.GetValue().IsSameAs( Binding ); // 再読bindingが設定値と一致したか
		Checker.Check( bSaved && bFreshBinding, "SaveWorld後のfresh LoadWorldは設定済みModel bindingを全field保持する" );

		CScene ProbeScene; // spawn試験だけに使うisolated scene
		ProbeScene.SetDrawExecutor( &Executor );
		NWorld::FWorldSpawnOptions Options; // fake asset managerを使うspawn設定
		Options.Assets = &Assets;
		FWorldInstance Instance = FreshWorld.IsValid() ? NWorld::Spawn( ProbeScene, FreshWorld->GetDefinitionShared(), FWorldBindings(), Options ) : FWorldInstance(); // 実worldのspawn結果と弱い参照群
		TSharedPtr<CGameObject> EffectObject = Instance.FindById( 1 ); // effect対象のreal model object
		TSharedPtr<CGameObject> NativeObject = Instance.FindById( 2 ); // native siblingのreal model object
		FModelAnimator* EffectAnimator = EffectObject.IsValid() ? EffectObject->GetData<FModelAnimator>() : nullptr; // effect対象のreal model animator
		FModelAnimator* NativeAnimator = NativeObject.IsValid() ? NativeObject->GetData<FModelAnimator>() : nullptr; // native siblingのreal model animator
		const bool bPrepared = EffectAnimator != nullptr && EffectAnimator->GetMaterialEffect().IsPrepared(); // 実backendでeffect resource準備できたか
		const FDrawBuffer EffectBuffer = EffectAnimator != nullptr ? RecordWorldModelAnimator( *EffectAnimator ) : FDrawBuffer(); // effect対象のreal command列
		const FDrawBuffer NativeBuffer = NativeAnimator != nullptr ? RecordWorldModelAnimator( *NativeAnimator ) : FDrawBuffer(); // siblingのreal native command列
		FDrawModelMaterialData RecordedBinding{}; // 実命令から読む独立payload値
		int32 RecordedBindingCount = 0; // 実命令payloadの個数
		if ( !EffectBuffer.Words.empty() ) ForEachDrawCommand( FDrawSpan{ EffectBuffer.Words.data(), static_cast<int32>( EffectBuffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::ModelMaterial3D && Command.NumDataWords == static_cast<int32>( sizeof( FDrawModelMaterialData ) / sizeof( uint32 ) ) ) { RecordedBinding = Command.Read<FDrawModelMaterialData>(); ++RecordedBindingCount; } } );
		CDxLibAssetBackend* RealBackend = dynamic_cast<CDxLibAssetBackend*>( Assets.GetBackend() ); // real geometryの材質対応を確認するbackend
		const FDxLibModelMaterialGeometry* RecordedGeometry = RealBackend != nullptr && RecordedBinding.Geometry >= 0 ? RealBackend->FindModelMaterialGeometry( RecordedBinding.Geometry ) : nullptr; // commandが参照する実geometry
		bool bGeometrySelectsMaterialThree = false; // command geometryがfixtureのmaterial 3を選んだか
		if ( RecordedGeometry != nullptr ) for ( const FDxLibModelMaterialGeometry::FMesh& Mesh : RecordedGeometry->Meshes ) bGeometrySelectsMaterialThree = bGeometrySelectsMaterialThree || Mesh.MaterialIndex == 3;
		const bool bRecordedWorldBinding = RecordedBindingCount == 1 && EffectBuffer.Words.size() == 41 && RecordedBinding.Handle == ( EffectAnimator != nullptr && EffectAnimator->Instance.IsValid() ? EffectAnimator->Instance->GetHandle() : -1 ) && bGeometrySelectsMaterialThree && RecordedBinding.BlendMode == static_cast<int32>( EDrawBlend::Add ) && RecordedBinding.Opacity == 0.5f && RecordedBinding.Material.bTwoSided == 1; // geometryの材質indexと効果設定が定義値通りか
		Checker.Check( bPrepared && bRecordedWorldBinding && CountWorldModelMaterialCommands( EffectBuffer, EDrawCommand::ModelMaterial3D ) == 1 && CountWorldModelMaterialCommands( EffectBuffer, EDrawCommand::Model3D ) == 0 && CountWorldModelMaterialCommands( NativeBuffer, EDrawCommand::Model3D ) == 1 && CountWorldModelMaterialCommands( NativeBuffer, EDrawCommand::ModelMaterial3D ) == 0, "real spawnはmaterial 3・TwoSided・Opacity・Addを41word命令へ記録しSiblingはnativeを保つ" );

		// PreUpdateシステムを直接一度走らせ、spawnされたAnimatorの時計を測る。
		const double TimeBeforeUpdate = EffectAnimator != nullptr ? EffectAnimator->GetMaterialEffect().GetEffectTime() : -1.0; // real PreUpdate前のeffect clock
		ProbeScene.GetSystems().Run( NEcs::EPhase::PreUpdate, 0.2f, ~0ull, ProbeScene.GetEcsWorld(), ProbeScene.GetTransformWorld(), nullptr );
		const double TimeAfterUpdate = EffectAnimator != nullptr ? EffectAnimator->GetMaterialEffect().GetEffectTime() : -1.0; // real PreUpdate後のeffect clock
		Checker.Check( EffectAnimator != nullptr && std::fabs( TimeBeforeUpdate ) < 1.0e-9 && std::fabs( TimeAfterUpdate - 0.2 ) < 1.0e-6, "実world spawnのModelAnimatorはECS PreUpdateでbinding固有clockを進める" );

		FDrawBuffer NativeBeforeStop; // Stop後に復帰したreal native command
		if ( EffectAnimator != nullptr ) { EffectAnimator->GetMaterialEffect().Stop(); NativeBeforeStop = RecordWorldModelAnimator( *EffectAnimator ); }
		Checker.Check( CountWorldModelMaterialCommands( NativeBeforeStop, EDrawCommand::ModelMaterial3D ) == 0 && CountWorldModelMaterialCommands( NativeBeforeStop, EDrawCommand::Model3D ) == 1, "実world spawnでもStop後はnative Model3D fallbackへ戻る" );

		Instance.Despawn();
		EffectObject.Reset(); NativeObject.Reset();
		EffectAnimator = nullptr; NativeAnimator = nullptr;
		ProbeScene.DestroyAllObjects();
		Assets.ReleaseUnused();
		Assets.Tick( 0.0f );
		Checker.Group = OldGroup;
	}
}
