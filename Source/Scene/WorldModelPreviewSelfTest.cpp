// SPDX-License-Identifier: NOASSERTION
#include "WorldModelPreviewSelfTest.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <map>
#include <set>
#include <thread>
#include <vector>

#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Interfaces/Render/DrawModelMaterial.h"
#include "Library/DebugUI/WorldEditor.h"
#include "Library/DebugUI/WorldEditorInternal.h"
#include "Library/Framework/Animation/ModelAnimator.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/EditorAssetInfo.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Asset/ModelAnimationAsset.h"
#include "Library/Framework/Asset/Model.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/World/WorldDefinition.h"
#include "Library/Framework/World/WorldModelMaterialEffect.h"
#include "Library/Platform/FileSystem.h"

namespace NWorldTest
{
	namespace
	{
		// 個体cache確認に使う固定Model効果設定。
		FWorldModelMaterialEffect MakePreviewBinding( int32 MaterialIndex, bool bEnabled = true )
		{
			FWorldModelMaterialEffect Binding; // componentへ返すbinding値
			Binding.MaterialIndex = MaterialIndex;
			Binding.Effect.Material.bEnabled = bEnabled;
			Binding.Effect.Duration = 2.0f;
			Binding.Effect.bLoop = true;
			Binding.Effect.Opacity = 0.55f;
			Binding.Effect.Blend = EDrawBlend::Add;
			Binding.Effect.Material.Emission.Keys.Add( FEffectScalarCurve::FKey{ 0.0f, 0.8f } );
			Binding.Effect.Material.Emission.Keys.Add( FEffectScalarCurve::FKey{ 1.0f, 0.8f } );
			Binding.bTwoSided = true;
			return Binding;
		}

		// fake surfaceに2材質の三角形と借用画像handleを用意する。
		FStaticModelSurfaceData MakePreviewFakeSurface( int32 DiffuseHandle )
		{
			FStaticModelSurfaceData Surface; // fake modelの二材質面情報
			for ( int32 MaterialIndex = 0; MaterialIndex < 2; ++MaterialIndex ) // source fixtureの材質slot
			{
				for ( int32 Corner = 0; Corner < 3; ++Corner ) // 三角形の頂点番号
				{
					FMeshVertex Vertex; // 現在作るtriangle vertex
					Vertex.Position[0] = static_cast<float32>( MaterialIndex * 3 + ( Corner == 2 ? 1 : 0 ) );
					Vertex.Position[1] = Corner == 1 ? 1.0f : 0.0f;
					Vertex.Normal[2] = 1.0f;
					Vertex.U = Corner == 2 ? 1.0f : 0.0f;
					Vertex.V = Corner == 1 ? 1.0f : 0.0f;
					Surface.Vertices.Add( Vertex );
					Surface.Indices.Add( static_cast<uint32>( MaterialIndex * 3 + Corner ) );
				}
				FStaticModelSurfaceSection Section; // 一つの材質slotを指すsection
				Section.FirstIndex = MaterialIndex * 3;
				Section.NumIndices = 3;
				Section.MaterialIndex = MaterialIndex;
				Section.SourceFrameIndex = 0;
				Section.SourceMeshIndex = MaterialIndex;
				Surface.Sections.Add( Section );
				FStaticModelSurfaceMaterial Material; // sectionに対応するtexture情報
				Material.BorrowedDiffuseTexture = DiffuseHandle;
				Surface.Materials.Add( Material );
			}
			return Surface;
		}

		// world objectにmodel pathとoptional bindingを設定する。
		FWorldObject MakePreviewModelObject( int32 Id, const FString& Path, const FWorldModelMaterialEffect* Binding, const FVector3& Position = FVector3::Zero )
		{
			FWorldObject Object; // caller指定のmodel object値
			Object.Id = Id;
			Object.Name = "model-preview";
			Object.Transform.Position = Position;
			FWorldModel Model; // objectのstatic model定義
			Model.Path = Path;
			if ( Binding != nullptr ) Model.MaterialEffect = *Binding;
			Object.Model = Model;
			return Object;
		}

		// draw buffer内の指定命令数を数える。
		int32 CountPreviewCommands( const FDrawBuffer& Buffer, EDrawCommand Type )
		{
			int32 Count = 0; // 指定commandの累計
			if ( !Buffer.Words.empty() ) ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == Type ) ++Count; } );
			return Count;
		}

		// ModelMaterial commandの固定payloadを指定handleから読む。
		bool FindPreviewCommand( const FDrawBuffer& Buffer, int32 Handle, FDrawModelMaterialData& Out )
		{
			bool bFound = false; // handle一致commandを見つけたか
			const auto ReadCommand = [&]( const FDrawCommandView& Command ) // 指定handleのpayloadを抽出するvisitor
			{
				if ( Command.Type != EDrawCommand::ModelMaterial3D || Command.NumDataWords != static_cast<int32>( sizeof( FDrawModelMaterialData ) / sizeof( uint32 ) ) ) return;
				const FDrawModelMaterialData Data = Command.Read<FDrawModelMaterialData>(); // 固定payloadを検査用に複製
				if ( Data.Handle != Handle ) return;
				Out = Data;
				bFound = true;
			};
			if ( !Buffer.Words.empty() ) ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, ReadCommand );
			return bFound;
		}

		// fake executorの効果対応を切り替える。
		class FModelPreviewTestExecutor final : public IDrawExecutor
		{
		public:
			bool bSupports = true; // testで切替える効果対応状態
			// command実行はfake backendの記録だけで済ませる。
			void Execute( const FDrawSpan& Span ) override { (void)Span; }
			// 現在の対応状態をruntimeへ返す。
			bool SupportsModelMaterialEffects() const override { return bSupports; }
		};

		// 意図的なhash衝突下でもkey全体で値を分けるhasher。
		struct FConstantPreviewKeyHasher
		{
			uint32 operator()( const FWorldEditorModelPreviewKey& Key ) const { (void)Key; return 0; }
		};

		// keyへrootからleafまでのobject idを設定する。
		FWorldEditorModelPreviewKey MakePreviewKey( std::initializer_list<int32> Ids )
		{
			FWorldEditorModelPreviewKey Key; // 構築中のobject経路key
			for ( const int32 Id : Ids ) Key.ObjectIds.Add( Id ); // owner経路の各object id
			return Key;
		}

		// backendに残ったchunkの全vertexとindexがcleanup前と一致するか調べる。
		bool SamePreviewGeometryMaps( const std::map<int32, TArray<FMeshGeometryChunk>>& A, const std::map<int32, TArray<FMeshGeometryChunk>>& B )
		{
			if ( A.size() != B.size() ) return false;
			for ( const auto& Pair : A ) // 比較元のgeometry handleとchunks
			{
				// 同じgeometry handleの保存済みchunks。
				const auto Found = B.find( Pair.first ); // 比較先の同じhandle entry
				if ( Found == B.end() || Pair.second.Num() != Found->second.Num() ) return false;
				for ( int32 Index = 0; Index < Pair.second.Num(); ++Index ) // 対応するchunkの位置
				{
					// 対応する左右chunkの頂点とindex。
					const FMeshGeometryChunk& Left = Pair.second[Index];
					const FMeshGeometryChunk& Right = Found->second[Index];
					if ( Left.Vertices.Num() != Right.Vertices.Num() || Left.Indices.Num() != Right.Indices.Num() ) return false;
					if ( !Left.Vertices.IsEmpty() && std::memcmp( Left.Vertices.GetData(), Right.Vertices.GetData(), static_cast<size_t>( Left.Vertices.Num() ) * sizeof( FMeshVertex ) ) != 0 ) return false;
					if ( !Left.Indices.IsEmpty() && std::memcmp( Left.Indices.GetData(), Right.Indices.GetData(), static_cast<size_t>( Left.Indices.Num() ) * sizeof( uint32 ) ) != 0 ) return false;
				}
			}
			return true;
		}

	}

	void RunWorldModelPreviewFakeChecks( NAssetTest::FChecker& Checker, CAssetManager& Assets, NAssetTest::FFakeAssetBackend& Fake, const FString& Directory )
	{
		const char* OldGroup = Checker.Group; // 呼出元が使っていた検査group
		Checker.Group = "world-model-preview-fake";
		const FString OldRoot = Assets.GetRootDirectory(); // 呼出元のasset root
		const bool bOldSurfaceSupport = Fake.bSupportStaticModelSurface; // 元backendの面情報対応状態
		const FStaticModelSurfaceData OldSurface = Fake.StaticModelSurfaceData; // 元backendの固定面情報
		const NAssetTest::FFakeAssetBackend::FModelData OldModelData = Fake.NextModelData; // 次に複製する元model設定
		const bool bOldGeometrySupport = Fake.bSupportModelMaterialGeometry; // 元backendの個体geometry対応状態
		const int32 OldFailGeometry = Fake.FailModelMaterialGeometryCreateCall; // 元geometry失敗位置
		const int32 OldNext = Fake.Next; // model・texture handle採番値
		const int32 OldNextGeometry = Fake.NextModelMaterialGeometry; // geometry handle採番値
		const int32 OldTextureLoads = Fake.NumTextureLoads; // 呼出元texture load累計
		const int32 OldLoadingQueries = Fake.NumLoadingQueries; // 呼出元loading照会累計
		const int32 OldDuplicates = Fake.NumDuplicates; // 呼出元model複製累計
		const int32 OldSurfaceQueries = Fake.NumStaticModelSurfaceQueries; // 呼出元source面照会累計
		const int32 OldGeometryCreates = Fake.NumModelMaterialGeometryCreates; // 呼出元geometry作成累計
		const std::set<int32> OldModels = Fake.Models; // 呼出元live model handle集合
		const std::set<int32> OldTextures = Fake.Textures; // 呼出元live texture handle集合
		const std::map<int32, TArray<FMeshGeometryChunk>> OldGeometries = Fake.ModelMaterialGeometries; // 呼出元geometry内容
		const std::vector<int32> OldDeletedGeometries = Fake.DeletedModelMaterialGeometries; // 呼出元geometry削除記録
		const std::vector<int32> OldDeletedTextures = Fake.DeletedTextures; // 呼出元texture削除記録
		const std::set<int32> OldLoading = Fake.Loading; // 呼出元loading handle集合
		const int32 OldFailTexture = Fake.FailTextureLoadCall; // 元texture失敗位置
		const FString ModelPath = "world_model_preview_fake.mv1"; // 同一sourceを共有するmodel path
		const FString PendingModelPath = "world_model_preview_pending.mv1"; // 非同期完了を制御するmodel path
		const FString TexturePath = "world_model_preview_fake.tga"; // fake modelに設定するborrowed画像path
		const std::vector<uint8> TinyTga = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 24, 0, 0, 255, 0 }; // 1x1 diffuse TGA bytes
		const bool bFilesWritten = NFileSystem::EnsureDirectory( Directory ) && NAssetTest::WriteFileText( Directory / ModelPath, "fake static model" ) && NAssetTest::WriteFileText( Directory / PendingModelPath, "fake pending model" ) && NAssetTest::WriteFileBytes( Directory / TexturePath, TinyTga ); // source・pending・画像fixtureの書込成否
		Assets.SetRootDirectory( Directory );
		Fake.bSupportStaticModelSurface = true;
		Fake.bSupportModelMaterialGeometry = true;
		Fake.FailModelMaterialGeometryCreateCall = 0;
		Fake.FailTextureLoadCall = 0;
		Fake.NextModelData = NAssetTest::FFakeAssetBackend::FModelData();
		Fake.NextModelData.MeshCount = 1;
		Fake.NextModelData.Names = { "Idle" }; // custom animation assetが参照するsource clip名
		Fake.NextModelData.Lengths = { 30.0f }; // Idle clipの長さ
		TSharedPtr<CModel> PendingSource = bFilesWritten ? Assets.LoadModel( PendingModelPath, true ) : TSharedPtr<CModel>(); // 非同期状態を作る専用source
		TSharedPtr<CTexture> Diffuse = bFilesWritten ? Assets.LoadTexture( TexturePath ) : TSharedPtr<CTexture>(); // fake surfaceが借用する画像
		if ( Diffuse.IsValid() ) Fake.StaticModelSurfaceData = MakePreviewFakeSurface( Diffuse->GetHandle() );
		CAssetManager* AppAssets = GetAppSubsystem<CAssetManager>();
		Checker.Check( AppAssets == &Assets, "preview fake testがworld editorと同じasset managerを使う" );
		Checker.Check( bFilesWritten, "model・pending model・diffuse fixtureを一時rootへ書き込む" );
		FWorldEditorState State; // owner mapを持つeditor session
		do
		{
			if ( AppAssets != &Assets || !bFilesWritten ) break;

		// 同じPrefab内leafを、root Idと同じIdで二か所に配置するworld。
		const FWorldModelMaterialEffect Binding = MakePreviewBinding( 1 ); // nestedとrootが共有するeffect設定
		FWorldDefinition Inner; // leaf objectを含むinner prefab world
		Inner.Name = "Model preview inner";
		Inner.NextId = 8;
		Inner.Objects.Add( MakePreviewModelObject( 7, ModelPath, &Binding, FVector3( 3.0f, 0.0f, 0.0f ) ) );
		FWorldDefinition Outer; // nested hostを含むouter prefab world
		Outer.Name = "Model preview outer";
		Outer.NextId = 5;
		FWorldObject NestedHost; // inner prefabを配置するhost
		NestedHost.Id = 4;
		NestedHost.Name = "nested-host";
		NestedHost.Transform.Position = FVector3( 2.0f, 0.0f, 0.0f );
		FWorldPrefab NestedPrefab; // hostから参照するinner prefab
		NestedPrefab.Path = "model_preview_inner.world";
		NestedHost.Prefab = NestedPrefab;
		Outer.Objects.Add( NestedHost );
		const bool bWorldsSaved = Assets.SaveWorld( "model_preview_inner.world", Inner ) && Assets.SaveWorld( "model_preview_outer.world", Outer ); // prefab fixtureの保存成否
		FWorldDefinition World; // 複数個体とprefabを含むroot world
		World.Name = "Model preview owners";
		World.NextId = 17;
		World.Objects.Add( MakePreviewModelObject( 7, ModelPath, &Binding, FVector3( -10.0f, 0.0f, 0.0f ) ) );
		World.Objects.Add( MakePreviewModelObject( 8, ModelPath, nullptr, FVector3( -5.0f, 0.0f, 0.0f ) ) );
		FWorldModelMaterialEffect MissingTextureBinding = Binding; // 欠落画像のnative fallbackを確かめる設定
		MissingTextureBinding.Effect.Material.bDissolve = true;
		MissingTextureBinding.Effect.Material.MaskTexture = "missing-preview-mask.tga";
		World.Objects.Add( MakePreviewModelObject( 13, ModelPath, &MissingTextureBinding, FVector3( -2.0f, 0.0f, 0.0f ) ) );
		FWorldModelMaterialEffect DisabledBinding = MakePreviewBinding( 1, false ); // disabled時に読まないmask/noise設定
		DisabledBinding.Effect.Material.bDissolve = true;
		DisabledBinding.Effect.Material.MaskTexture = "missing-disabled-preview-mask.tga";
		DisabledBinding.Effect.Material.Distortion.bEnabled = true;
		DisabledBinding.Effect.Material.Distortion.Texture = "missing-disabled-preview-noise.tga";
		DisabledBinding.Effect.Material.Distortion.Strength.Keys.Add( FEffectScalarCurve::FKey{ 0.0f, 0.25f } );
		DisabledBinding.Effect.Material.Distortion.Strength.Keys.Add( FEffectScalarCurve::FKey{ 1.0f, 0.25f } );
		FWorldObject DisabledObject = MakePreviewModelObject( 14, ModelPath, &DisabledBinding ); // effect無しでnative描画するmodel object
		DisabledObject.bEnabled = true; // objectは有効だが材質効果だけをdisabledにする
		World.Objects.Add( DisabledObject );
		FWorldObject DisabledObjectOnly = MakePreviewModelObject( 15, ModelPath, &Binding ); // object flagだけで無効にする別model
		DisabledObjectOnly.bEnabled = false; // world traversal対象外にするobject flag
		World.Objects.Add( DisabledObjectOnly );
		const FString AnimationPath = "world_model_preview_custom.modelanim.json"; // 独自clipを登録するanimation asset path
		FModelAnimationDefinition AnimationDefinition; // non-default補正を持つcustom animation set
		AnimationDefinition.ModelPath = ModelPath;
		AnimationDefinition.Offset = FVector3( 0.25f, -0.5f, 0.75f );
		AnimationDefinition.Scale = FVector3( 1.5f, 0.75f, 2.0f );
		FModelClipDef CustomClip; // custom名からIdle sourceを使うclip
		CustomClip.Name = "Custom";
		CustomClip.SourcePath = ModelPath;
		CustomClip.AnimName = "Idle";
		AnimationDefinition.Clips.Add( CustomClip );
		const bool bAnimationSaved = Assets.SaveModelAnimationDefinition( AnimationPath, AnimationDefinition ); // animation定義の保存成否
		FWorldObject AnimatedObject = MakePreviewModelObject( 16, ModelPath, &Binding ); // attached animationによるfallback対象
		AnimatedObject.Model.GetValue().Animations = AnimationPath;
		AnimatedObject.Model.GetValue().Clip = "Custom";
		AnimatedObject.Model.GetValue().Speed = 1.25f;
		World.Objects.Add( AnimatedObject );
		if ( PendingSource.IsValid() ) World.Objects.Add( MakePreviewModelObject( 9, PendingModelPath, &Binding, FVector3( 5.0f, 0.0f, 0.0f ) ) );
		FWorldObject FirstHost; // 一つ目のprefab配置
		FirstHost.Id = 11;
		FirstHost.Transform.Position = FVector3( 10.0f, 0.0f, 0.0f );
		FWorldPrefab FirstPrefab; // 両hostで共有するprefab定義
		FirstPrefab.Path = "model_preview_outer.world";
		FirstHost.Prefab = FirstPrefab;
		World.Objects.Add( FirstHost );
		FWorldObject SecondHost; // 二つ目のprefab配置
		SecondHost.Id = 12;
		SecondHost.Transform.Position = FVector3( 30.0f, 0.0f, 0.0f );
		SecondHost.Prefab = FirstPrefab;
		World.Objects.Add( SecondHost );
		State.bShowGrid = false;
		State.Session.Reset( World, "model_preview_owners.world" );
		State.AssetInfo = MakeUnique<FAssetEditorInfo>( Assets );
		FModelPreviewTestExecutor Executor; // 個体previewを受け付けるfake executor
		const int32 TextureLoadsBeforeWorldUpdate = Fake.NumTextureLoads; // disabled/欠落効果の前後で比較するload数
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.125f, &Executor );
		const FWorldEditorModelPreviewKey RootKey = MakePreviewKey( { 7 } ); // root objectのpreview owner key
		const FWorldEditorModelPreviewKey SiblingKey = MakePreviewKey( { 8 } ); // 同sourceのnative sibling key
		const FWorldEditorModelPreviewKey FirstNestedKey = MakePreviewKey( { 11, 4, 7 } ); // host11下のleaf owner key
		const FWorldEditorModelPreviewKey SecondNestedKey = MakePreviewKey( { 12, 4, 7 } ); // host12下の同leaf owner key
		const FWorldEditorModelPreviewKey PendingKey = MakePreviewKey( { 9 } ); // 非同期model ownerの鍵
		const FWorldEditorModelPreviewKey MissingKey = MakePreviewKey( { 13 } ); // mask欠落model ownerの鍵
		const FWorldEditorModelPreviewKey DisabledKey = MakePreviewKey( { 14 } ); // effect無効でnative描画するowner key
		const FWorldEditorModelPreviewKey DisabledObjectKey = MakePreviewKey( { 15 } ); // object flag無効でownerを作らないkey
		const FWorldEditorModelPreviewKey AnimatedKey = MakePreviewKey( { 16 } ); // custom animation ownerの鍵
		const FWorldEditorModelPreview* PendingPreview = State.ModelPreviews.Find( PendingKey ); // 読込中previewの状態
		const FWorldEditorModelPreview* MissingPreview = State.ModelPreviews.Find( MissingKey ); // mask失敗previewの状態
		const FWorldEditorModelPreview* DisabledPreview = State.ModelPreviews.Find( DisabledKey ); // effect無効previewの状態
		const FWorldEditorModelPreview* AnimatedPreview = State.ModelPreviews.Find( AnimatedKey ); // 独自clip再生中のpreview状態
		Checker.Check( bFilesWritten && bWorldsSaved && bAnimationSaved && State.ModelPreviews.Num() == 8 && State.ModelPreviews.Contains( RootKey ) && State.ModelPreviews.Contains( SiblingKey ) && State.ModelPreviews.Contains( FirstNestedKey ) && State.ModelPreviews.Contains( SecondNestedKey ) && PendingPreview != nullptr && MissingPreview != nullptr && DisabledPreview != nullptr && AnimatedPreview != nullptr && !State.ModelPreviews.Contains( DisabledObjectKey ), "nested配置とdisabled bindingを個体ownerへ分け、object無効modelは巡回しない" );
		Checker.Check( PendingPreview != nullptr && !PendingPreview->bSourceReady && !PendingPreview->Animator.Instance.IsValid() && PendingPreview->FailureReason.Contains( "待っています" ) && !State.AssetInfo->GetModelInstance( PendingModelPath ).IsValid(), "pending sourceは専用instanceを作らず共有cacheも未準備instanceを返さない" );
		const int32 PendingHandle = PendingSource.IsValid() ? PendingSource->GetHandle() : -1; // 完了させるfake model handle
		if ( PendingHandle >= 0 ) Fake.Loading.erase( PendingHandle );
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.125f, &Executor );
		PendingPreview = State.ModelPreviews.Find( PendingKey );
		Checker.Check( PendingPreview != nullptr && PendingPreview->bSourceReady && PendingPreview->Animator.Instance.IsValid() && PendingPreview->Animator.GetMaterialEffect().IsPrepared(), "loading完了後は専用instanceと材質効果を準備する" );
		MissingPreview = State.ModelPreviews.Find( MissingKey );
		DisabledPreview = State.ModelPreviews.Find( DisabledKey );
		Checker.Check( DisabledPreview != nullptr && DisabledPreview->Animator.Instance.IsValid() && !DisabledPreview->Animator.GetMaterialEffect().IsPrepared() && DisabledPreview->FailureReason.IsEmpty() && Fake.NumTextureLoads == TextureLoadsBeforeWorldUpdate, "disabled bindingは専用native ownerを持つがmask/noise画像をloadしない" );
		Checker.Check( MissingPreview != nullptr && !MissingPreview->Animator.GetMaterialEffect().IsPrepared() && MissingPreview->FailureReason.Contains( "材質1" ) && Fake.NumTextureLoads == TextureLoadsBeforeWorldUpdate, "欠落maskは対象材質をfailureへ記録し、disabledと欠落画像はtexture loadしない" );
		AnimatedPreview = State.ModelPreviews.Find( AnimatedKey );
		const bool bAnimationSettingsApplied = AnimatedPreview != nullptr && AnimatedPreview->Animator.Instance.IsValid() && AnimatedPreview->Animator.GetCurrentClip() == "Custom" && std::fabs( AnimatedPreview->Animator.Speed - 1.25f ) < 1.0e-6f && AnimatedPreview->Animator.ModelOffset.x == 0.25f && AnimatedPreview->Animator.ModelOffset.y == -0.5f && AnimatedPreview->Animator.ModelOffset.z == 0.75f && AnimatedPreview->Animator.ModelScale.x == 1.5f && AnimatedPreview->Animator.ModelScale.y == 0.75f && AnimatedPreview->Animator.ModelScale.z == 2.0f && AnimatedPreview->Animator.Instance->GetNumAttached() > 0 && !AnimatedPreview->Animator.GetMaterialEffect().IsPrepared() && AnimatedPreview->FailureReason.Contains( "animation" ); // Clipと補正値、native fallbackを確かめる
		Checker.Check( bAnimationSettingsApplied, "custom animation clipの再生位置設定と非default補正を適用し効果はnativeへ戻す" );
		TMap<FWorldEditorModelPreviewKey, int32, FConstantPreviewKeyHasher> CollisionMap; // hashが同値になるkey map
		CollisionMap.Add( FirstNestedKey, 11 );
		CollisionMap.Add( SecondNestedKey, 12 );
		Checker.Check( FirstNestedKey.ObjectIds.Last() == 7 && SecondNestedKey.ObjectIds.Last() == 7 && !( FirstNestedKey == SecondNestedKey ) && CollisionMap.Num() == 2 && CollisionMap.FindRef( FirstNestedKey ) == 11 && CollisionMap.FindRef( SecondNestedKey ) == 12, "full owner keyは同じleaf Idを持つ二配置をhash衝突下でも区別する" );
		TSharedPtr<CModelInstance> CachedInstance = State.AssetInfo->GetModelInstance( ModelPath ); // AssetInfoが共有する通常instance
		const FWorldEditorModelPreview* RootPreview = State.ModelPreviews.Find( RootKey );
		const FWorldEditorModelPreview* FirstNestedPreview = State.ModelPreviews.Find( FirstNestedKey );
		const FWorldEditorModelPreview* SecondNestedPreview = State.ModelPreviews.Find( SecondNestedKey );
		const FWorldEditorModelPreview* SiblingPreview = State.ModelPreviews.Find( SiblingKey );
		const bool bUniqueInstances = CachedInstance.IsValid() && RootPreview != nullptr && FirstNestedPreview != nullptr && SecondNestedPreview != nullptr && SiblingPreview != nullptr && RootPreview->Animator.Instance.IsValid() && FirstNestedPreview->Animator.Instance.IsValid() && SecondNestedPreview->Animator.Instance.IsValid() && SiblingPreview->Animator.Instance.IsValid() && RootPreview->Animator.Instance->GetHandle() != CachedInstance->GetHandle() && FirstNestedPreview->Animator.Instance->GetHandle() != RootPreview->Animator.Instance->GetHandle() && SecondNestedPreview->Animator.Instance->GetHandle() != FirstNestedPreview->Animator.Instance->GetHandle() && RootPreview->Animator.Instance->GetSource().Get() == CachedInstance->GetSource().Get() && FirstNestedPreview->Animator.Instance->GetSource().Get() == CachedInstance->GetSource().Get() && SecondNestedPreview->Animator.Instance->GetSource().Get() == CachedInstance->GetSource().Get() && CachedInstance->GetNumAttached() == 0; // previewが共有instanceと分離している判定
		Checker.Check( bUniqueInstances, "previewはAssetInfoの共有instanceを変えずsourceだけ共有して個体instanceを分ける" );
		const double RootTimeAfterUpdate = RootPreview != nullptr ? RootPreview->Animator.GetMaterialEffect().GetEffectTime() : -1.0; // root owner update後clock
		const double FirstNestedTimeAfterUpdate = FirstNestedPreview != nullptr ? FirstNestedPreview->Animator.GetMaterialEffect().GetEffectTime() : -1.0; // 一つ目のprefab owner update後clock
		const double SecondNestedTimeAfterUpdate = SecondNestedPreview != nullptr ? SecondNestedPreview->Animator.GetMaterialEffect().GetEffectTime() : -1.0; // 二つ目のprefab owner update後clock
		const double PendingTimeAfterUpdate = PendingPreview != nullptr ? PendingPreview->Animator.GetMaterialEffect().GetEffectTime() : -1.0; // ready後のpending個体clock
		const int32 SurfaceQueriesBeforeRecord = Fake.NumStaticModelSurfaceQueries; // Record開始前のsource面照会累計
		const int32 GeometryCreatesBeforeRecord = Fake.NumModelMaterialGeometryCreates; // Record開始前のgeometry作成累計
		const int32 TextureLoadsBeforeRecord = Fake.NumTextureLoads; // Record開始前の画像load累計
		const int32 LoadingQueriesBeforeRecord = Fake.NumLoadingQueries; // Record中の非同期状態照会
		FDrawBuffer BufferA; // 1回目のworld record結果
		FDrawContext DrawA( BufferA ); // 1回目のrecord context
		NWorldEditorUI::RecordWorldScene( State, DrawA );
		FDrawBuffer ActiveOwnerBuffer; // 個体Record API単独呼出しの命令列
		FDrawContext ActiveOwnerDraw( ActiveOwnerBuffer ); // root ownerの直接record context
		const bool bActiveHandled = NWorldEditorUI::RecordModelObject( State, RootKey, World.Objects[0].Transform.ToTransform(), ActiveOwnerDraw ); // active個体のhandled結果
		FDrawBuffer NativeOwnerBuffer; // binding無しsiblingの直接命令列
		FDrawContext NativeOwnerDraw( NativeOwnerBuffer ); // native siblingの直接record context
		const bool bNativeHandled = NWorldEditorUI::RecordModelObject( State, SiblingKey, World.Objects[1].Transform.ToTransform(), NativeOwnerDraw ); // native個体のhandled結果
		FDrawBuffer MissingOwnerBuffer; // 未登録keyの命令列
		FDrawContext MissingOwnerDraw( MissingOwnerBuffer ); // 未登録keyのrecord context
		const bool bMissingHandled = NWorldEditorUI::RecordModelObject( State, MakePreviewKey( { 999 } ), FTransform::Identity, MissingOwnerDraw ); // 未登録ownerのhandled結果
		FDrawBuffer DisabledObjectBuffer; // object無効個体の空命令列
		FDrawContext DisabledObjectDraw( DisabledObjectBuffer ); // object無効keyのrecord context
		const bool bDisabledObjectHandled = NWorldEditorUI::RecordModelObject( State, DisabledObjectKey, FTransform::Identity, DisabledObjectDraw ); // owner無し個体のhandled結果
		FWorldEditorModelPreviewKey EmptyOwnerKey; // path要素のない不正key
		FDrawBuffer EmptyOwnerBuffer; // empty keyの命令列
		FDrawContext EmptyOwnerDraw( EmptyOwnerBuffer ); // empty keyのrecord context
		const bool bEmptyHandled = NWorldEditorUI::RecordModelObject( State, EmptyOwnerKey, FTransform::Identity, EmptyOwnerDraw ); // empty keyのhandled結果
		Checker.Check( bActiveHandled && CountPreviewCommands( ActiveOwnerBuffer, EDrawCommand::ModelMaterial3D ) == 1 && CountPreviewCommands( ActiveOwnerBuffer, EDrawCommand::Model3D ) == 0 && bNativeHandled && CountPreviewCommands( NativeOwnerBuffer, EDrawCommand::Model3D ) == 1 && CountPreviewCommands( NativeOwnerBuffer, EDrawCommand::ModelMaterial3D ) == 0 && !bMissingHandled && MissingOwnerBuffer.Words.empty() && !bDisabledObjectHandled && DisabledObjectBuffer.Words.empty() && !bEmptyHandled && EmptyOwnerBuffer.Words.empty(), "個体Recordはactive/nativeをhandledで一命令にし未登録・empty keyを拒否する" );
		FDrawBuffer BufferB; // 反復Recordとの比較結果
		FDrawContext DrawB( BufferB ); // 反復Record context
		NWorldEditorUI::RecordWorldScene( State, DrawB );
		const FWorldEditorModelPreview* RootAfterRecord = State.ModelPreviews.Find( RootKey );
		const FWorldEditorModelPreview* FirstNestedAfterRecord = State.ModelPreviews.Find( FirstNestedKey );
		const FWorldEditorModelPreview* SecondNestedAfterRecord = State.ModelPreviews.Find( SecondNestedKey );
		const FWorldEditorModelPreview* PendingAfterRecord = State.ModelPreviews.Find( PendingKey ); // Record後のready owner
		const bool bRecordIsPure = BufferA.Words == BufferB.Words && RootAfterRecord != nullptr && FirstNestedAfterRecord != nullptr && SecondNestedAfterRecord != nullptr && PendingAfterRecord != nullptr && std::fabs( RootAfterRecord->Animator.GetMaterialEffect().GetEffectTime() - RootTimeAfterUpdate ) < 1.0e-6 && std::fabs( FirstNestedAfterRecord->Animator.GetMaterialEffect().GetEffectTime() - FirstNestedTimeAfterUpdate ) < 1.0e-6 && std::fabs( SecondNestedAfterRecord->Animator.GetMaterialEffect().GetEffectTime() - SecondNestedTimeAfterUpdate ) < 1.0e-6 && std::fabs( PendingAfterRecord->Animator.GetMaterialEffect().GetEffectTime() - PendingTimeAfterUpdate ) < 1.0e-6 && Fake.NumStaticModelSurfaceQueries == SurfaceQueriesBeforeRecord && Fake.NumModelMaterialGeometryCreates == GeometryCreatesBeforeRecord && Fake.NumTextureLoads == TextureLoadsBeforeRecord && Fake.NumLoadingQueries == LoadingQueriesBeforeRecord; // 二回のRecordとowner状態が同じか
		Checker.Check( bRecordIsPure, "RecordWorldScene反復はpreview時計・surface query・geometry作成・texture loadを変えない" );
		const int32 GeometryBeforeStableUpdate = Fake.NumModelMaterialGeometryCreates; // 同一構成Update前の作成数
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.0f, &Executor );
		Checker.Check( Fake.NumModelMaterialGeometryCreates == GeometryBeforeStableUpdate, "同じsource・binding・executorではUpdateを重ねてもeffect geometryを再作成しない" );
		const FWorldEditorModelPreview* BeforeWorkerUpdate = State.ModelPreviews.Find( RootKey ); // worker guard前のroot preview
		const double TimeBeforeWorker = BeforeWorkerUpdate != nullptr ? BeforeWorkerUpdate->Animator.GetMaterialEffect().GetEffectTime() : -1.0; // worker guard前のclock
		const int32 QueriesBeforeWorker = Fake.NumStaticModelSurfaceQueries; // worker guard前のsurface問い合わせ数
		const int32 CreatesBeforeWorker = Fake.NumModelMaterialGeometryCreates; // worker guard前のgeometry作成数
		const int32 LoadsBeforeWorker = Fake.NumTextureLoads; // worker guard前のtexture load数
		const int32 LoadingQueriesBeforeWorker = Fake.NumLoadingQueries; // worker guard前のloading照会数
		std::thread WorkerUpdate( [&State, &Executor]() { NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.125f, &Executor ); } ); // worker threadからの禁止Update
		WorkerUpdate.join();
		const FWorldEditorModelPreview* AfterWorkerUpdate = State.ModelPreviews.Find( RootKey ); // worker guard後のroot preview
		const bool bWorkerUpdateIgnored = State.ModelPreviews.Num() == 8 && AfterWorkerUpdate != nullptr && std::fabs( AfterWorkerUpdate->Animator.GetMaterialEffect().GetEffectTime() - TimeBeforeWorker ) < 1.0e-9 && Fake.NumStaticModelSurfaceQueries == QueriesBeforeWorker && Fake.NumModelMaterialGeometryCreates == CreatesBeforeWorker && Fake.NumTextureLoads == LoadsBeforeWorker && Fake.NumLoadingQueries == LoadingQueriesBeforeWorker; // worker updateでstate/resourceが不変か
		Checker.Check( bWorkerUpdateIgnored, "worker threadのpreview Updateはmap・clock・model resourceを変えない" );
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.125f, &Executor );
		const FWorldEditorModelPreviewKey PreviewKeys[3] = { RootKey, FirstNestedKey, SecondNestedKey }; // world行列を照合する三owner key
		const float32 ExpectedWorldX[3] = { -10.0f, 15.0f, 35.0f }; // 配置値から独立に決めたworld x
		bool bModelPayloadsMatch = true; // payload matrixが配置値と一致したか
		for ( int32 Index = 0; Index < 3; ++Index ) // 各root/prefab ownerの検査位置
		{
			const FWorldEditorModelPreview* Preview = State.ModelPreviews.Find( PreviewKeys[Index] );
			FDrawModelMaterialData Payload{}; // 現在ownerのModelMaterial payload
			const bool bFound = Preview != nullptr && Preview->Animator.Instance.IsValid() && FindPreviewCommand( BufferA, Preview->Animator.Instance->GetHandle(), Payload ); // 指定instanceのcommandが記録されたか
			bModelPayloadsMatch = bModelPayloadsMatch && bFound && std::fabs( Payload.M[9] - ExpectedWorldX[Index] ) < 0.001f && std::fabs( Payload.M[10] ) < 0.001f;
		}
		Checker.Check( bModelPayloadsMatch && CountPreviewCommands( BufferA, EDrawCommand::ModelMaterial3D ) == 4 && CountPreviewCommands( BufferA, EDrawCommand::Model3D ) == 4, "実world記録は個体別handleと既知位置を使いnative siblingと失敗fallbackを残す" );
		FWorldEditorModelPreview* FirstPaused = State.ModelPreviews.Find( FirstNestedKey );
		if ( FirstPaused != nullptr ) FirstPaused->Animator.GetMaterialEffect().Pause();
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.2f, &Executor );
		FWorldEditorModelPreview* RootAfterPause = State.ModelPreviews.Find( RootKey );
		const FWorldEditorModelPreview* FirstAfterPause = State.ModelPreviews.Find( FirstNestedKey );
		const FWorldEditorModelPreview* SecondAfterPause = State.ModelPreviews.Find( SecondNestedKey );
		const bool bIndependentClocks = RootAfterPause != nullptr && FirstAfterPause != nullptr && SecondAfterPause != nullptr && std::fabs( RootAfterPause->Animator.GetMaterialEffect().GetEffectTime() - 0.575 ) < 1.0e-5 && std::fabs( FirstAfterPause->Animator.GetMaterialEffect().GetEffectTime() - 0.375 ) < 1.0e-5 && std::fabs( SecondAfterPause->Animator.GetMaterialEffect().GetEffectTime() - 0.575 ) < 1.0e-5; // pauseした一個体だけ時計を保つか
		Checker.Check( bIndependentClocks, "個体Updateは各clockを一度進め、Pauseした同Prefabの片方だけを保持する" );
		if ( RootAfterPause != nullptr ) RootAfterPause->Animator.GetMaterialEffect().Stop();
		FDrawBuffer StoppedBuffer; // Stop後の混在native/effect command列
		FDrawContext StoppedDraw( StoppedBuffer ); // Stop後Record context
		NWorldEditorUI::RecordWorldScene( State, StoppedDraw );
		const FWorldEditorModelPreview* StoppedPreview = State.ModelPreviews.Find( RootKey );
		const bool bStopUsesNative = StoppedPreview != nullptr && StoppedPreview->Animator.Instance.IsValid() && CountPreviewCommands( StoppedBuffer, EDrawCommand::Model3D ) == 5 && CountPreviewCommands( StoppedBuffer, EDrawCommand::ModelMaterial3D ) == 3; // Stop ownerがnativeへ戻ったworld command数
		FDrawBuffer StoppedOwnerBuffer; // Stop owner単独のfallback命令列
		FDrawContext StoppedOwnerDraw( StoppedOwnerBuffer ); // Stop ownerの直接record context
		const bool bStoppedHandled = NWorldEditorUI::RecordModelObject( State, RootKey, World.Objects[0].Transform.ToTransform(), StoppedOwnerDraw ); // Stop後ownerのhandled結果
		Checker.Check( bStopUsesNative && bStoppedHandled && CountPreviewCommands( StoppedOwnerBuffer, EDrawCommand::Model3D ) == 1 && CountPreviewCommands( StoppedOwnerBuffer, EDrawCommand::ModelMaterial3D ) == 0, "Stopしたownerはhandledのnative命令へ戻り他のpreview効果は続く" );
		const FWorldEditorModelPreview* BeforeExecutorSwitch = State.ModelPreviews.Find( FirstNestedKey ); // pointer切替前のactive個体
		FDrawModelMaterialData BeforeExecutorPayload{}; // 切替前に解放を追跡するgeometry handle
		const int32 GeometryBeforeExecutorSwitch = BeforeExecutorSwitch != nullptr && BeforeExecutorSwitch->Animator.Instance.IsValid() && FindPreviewCommand( BufferA, BeforeExecutorSwitch->Animator.Instance->GetHandle(), BeforeExecutorPayload ) ? BeforeExecutorPayload.Geometry : -1; // 旧executorが所有するgeometry
		const int32 CreatesBeforeExecutorSwitch = Fake.NumModelMaterialGeometryCreates; // 別executor再準備前の作成数
		FModelPreviewTestExecutor AlternateExecutor; // 対応能力は同じ別executor identity
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.0f, &AlternateExecutor );
		const FWorldEditorModelPreview* AfterExecutorSwitch = State.ModelPreviews.Find( FirstNestedKey ); // pointer切替後のactive個体
		const bool bExecutorIdentityRebuilt = AfterExecutorSwitch != nullptr && AfterExecutorSwitch->Executor == &AlternateExecutor && AfterExecutorSwitch->Animator.GetMaterialEffect().IsPrepared() && Fake.NumModelMaterialGeometryCreates == CreatesBeforeExecutorSwitch + 4 && ( GeometryBeforeExecutorSwitch < 0 || std::find( Fake.DeletedModelMaterialGeometries.begin(), Fake.DeletedModelMaterialGeometries.end(), GeometryBeforeExecutorSwitch ) != Fake.DeletedModelMaterialGeometries.end() ); // 同能力の別pointerだけでactive owner群を再準備
		Checker.Check( bExecutorIdentityRebuilt, "能力が同じ別executor pointerへの切替でgeometryを再準備し旧handleを解放する" );
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.0f, &Executor );
		Executor.bSupports = false; // backend capabilityが失われたframeを模擬
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.0f, &Executor );
		FDrawBuffer UnsupportedBuffer; // 非対応時のnative fallback命令
		FDrawContext UnsupportedDraw( UnsupportedBuffer );
		NWorldEditorUI::RecordWorldScene( State, UnsupportedDraw );
		const FWorldEditorModelPreview* UnsupportedPreview = State.ModelPreviews.Find( FirstNestedKey ); // 能力喪失後のpreview状態
		const bool bUnsupportedFallback = UnsupportedPreview != nullptr && !UnsupportedPreview->Animator.GetMaterialEffect().IsPrepared() && UnsupportedPreview->FailureReason.Contains( "対応していません" ) && CountPreviewCommands( UnsupportedBuffer, EDrawCommand::ModelMaterial3D ) == 0 && CountPreviewCommands( UnsupportedBuffer, EDrawCommand::Model3D ) == 8; // 対応能力喪失後のfallback状態
		Checker.Check( bUnsupportedFallback, "executor能力が失われたframeは全model個体をnative fallbackへ戻す" );
		Executor.bSupports = true; // 対応能力は戻すがnull executorは別経路で検査
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.0f, nullptr );
		FDrawBuffer NullExecutorBuffer; // executorがないときのnative command列
		FDrawContext NullExecutorDraw( NullExecutorBuffer ); // executorなし時のrecord context
		NWorldEditorUI::RecordWorldScene( State, NullExecutorDraw );
		const FWorldEditorModelPreview* NullExecutorPreview = State.ModelPreviews.Find( RootKey ); // null executor後のroot状態
		Checker.Check( NullExecutorPreview != nullptr && NullExecutorPreview->FailureReason.Contains( "実行先" ) && CountPreviewCommands( NullExecutorBuffer, EDrawCommand::ModelMaterial3D ) == 0 && CountPreviewCommands( NullExecutorBuffer, EDrawCommand::Model3D ) == 8, "null executorではeffect準備を行わず全modelをnative描画する" );
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.0f, &Executor );

		// 同じobjectのopacity変更だけで古いgeometryを解放し再準備する。
		const int32 GeometryBeforeEdit = Fake.NumModelMaterialGeometryCreates; // binding編集前のgeometry作成累計
		const FWorldEditorModelPreview* BeforeEdit = State.ModelPreviews.Find( RootKey );
		FDrawModelMaterialData BeforeEditPayload{}; // 編集前に参照するcommand payload
		FDrawBuffer BeforeEditBuffer; // 現在のgeometry handleを取得する命令列
		FDrawContext BeforeEditDraw( BeforeEditBuffer );
		NWorldEditorUI::RecordWorldScene( State, BeforeEditDraw );
		const int32 OldGeometry = BeforeEdit != nullptr && BeforeEdit->Animator.Instance.IsValid() && FindPreviewCommand( BeforeEditBuffer, BeforeEdit->Animator.Instance->GetHandle(), BeforeEditPayload ) ? BeforeEditPayload.Geometry : -1; // root ownerが編集前に使っていたgeometry
		FWorldDefinition ChangedWorld = State.Session.Document.Get(); // root binding一つだけを変更するworld copy
		FWorldObject* ChangedLeaf = ChangedWorld.FindObject( 7 ); // root ownerの変更対象object
		if ( ChangedLeaf != nullptr && ChangedLeaf->Model.IsSet() && ChangedLeaf->Model.GetValue().MaterialEffect.IsSet() ) ChangedLeaf->Model.GetValue().MaterialEffect.GetValue().Effect.Opacity = 0.61f;
		State.Session.Reset( ChangedWorld, "model_preview_owners.world" );
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.0f, &Executor );
		const FWorldEditorModelPreview* ChangedRoot = State.ModelPreviews.Find( RootKey );
		const FWorldEditorModelPreview* ChangedFirstNested = State.ModelPreviews.Find( FirstNestedKey );
		const FWorldEditorModelPreview* ChangedSecondNested = State.ModelPreviews.Find( SecondNestedKey );
		const bool bChangedDefinitionRebuilt = ChangedRoot != nullptr && ChangedFirstNested != nullptr && ChangedSecondNested != nullptr && ChangedRoot->Animator.GetMaterialEffect().IsPrepared() && ChangedFirstNested->Animator.GetMaterialEffect().IsPrepared() && ChangedSecondNested->Animator.GetMaterialEffect().IsPrepared() && Fake.NumModelMaterialGeometryCreates == GeometryBeforeEdit + 1; // binding値変更だけでrootを再準備したか
		Checker.Check( bChangedDefinitionRebuilt && ( OldGeometry < 0 || std::find( Fake.DeletedModelMaterialGeometries.begin(), Fake.DeletedModelMaterialGeometries.end(), OldGeometry ) != Fake.DeletedModelMaterialGeometries.end() ), "sourceとexecutorを保ったbinding変更だけで対象ownerを再準備し古いgeometryを解放する" );

		// 失敗設定はnativeへ戻し、同じ失敗hashで毎frame再試行しない。
		FWorldDefinition InvalidWorld = State.Session.Document.Get(); // 同じownerでinvalid設定を試すworld copy
		FWorldObject* InvalidLeaf = InvalidWorld.FindObject( 7 ); // NaN opacityを設定するroot object
		if ( InvalidLeaf != nullptr && InvalidLeaf->Model.IsSet() && InvalidLeaf->Model.GetValue().MaterialEffect.IsSet() ) InvalidLeaf->Model.GetValue().MaterialEffect.GetValue().Effect.Opacity = std::numeric_limits<float32>::quiet_NaN();
		State.Session.Reset( InvalidWorld, "model_preview_invalid.world" );
		const int32 SurfaceBeforeInvalid = Fake.NumStaticModelSurfaceQueries; // invalid setting処理前の面照会累計
		const int32 GeometryBeforeInvalid = Fake.NumModelMaterialGeometryCreates; // invalid setting処理前のgeometry作成累計
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.0f, &Executor );
		FDrawBuffer InvalidBuffer; // invalid設定後のnative fallback commands
		FDrawContext InvalidDraw( InvalidBuffer ); // invalid設定worldのrecord context
		NWorldEditorUI::RecordWorldScene( State, InvalidDraw );
		const int32 SurfaceAfterInvalid = Fake.NumStaticModelSurfaceQueries; // invalid設定record直後の面照会累計
		const int32 GeometryAfterInvalid = Fake.NumModelMaterialGeometryCreates; // invalid設定record直後のgeometry作成累計
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.0f, &Executor );
		const FWorldEditorModelPreview* InvalidPreview = State.ModelPreviews.Find( RootKey );
		Checker.Check( InvalidPreview != nullptr && !InvalidPreview->Animator.GetMaterialEffect().IsPrepared() && !InvalidPreview->FailureReason.IsEmpty() && CountPreviewCommands( InvalidBuffer, EDrawCommand::Model3D ) == 5 && CountPreviewCommands( InvalidBuffer, EDrawCommand::ModelMaterial3D ) == 3 && Fake.NumStaticModelSurfaceQueries == SurfaceAfterInvalid && Fake.NumModelMaterialGeometryCreates == GeometryAfterInvalid && SurfaceAfterInvalid >= SurfaceBeforeInvalid && GeometryAfterInvalid >= GeometryBeforeInvalid, "invalid設定は対象だけnativeへ戻り同じfailure hashを繰返しqueryしない" );

		// rootを切替えて同じ相対pathの別sourceへpreview ownerを作り直す。
		const FString AlternateRoot = Directory / "preview_model_source_reload"; // 同じ相対model pathを置く別asset root
		const bool bAlternateFixtureWritten = NFileSystem::EnsureDirectory( AlternateRoot ) && NAssetTest::WriteFileText( AlternateRoot / ModelPath, "alternate fake static model" ); // 別source fixtureの書込成否
		FWorldDefinition ReloadWorld; // 変更しない相対pathのroot-only world
		ReloadWorld.NextId = 8;
		ReloadWorld.Objects.Add( MakePreviewModelObject( 7, ModelPath, &Binding ) );
		State.Session.Reset( ReloadWorld, "model_preview_reload.world" );
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.0f, &Executor );
		const FWorldEditorModelPreview* BeforeSourceReload = State.ModelPreviews.Find( RootKey ); // 元rootで準備済みのowner
		const CModel* OldSourceAddress = BeforeSourceReload != nullptr ? BeforeSourceReload->Source.Get() : nullptr; // 入替前sourceの識別値
		const int32 OldInstanceHandle = BeforeSourceReload != nullptr && BeforeSourceReload->Animator.Instance.IsValid() ? BeforeSourceReload->Animator.Instance->GetHandle() : -1; // 入替前専用instance handle
		Assets.SetRootDirectory( AlternateRoot );
		State.AssetInfo->Clear();
		const int32 GeometryBeforeSourceReload = Fake.NumModelMaterialGeometryCreates; // source差し替え前のgeometry作成数
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.0f, &Executor );
		const FWorldEditorModelPreview* AfterSourceReload = State.ModelPreviews.Find( RootKey ); // 新rootから再取得したowner
		const CModel* AlternateSourceAddress = State.AssetInfo->GetModelSource( ModelPath ).Get(); // cacheが返す新sourceの識別値
		const bool bSamePathSourceReloaded = bAlternateFixtureWritten && AfterSourceReload != nullptr && AfterSourceReload->Source.IsValid() && AlternateSourceAddress != nullptr && AfterSourceReload->Definition.Path == ModelPath && AfterSourceReload->Source.Get() == AlternateSourceAddress && AfterSourceReload->Source.Get() != OldSourceAddress && AfterSourceReload->Animator.Instance.IsValid() && AfterSourceReload->Animator.Instance->GetHandle() != OldInstanceHandle && AfterSourceReload->Animator.GetMaterialEffect().IsPrepared() && Fake.NumModelMaterialGeometryCreates == GeometryBeforeSourceReload + 1; // 同pathのsource差替え後のownerとgeometry状態
		Checker.Check( bSamePathSourceReloaded, "同じrelative pathでもasset rootのsource差し替えを検知して個体とgeometryを再構築する" );
		Assets.SetRootDirectory( Directory );
		State.AssetInfo->Clear();
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.0f, &Executor );
		const FWorldEditorModelPreview* RestoredPreview = State.ModelPreviews.Find( RootKey ); // 元rootから作り直したpreview owner
		const bool bOriginalSourceRestored = CachedInstance.IsValid() && CachedInstance->GetSource().IsValid() && RestoredPreview != nullptr && RestoredPreview->Source.Get() == CachedInstance->GetSource().Get(); // cached sourceとpreviewの一致
		Checker.Check( bOriginalSourceRestored, "元asset rootへ戻すと同じworld definitionから元source ownerを再取得する" );

		// owner mapを空のsceneでpruneし、AssetInfoはsourceと借用画像を保持する。
		TWeakPtr<CModel> SourceWeak = CachedInstance.IsValid() ? TWeakPtr<CModel>( CachedInstance->GetSource() ) : TWeakPtr<CModel>();
		const int32 GeometryBeforePrune = static_cast<int32>( Fake.ModelMaterialGeometries.size() ); // prune前に残るgeometry数
		State.Session.Reset( FWorldDefinition(), FString() );
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.0f, &Executor );
		Checker.Check( State.ModelPreviews.IsEmpty() && static_cast<int32>( Fake.ModelMaterialGeometries.size() ) < GeometryBeforePrune && SourceWeak.IsValid(), "pruneは専用instanceとeffect geometryを解放しAssetInfo共有sourceを維持する" );
		State.AssetInfo->Clear();
		CachedInstance.Reset();
		Diffuse.Reset();
		PendingSource.Reset();
		State.AssetInfo.Reset();
		Assets.ReleaseUnused();
		Assets.Tick( 0.0f );
		const bool bFakeOwnersRestored = Fake.Models == OldModels && Fake.Textures == OldTextures && Fake.Loading == OldLoading && SamePreviewGeometryMaps( Fake.ModelMaterialGeometries, OldGeometries ); // cleanup後のlive handle集合
		Checker.Check( !SourceWeak.IsValid() && bFakeOwnersRestored, "AssetInfo clear後にsource・画像・preview geometryの追加ownerがすべて解放される" );
		} while ( false );

		State.ModelPreviews.Empty();
		if ( State.AssetInfo.IsValid() ) State.AssetInfo->Clear();
		State.AssetInfo.Reset();
		Diffuse.Reset();
		PendingSource.Reset();
		Assets.ReleaseUnused();
		Assets.Tick( 0.0f );
		Assets.SetRootDirectory( OldRoot );
		Fake.bSupportStaticModelSurface = bOldSurfaceSupport;
		Fake.StaticModelSurfaceData = OldSurface;
		Fake.NextModelData = OldModelData;
		Fake.bSupportModelMaterialGeometry = bOldGeometrySupport;
		Fake.FailModelMaterialGeometryCreateCall = OldFailGeometry;
		Fake.FailTextureLoadCall = OldFailTexture;
		Fake.Next = OldNext;
		Fake.NextModelMaterialGeometry = OldNextGeometry;
		Fake.NumTextureLoads = OldTextureLoads;
		Fake.NumLoadingQueries = OldLoadingQueries;
		Fake.NumDuplicates = OldDuplicates;
		Fake.NumStaticModelSurfaceQueries = OldSurfaceQueries;
		Fake.NumModelMaterialGeometryCreates = OldGeometryCreates;
		Fake.Models = OldModels;
		Fake.Textures = OldTextures;
		Fake.ModelMaterialGeometries = OldGeometries;
		Fake.DeletedModelMaterialGeometries = OldDeletedGeometries;
		Fake.DeletedTextures = OldDeletedTextures;
		Fake.Loading = OldLoading;
		Checker.Group = OldGroup;
	}

}
