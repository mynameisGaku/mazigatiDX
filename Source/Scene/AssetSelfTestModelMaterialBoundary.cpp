// SPDX-License-Identifier: NOASSERTION
#include "AssetSelfTestModelMaterialBoundary.h"

#include <cmath>
#include <limits>
#include <thread>

#include "AssetSelfTestSupport.h"
#include "Library/Core/Interfaces/Render/DrawModelMaterial.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/Model.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/Render/ModelMaterialEffectRenderer.h"
#include "Library/Platform/DxLib/DxLibAssetBackend.h"

namespace NAssetTest
{
	namespace
	{
		// 境界試験で使うloop clockと独立した二組のUV速度を作る。
		FMeshMaterialEffectDefinition MakeBoundaryDefinition( bool bLoop )
		{
			FMeshMaterialEffectDefinition Definition;
			Definition.Material.bEnabled = true;
			Definition.Material.UVScroll = FVector2( 0.25f, -0.125f );
			Definition.Material.Distortion.UVScroll = FVector2( 0.375f, -0.2f );
			Definition.Duration = 1.25f;
			Definition.bLoop = bLoop;
			return Definition;
		}

		// float32の速度と秒数をfloat64へ広げ、床関数で独立にUV位相を求める。
		float64 ExpectedBoundaryUVPhase( float32 ScrollSpeed, float32 DeltaTime )
		{
			// 保存済みfloat32値を含めた経過位相。
			const float64 TotalPhase = static_cast<float64>( ScrollSpeed ) * static_cast<float64>( DeltaTime );
			return TotalPhase - std::floor( TotalPhase );
		}

		// 効果能力だけを公開する、記録命令を実行しないfake executor。
		class FBoundaryExecutor final : public IDrawExecutor
		{
		public:
			void Execute( const FDrawSpan& Span ) override { (void)Span; }
			bool SupportsModelMaterialEffects() const override { return true; }
		};

		// 入力位置から識別できる色・UV・法線を持つ頂点を作る。
		FMeshVertex MakeBoundaryVertex( float32 X, uint8 Red, uint8 Green, uint8 Blue )
		{
			FMeshVertex Vertex;
			Vertex.Position[0] = X;
			Vertex.Position[1] = X + 0.5f;
			Vertex.Position[2] = -X;
			Vertex.Normal[0] = 0.25f;
			Vertex.Normal[1] = 0.5f;
			Vertex.Normal[2] = 0.75f;
			Vertex.U = X * 0.125f;
			Vertex.V = -X * 0.25f;
			Vertex.Color[0] = Red;
			Vertex.Color[1] = Green;
			Vertex.Color[2] = Blue;
			Vertex.Color[3] = 211;
			return Vertex;
		}

		// 二材質の連続sectionを持つ小さなbackend入力を作る。
		FStaticModelSurfaceData MakeBoundarySurface()
		{
			FStaticModelSurfaceData Surface;
			Surface.Vertices.Add( MakeBoundaryVertex( 1.0f, 19, 31, 47 ) );
			Surface.Vertices.Add( MakeBoundaryVertex( 2.0f, 59, 71, 83 ) );
			Surface.Vertices.Add( MakeBoundaryVertex( 3.0f, 97, 109, 121 ) );
			Surface.Vertices.Add( MakeBoundaryVertex( 4.0f, 137, 149, 163 ) );
			Surface.Vertices.Add( MakeBoundaryVertex( 5.0f, 173, 187, 199 ) );
			Surface.Vertices.Add( MakeBoundaryVertex( 6.0f, 211, 223, 239 ) );
			Surface.Indices = { 0, 1, 2, 3, 4, 5 };
			FStaticModelSurfaceMaterial RedMaterial;
			RedMaterial.Diffuse[0] = 0.2f;
			RedMaterial.Diffuse[1] = 0.4f;
			RedMaterial.Diffuse[2] = 0.6f;
			RedMaterial.Diffuse[3] = 0.8f;
			RedMaterial.BorrowedDiffuseTexture = 7811;
			Surface.Materials.Add( RedMaterial );
			FStaticModelSurfaceMaterial BlueMaterial;
			BlueMaterial.Diffuse[0] = 0.7f;
			BlueMaterial.Diffuse[1] = 0.5f;
			BlueMaterial.Diffuse[2] = 0.3f;
			BlueMaterial.Diffuse[3] = 0.9f;
			BlueMaterial.BorrowedDiffuseTexture = 7812;
			Surface.Materials.Add( BlueMaterial );
			Surface.Materials.Add( FStaticModelSurfaceMaterial() );
			FStaticModelSurfaceSection FirstSection;
			FirstSection.FirstIndex = 0;
			FirstSection.NumIndices = 3;
			FirstSection.MaterialIndex = 0;
			FirstSection.SourceFrameIndex = 4;
			FirstSection.SourceMeshIndex = 7;
			Surface.Sections.Add( FirstSection );
			FStaticModelSurfaceSection SecondSection;
			SecondSection.FirstIndex = 3;
			SecondSection.NumIndices = 3;
			SecondSection.MaterialIndex = 1;
			SecondSection.SourceFrameIndex = 5;
			SecondSection.SourceMeshIndex = 8;
			Surface.Sections.Add( SecondSection );
			return Surface;
		}

		// 全頂点値が一致するかを、頂点色も含めて比べる。
		bool SameBoundaryVertex( const FMeshVertex& A, const FMeshVertex& B )
		{
			for ( int32 Axis = 0; Axis < 3; ++Axis ) if ( A.Position[Axis] != B.Position[Axis] || A.Normal[Axis] != B.Normal[Axis] ) return false;
			return A.U == B.U && A.V == B.V && A.Color[0] == B.Color[0] && A.Color[1] == B.Color[1] && A.Color[2] == B.Color[2] && A.Color[3] == B.Color[3];
		}

		// backend geometryのmesh対応・全chunk値が同じかを調べる。
		bool SameBoundaryGeometry( const FDxLibModelMaterialGeometry& A, const FDxLibModelMaterialGeometry& B )
		{
			if ( A.Meshes.Num() != B.Meshes.Num() ) return false;
			for ( int32 MeshIndex = 0; MeshIndex < A.Meshes.Num(); ++MeshIndex )
			{
				const FDxLibModelMaterialGeometry::FMesh& LeftMesh = A.Meshes[MeshIndex];
				const FDxLibModelMaterialGeometry::FMesh& RightMesh = B.Meshes[MeshIndex];
				if ( LeftMesh.SourceMeshIndex != RightMesh.SourceMeshIndex || LeftMesh.SourceFrameIndex != RightMesh.SourceFrameIndex || LeftMesh.MaterialIndex != RightMesh.MaterialIndex || LeftMesh.Chunks.Num() != RightMesh.Chunks.Num() ) return false;
				for ( int32 ChunkIndex = 0; ChunkIndex < LeftMesh.Chunks.Num(); ++ChunkIndex )
				{
					const FMeshGeometryChunk& LeftChunk = LeftMesh.Chunks[ChunkIndex];
					const FMeshGeometryChunk& RightChunk = RightMesh.Chunks[ChunkIndex];
					if ( LeftChunk.Vertices.Num() != RightChunk.Vertices.Num() || LeftChunk.Indices != RightChunk.Indices ) return false;
					for ( int32 VertexIndex = 0; VertexIndex < LeftChunk.Vertices.Num(); ++VertexIndex ) if ( !SameBoundaryVertex( LeftChunk.Vertices[VertexIndex], RightChunk.Vertices[VertexIndex] ) ) return false;
				}
			}
			return true;
		}

		// backendが一材質だけを元値のまま保持したか確認する。
		bool HasExpectedSmallGeometry( const FDxLibModelMaterialGeometry* Geometry, const FStaticModelSurfaceData& Surface, int32 MaterialIndex )
		{
			if ( Geometry == nullptr || !Surface.Sections.IsValidIndex( MaterialIndex ) || Geometry->Meshes.Num() != 1 || Geometry->Meshes[0].Chunks.Num() != 1 ) return false;
			const FStaticModelSurfaceSection& Section = Surface.Sections[MaterialIndex];
			const FMeshGeometryChunk& Chunk = Geometry->Meshes[0].Chunks[0];
			if ( Geometry->Meshes[0].SourceFrameIndex != Section.SourceFrameIndex || Geometry->Meshes[0].SourceMeshIndex != Section.SourceMeshIndex || Geometry->Meshes[0].MaterialIndex != Section.MaterialIndex || Chunk.Vertices.Num() != 3 || Chunk.Indices.Num() != 3 ) return false;
			for ( int32 Index = 0; Index < 3; ++Index ) if ( Chunk.Indices[Index] != static_cast<uint32>( Index ) || !SameBoundaryVertex( Chunk.Vertices[Index], Surface.Vertices[static_cast<int32>( Surface.Indices[Section.FirstIndex + Index] )] ) ) return false;
			return true;
		}

		// 無効入力が既存geometryを保ち、handleを消費しないことを確認する。
		void CheckRejectedSurface( FChecker& Checker, CDxLibAssetBackend& Backend, const FStaticModelSurfaceData& Surface, int32 MaterialIndex, int32 ExpectedCount, int32 FirstHandle, int32 SecondHandle, const FDxLibModelMaterialGeometry& FirstBefore, const FDxLibModelMaterialGeometry& SecondBefore, const char* CaseName )
		{
			const int32 RejectedHandle = Backend.CreateModelMaterialGeometry( Surface, MaterialIndex );
			const bool bRejected = RejectedHandle < 0;
			// 既存handleを誤って返す変異時に、試験fixtureの資源を消さない。
			if ( RejectedHandle >= 0 && RejectedHandle != FirstHandle && RejectedHandle != SecondHandle ) Backend.DeleteModelMaterialGeometry( RejectedHandle );
			const FDxLibModelMaterialGeometry* FirstAfter = Backend.FindModelMaterialGeometry( FirstHandle );
			const FDxLibModelMaterialGeometry* SecondAfter = Backend.FindModelMaterialGeometry( SecondHandle );
			const bool bExistingGeometryUnchanged = FirstAfter != nullptr && SecondAfter != nullptr && SameBoundaryGeometry( *FirstAfter, FirstBefore ) && SameBoundaryGeometry( *SecondAfter, SecondBefore );
			Checker.Check( bRejected && Backend.GetNumModelMaterialGeometries() == ExpectedCount && bExistingGeometryUnchanged, "%s: 不正な面を原子的に拒否し既存handleを保つ", CaseName );
		}
	}

	void RunStaticModelMaterialBoundaryFakeChecks( FChecker& Checker, CAssetManager& Assets, FFakeAssetBackend& Fake, const FString& Directory )
	{
		Checker.Group = "model-material-boundary-fake";
		// 既存fake backend設定をこのfixtureの終了後に戻す値。
		const bool OldSurfaceSupport = Fake.bSupportStaticModelSurface;
		const bool OldGeometrySupport = Fake.bSupportModelMaterialGeometry;
		const FStaticModelSurfaceData OldSurface = Fake.StaticModelSurfaceData;
		const FFakeAssetBackend::FModelData OldNextModelData = Fake.NextModelData;
		const std::set<int32> OldLoading = Fake.Loading;
		const int32 OldSurfaceQueries = Fake.NumStaticModelSurfaceQueries;
		const int32 OldGeometryCreates = Fake.NumModelMaterialGeometryCreates;
		const int32 OldTextureLoads = Fake.NumTextureLoads;
		const int32 OldLoadingQueries = Fake.NumLoadingQueries;
		const int32 OldNextGeometry = Fake.NextModelMaterialGeometry;
		const size_t OldDeletedGeometryCount = Fake.DeletedModelMaterialGeometries.size();
		const size_t OldLoadedPathCount = Fake.LoadedModelPaths.size();

		// 大きなdeltaを曲線周期とUV実秒数で別々に畳む定義。
		FMeshMaterialEffectDefinition LoopDefinition = MakeBoundaryDefinition( true );
		FMeshMaterialEffectPlayback LoopPlayback;
		LoopPlayback.Restart();
		LoopPlayback.Update( 10000.375f, LoopDefinition );
		Checker.Near( LoopPlayback.EffectTimeSeconds, 0.375, 1.0e-8, "大きなdeltaの曲線時刻をDuration内へ折り返す" );
		Checker.Near( LoopPlayback.EffectUVOffset[0], 0.09375, 1.0e-6, "元画像Uは経過秒数全体を使う" );
		Checker.Near( LoopPlayback.EffectUVOffset[1], 0.953125, 1.0e-6, "負の元画像V速度を正の位相へ折り返す" );
		Checker.Near( LoopPlayback.DistortionUVOffset[0], 0.140625, 1.0e-6, "ゆがみ画像Uは独立した経過秒scrollを使う" );
		// 保存float32速度をfloat64秒数へ掛けた独立noise V期待値。
		const float64 ExpectedNoiseV = ExpectedBoundaryUVPhase( LoopDefinition.Material.Distortion.UVScroll.y, 10000.375f );
		Checker.Near( LoopPlayback.DistortionUVOffset[1], ExpectedNoiseV, 1.0e-10, "負のnoise V速度をfloat32入力値から独立して折り返す" );

		// Durationで折り返すと元画像UVと異なる、周期aliasのない短いloop入力。
		FMeshMaterialEffectPlayback NonAliasLoopPlayback;
		NonAliasLoopPlayback.Restart();
		NonAliasLoopPlayback.Update( 2.125f, LoopDefinition );
		Checker.Near( NonAliasLoopPlayback.EffectTimeSeconds, 0.875, 1.0e-8, "別の大きさのloop deltaも曲線時刻をDuration内へ折り返す" );
		Checker.Near( NonAliasLoopPlayback.EffectUVOffset[0], 0.53125, 1.0e-8, "loopの元画像UVはcurve周期ではなく全elapsed秒で進む" );

		// pause中の無効deltaと再開後の時間進行を比較する基準値。
		LoopPlayback.Pause();
		const float64 PausedTime = LoopPlayback.EffectTimeSeconds;
		const float64 PausedSourceU = LoopPlayback.EffectUVOffset[0];
		const float64 PausedNoiseV = LoopPlayback.DistortionUVOffset[1];
		LoopPlayback.Update( 0.75f, LoopDefinition );
		const bool bPausedFrozen = LoopPlayback.EffectTimeSeconds == PausedTime && LoopPlayback.EffectUVOffset[0] == PausedSourceU && LoopPlayback.DistortionUVOffset[1] == PausedNoiseV;
		LoopPlayback.Play();
		LoopPlayback.Update( 0.25f, LoopDefinition );
		const bool bPausedResumed = std::abs( LoopPlayback.EffectTimeSeconds - 0.625 ) < 1.0e-8;
		Checker.Check( bPausedFrozen && bPausedResumed, "一時停止中は曲線と二組のUV位相を保ち再開後に同じ時計を進める" );

		// one-shot終了後のcurve終端と残り時間分だけ進むUV基準。
		FMeshMaterialEffectDefinition OnceDefinition = MakeBoundaryDefinition( false );
		OnceDefinition.Material.Emission.Keys.Add( { 0.0f, 0.25f } );
		OnceDefinition.Material.Emission.Keys.Add( { 1.0f, 0.75f } );
		FMeshMaterialEffectPlayback OncePlayback;
		OncePlayback.Restart();
		OncePlayback.Update( 5.0f, OnceDefinition );
		const bool bFinishedAtEnd = OncePlayback.EffectState == FMeshMaterialEffectPlayback::EEffectPlaybackState::Finished && OncePlayback.EffectTimeSeconds == 1.25 && OnceDefinition.Material.Emission.Evaluate( 1.0f, 0.0f ) == 0.75f;
		const bool bOneShotOffsetsUseRemaining = std::abs( OncePlayback.EffectUVOffset[0] - 0.3125 ) < 1.0e-8 && std::abs( OncePlayback.EffectUVOffset[1] - 0.84375 ) < 1.0e-8 && std::abs( OncePlayback.DistortionUVOffset[0] - 0.46875 ) < 1.0e-8 && std::abs( OncePlayback.DistortionUVOffset[1] - 0.75 ) < 1.0e-6;
		OncePlayback.Play();
		const bool bFinishedPlayRestarts = OncePlayback.EffectState == FMeshMaterialEffectPlayback::EEffectPlaybackState::Playing && OncePlayback.EffectTimeSeconds == 0.0 && OncePlayback.EffectUVOffset[0] == 0.0 && OncePlayback.DistortionUVOffset[1] == 0.0;
		OncePlayback.Update( 0.25f, OnceDefinition );
		OncePlayback.Restart();
		const bool bRestartReturnsToZero = OncePlayback.EffectTimeSeconds == 0.0 && OncePlayback.EffectUVOffset[0] == 0.0 && OncePlayback.DistortionUVOffset[1] == 0.0;
		Checker.Check( bFinishedAtEnd && bOneShotOffsetsUseRemaining && bFinishedPlayRestarts && bRestartReturnsToZero, "一回再生は終端曲線を保ちUVを終了まで進め、PlayとRestartで時刻0へ戻る" );

		// 不正deltaや不正Durationで状態が変わらない保存値。
		FMeshMaterialEffectPlayback InvalidInputPlayback;
		InvalidInputPlayback.Restart();
		InvalidInputPlayback.Update( 0.25f, LoopDefinition );
		const float64 BeforeInvalidTime = InvalidInputPlayback.EffectTimeSeconds;
		const float64 BeforeInvalidUV = InvalidInputPlayback.EffectUVOffset[0];
		for ( const float32 InvalidDelta : { -1.0f, std::numeric_limits<float32>::quiet_NaN(), std::numeric_limits<float32>::infinity(), -std::numeric_limits<float32>::infinity() } ) InvalidInputPlayback.Update( InvalidDelta, LoopDefinition );
		FMeshMaterialEffectDefinition InvalidDuration = LoopDefinition;
		InvalidDuration.Duration = 0.0f;
		InvalidInputPlayback.Update( 0.5f, InvalidDuration );
		InvalidDuration.Duration = std::numeric_limits<float32>::quiet_NaN();
		InvalidInputPlayback.Update( 0.5f, InvalidDuration );
		Checker.Check( InvalidInputPlayback.EffectTimeSeconds == BeforeInvalidTime && InvalidInputPlayback.EffectUVOffset[0] == BeforeInvalidUV, "負・非有限deltaと不正Durationを無視する" );

		// fake source・geometry所有をworker解放境界まで調べるfixture設定。
		Fake.bSupportStaticModelSurface = true;
		Fake.bSupportModelMaterialGeometry = true;
		Fake.StaticModelSurfaceData = MakeBoundarySurface();
		// 前suiteのanimation設定に依存しない空のmodel fixture。
		Fake.NextModelData = FFakeAssetBackend::FModelData();
		const FString ModelPath = Directory / "model_material_boundary_fake.mv1";
		const bool bFixtureWritten = NFileSystem::EnsureDirectory( Directory ) && WriteFileText( ModelPath, "fake model material boundary" );
		TSharedPtr<CModel> Model = bFixtureWritten ? Assets.LoadModel( ModelPath ) : TSharedPtr<CModel>();
		TSharedPtr<CModelInstance> Instance = Model.IsValid() ? Model->CreateInstance() : TSharedPtr<CModelInstance>();
		TWeakPtr<CModel> SourceWeak = Model;
		FModelMaterialEffectRenderer Renderer;
		FBoundaryExecutor Executor;
		int32 TextureLoaderCalls = 0;
		const auto BorrowedTextureLoader = [&TextureLoaderCalls]( const FString& ) { ++TextureLoaderCalls; return TSharedPtr<CTexture>(); };
		FMeshMaterialEffectDefinition RendererDefinition = MakeBoundaryDefinition( true );
		const int32 QueriesBeforeWorker = Fake.NumStaticModelSurfaceQueries;
		const int32 GeometriesBeforeWorker = Fake.NumModelMaterialGeometryCreates;
		bool bWorkerPrepared = true;
		FModelMaterialEffectRenderer WorkerPrepareRenderer;
		std::thread PrepareWorker( [&]() { bWorkerPrepared = WorkerPrepareRenderer.Prepare( Model, 0, RendererDefinition, BorrowedTextureLoader, Executor ); } );
		PrepareWorker.join();
		Checker.Check( Model.IsValid() && !bWorkerPrepared && Fake.NumStaticModelSurfaceQueries == QueriesBeforeWorker && Fake.NumModelMaterialGeometryCreates == GeometriesBeforeWorker && TextureLoaderCalls == 0, "別threadからのPrepareは面取得・geometry作成・画像読込より先に拒否する" );

		const int32 QueriesBeforePrepare = Fake.NumStaticModelSurfaceQueries;
		const int32 CreatesBeforePrepare = Fake.NumModelMaterialGeometryCreates;
		const size_t GeometryCountBeforePrepare = Fake.ModelMaterialGeometries.size();
		const bool bPrepared = Model.IsValid() && Renderer.Prepare( Model, 0, RendererDefinition, BorrowedTextureLoader, Executor );
		Renderer.Update( 10000.375f );
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		const bool bRecordHandled = Renderer.Record( Draw, Instance, FMatrix4x4::Identity );
		FDrawModelMaterialData Payload;
		bool bPayloadFound = false;
		if ( !Buffer.Words.empty() ) ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type != EDrawCommand::ModelMaterial3D ) return;
			Payload = Command.Read<FDrawModelMaterialData>();
			bPayloadFound = true;
		} );
		// commandのfloat32 noise Vへ丸める独立期待値。
		const float32 ExpectedPayloadNoiseV = static_cast<float32>( ExpectedBoundaryUVPhase( RendererDefinition.Material.Distortion.UVScroll.y, 10000.375f ) );
		const bool bRendererClockAndScroll = bRecordHandled && bPayloadFound && std::abs( Renderer.GetEffectTime() - 0.375 ) < 1.0e-8 && std::abs( Payload.Material.UVOffset[0] - 0.09375f ) < 1.0e-6f && std::abs( Payload.Material.UVOffset[1] - 0.953125f ) < 1.0e-6f && std::abs( Payload.Material.DistortionUVOffset[0] - 0.140625f ) < 1.0e-6f && Payload.Material.DistortionUVOffset[1] == ExpectedPayloadNoiseV;
		Checker.Check( bFixtureWritten && bPrepared && Instance.IsValid() && bRendererClockAndScroll && TextureLoaderCalls == 0 && Fake.NumStaticModelSurfaceQueries == QueriesBeforePrepare + 1 && Fake.NumModelMaterialGeometryCreates == CreatesBeforePrepare + 1 && Fake.ModelMaterialGeometries.size() == GeometryCountBeforePrepare + 1, "描画命令は大delta後のloop時刻と独立UV位相を持ち、借用画像を再読込しない" );

		// 最後のeffect ownerだけをworkerで外すための共有参照とhandle。
		FModelMaterialEffectRenderer LastRenderer = Renderer;
		const int32 SourceHandle = Model.IsValid() ? Model->GetHandle() : NAsset::InvalidHandle;
		const int32 GeometryHandle = Payload.Geometry;
		const size_t ModelsBeforeCleanup = Fake.Models.size();
		const size_t GeometriesBeforeCleanup = Fake.ModelMaterialGeometries.size();
		const size_t TexturesBeforeCleanup = Fake.Textures.size();
		const size_t DeletedTexturesBeforeCleanup = Fake.DeletedTextures.size();
		Renderer.Clear();
		Instance.Reset();
		Model.Reset();
		Assets.ReleaseUnused();
		const bool bOwnerRetainedBeforeWorker = SourceWeak.IsValid() && Fake.Models.count( SourceHandle ) == 1 && Fake.ModelMaterialGeometries.count( GeometryHandle ) == 1;
		std::thread ReleaseWorker( [&LastRenderer]() { LastRenderer.Clear(); } );
		ReleaseWorker.join();
		const bool bDeferredAfterWorker = !SourceWeak.IsValid() && Fake.Models.size() == ModelsBeforeCleanup - 1 && Fake.Models.count( SourceHandle ) == 1 && Fake.ModelMaterialGeometries.size() == GeometriesBeforeCleanup && Fake.ModelMaterialGeometries.count( GeometryHandle ) == 1;
		Assets.Tick( 0.0f );
		const bool bReleasedOnTick = !SourceWeak.IsValid() && Fake.Models.size() + 2 == ModelsBeforeCleanup && Fake.Models.count( SourceHandle ) == 0 && Fake.ModelMaterialGeometries.size() + 1 == GeometriesBeforeCleanup && Fake.ModelMaterialGeometries.count( GeometryHandle ) == 0 && Fake.Textures.size() == TexturesBeforeCleanup && Fake.DeletedTextures.size() == DeletedTexturesBeforeCleanup;
		Checker.Check( bOwnerRetainedBeforeWorker && bDeferredAfterWorker && bReleasedOnTick, "別threadで最後の描画資源を解放すると元modelは失効しnative modelとgeometryはAssetManagerのtickまで残り借用画像は削除しない" );

		// この試験で変えたfake設定・計数と履歴長を元へ戻す。
		Fake.bSupportStaticModelSurface = OldSurfaceSupport;
		Fake.bSupportModelMaterialGeometry = OldGeometrySupport;
		Fake.StaticModelSurfaceData = OldSurface;
		Fake.NextModelData = OldNextModelData;
		Fake.Loading = OldLoading;
		Fake.NumStaticModelSurfaceQueries = OldSurfaceQueries;
		Fake.NumModelMaterialGeometryCreates = OldGeometryCreates;
		Fake.NumTextureLoads = OldTextureLoads;
		Fake.NumLoadingQueries = OldLoadingQueries;
		Fake.NextModelMaterialGeometry = OldNextGeometry;
		Fake.DeletedModelMaterialGeometries.resize( OldDeletedGeometryCount );
		Fake.LoadedModelPaths.resize( OldLoadedPathCount );
	}

	void RunStaticModelMaterialBoundaryRealChecks( FChecker& Checker, CAssetManager& Assets, const FString& Directory )
	{
		Checker.Group = "model-material-boundary-real";
		// 実モデル資源を所有しているbackendを検査対象へ絞る参照。
		IAssetBackend* AssetBackend = Assets.GetBackend();
		CDxLibAssetBackend* Backend = dynamic_cast<CDxLibAssetBackend*>( AssetBackend );
		if ( Backend == nullptr )
		{
		Checker.Check( false, "モデル材質geometry境界試験にはDxLibの資源backendが必要" );
			return;
		}

		// 既存geometryを保持しながらfailure atomicityを測る小入力。
		const FStaticModelSurfaceData Surface = MakeBoundarySurface();
		const int32 BaselineCount = Backend->GetNumModelMaterialGeometries();
		const int32 FirstHandle = Backend->CreateModelMaterialGeometry( Surface, 0 );
		const int32 SecondHandle = Backend->CreateModelMaterialGeometry( Surface, 1 );
		const bool bCreatedBoth = FirstHandle >= 0 && SecondHandle == FirstHandle + 1;
		const FDxLibModelMaterialGeometry* FirstGeometry = FirstHandle >= 0 ? Backend->FindModelMaterialGeometry( FirstHandle ) : nullptr;
		const FDxLibModelMaterialGeometry* SecondGeometry = SecondHandle >= 0 ? Backend->FindModelMaterialGeometry( SecondHandle ) : nullptr;
		const bool bSelectedRawCopy = HasExpectedSmallGeometry( FirstGeometry, Surface, 0 ) && HasExpectedSmallGeometry( SecondGeometry, Surface, 1 ) && Surface.Materials[0].Diffuse[0] == 0.2f && Surface.Vertices[0].Color[0] == 19 && Surface.Vertices[3].Color[0] == 137;
		Checker.Check( bCreatedBoth && bSelectedRawCopy && Backend->GetNumModelMaterialGeometries() == BaselineCount + 2, "資源backendは連番の不変材質geometryを作り選択三角形だけを頂点色・属性ごとに写す" );
		if ( !bCreatedBoth || FirstGeometry == nullptr || SecondGeometry == nullptr )
		{
			if ( FirstHandle >= 0 ) Backend->DeleteModelMaterialGeometry( FirstHandle );
			if ( SecondHandle >= 0 ) Backend->DeleteModelMaterialGeometry( SecondHandle );
			Checker.Check( Backend->GetNumModelMaterialGeometries() == BaselineCount, "形状資源の準備失敗後にbackend mapの件数を開始値へ戻す" );
			return;
		}
		// backend map插入でpointerが移動しても保持できる既存geometry値copy。
		const FDxLibModelMaterialGeometry FirstGeometryValue = *FirstGeometry;
		const FDxLibModelMaterialGeometry SecondGeometryValue = *SecondGeometry;

		// malformed入力を一つずつ作る試験用copyと改変lambda。
		const auto Reject = [&]( FStaticModelSurfaceData Invalid, int32 MaterialIndex, const char* Name ) { CheckRejectedSurface( Checker, *Backend, Invalid, MaterialIndex, BaselineCount + 2, FirstHandle, SecondHandle, FirstGeometryValue, SecondGeometryValue, Name ); };
		FStaticModelSurfaceData Invalid = Surface;
		Invalid.Sections[1].FirstIndex = 4;
		Reject( Invalid, 0, "先頭sectionが有効でも末尾sectionに隙間がある" );
		Invalid = Surface;
		Invalid.Sections[1].FirstIndex = 2;
		Reject( Invalid, 0, "section開始位置が重なる" );
		Invalid = Surface;
		Invalid.Indices[5] = 99;
		Reject( Invalid, 0, "source indexが頂点範囲を超える" );
		Invalid = Surface;
		Invalid.Vertices[5].Position[1] = std::numeric_limits<float32>::quiet_NaN();
		Reject( Invalid, 0, "source頂点に非有限値がある" );
		Invalid = Surface;
		Invalid.Materials[1].Diffuse[2] = std::numeric_limits<float32>::quiet_NaN();
		Reject( Invalid, 0, "未選択materialに非有限値がある" );
		Invalid = Surface;
		Invalid.Indices.Add( 0 );
		Invalid.Indices.Add( 1 );
		Invalid.Indices.Add( 2 );
		Reject( Invalid, 0, "section列の後ろに未被覆indexが残る" );
		Invalid = Surface;
		Invalid.Sections[1].SourceMeshIndex = Invalid.Sections[0].SourceMeshIndex;
		Invalid.Sections[1].SourceFrameIndex = Invalid.Sections[0].SourceFrameIndex + 1;
		Reject( Invalid, 0, "同じmesh IDのframeとmaterial対応が食い違う" );
		Reject( Surface, 2, "選択sectionを持たないmaterial" );

		// 8 chunkを必要とし、元三角形順を位置値で復元する大きな面。
		FStaticModelSurfaceData LargeSurface;
		constexpr int32 NumTriangles = 2728;
		constexpr int32 NumLargeVertices = NumTriangles * 3;
		LargeSurface.Vertices.Reserve( NumLargeVertices );
		LargeSurface.Indices.Reserve( NumLargeVertices );
		for ( int32 Index = 0; Index < NumLargeVertices; ++Index )
		{
			LargeSurface.Vertices.Add( MakeBoundaryVertex( static_cast<float32>( Index ), static_cast<uint8>( Index % 251 ), static_cast<uint8>( ( Index + 31 ) % 251 ), static_cast<uint8>( ( Index + 79 ) % 251 ) ) );
			LargeSurface.Indices.Add( static_cast<uint32>( Index ) );
		}
		LargeSurface.Materials.Add( FStaticModelSurfaceMaterial() );
		FStaticModelSurfaceSection LargeSection;
		LargeSection.NumIndices = NumLargeVertices;
		LargeSection.MaterialIndex = 0;
		LargeSection.SourceFrameIndex = 3;
		LargeSection.SourceMeshIndex = 6;
		LargeSurface.Sections.Add( LargeSection );
		const int32 LargeHandle = Backend->CreateModelMaterialGeometry( LargeSurface, 0 );
		const FDxLibModelMaterialGeometry* LargeGeometry = LargeHandle >= 0 ? Backend->FindModelMaterialGeometry( LargeHandle ) : nullptr;
		const bool bRejectedCallsDidNotConsumeHandle = LargeHandle == SecondHandle + 1;
		bool bLargeChunkSequence = LargeGeometry != nullptr && LargeGeometry->Meshes.Num() == 1 && LargeGeometry->Meshes[0].Chunks.Num() == 8;
		int32 RecoveredVertex = 0;
		if ( LargeGeometry != nullptr && LargeGeometry->Meshes.Num() == 1 ) for ( const FMeshGeometryChunk& Chunk : LargeGeometry->Meshes[0].Chunks )
		{
			bLargeChunkSequence = bLargeChunkSequence && Chunk.Vertices.Num() <= 1024 && Chunk.Indices.Num() <= 3072 && Chunk.Vertices.Num() == Chunk.Indices.Num() && Chunk.Indices.Num() % 3 == 0;
			for ( int32 Index = 0; Index < Chunk.Indices.Num(); ++Index )
			{
				const uint32 LocalIndex = Chunk.Indices[Index];
				if ( LocalIndex >= static_cast<uint32>( Chunk.Vertices.Num() ) ) { bLargeChunkSequence = false; continue; }
				const FMeshVertex& Vertex = Chunk.Vertices[static_cast<int32>( LocalIndex )];
				bLargeChunkSequence = bLargeChunkSequence && Vertex.Position[0] == static_cast<float32>( RecoveredVertex ) && Vertex.Color[0] == static_cast<uint8>( RecoveredVertex % 251 );
				++RecoveredVertex;
			}
		}
		bLargeChunkSequence = bLargeChunkSequence && RecoveredVertex == NumLargeVertices;
		Checker.Check( bLargeChunkSequence && bRejectedCallsDidNotConsumeHandle, "資源backendは8個の上限内chunkをまたいで三角形順と頂点属性を保ち失敗でhandleを消費しない" );

		// geometryが多数chunkでも固定ModelMaterial命令のword数を一定にする値。
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		FDrawModelMaterialData Command;
		Command.Handle = 1;
		Command.Geometry = LargeHandle;
		Command.Material.Texture = 9;
		Command.Material.DistortionTexture = -1;
		Command.Material.Material.MaskTexture = -1;
		const bool bRecorded = Draw.ModelMaterial( Command );
		Checker.Check( bRecorded && Buffer.Words.size() == 41 && sizeof( FDrawModelMaterialData ) == 160, "8 geometry chunkでも固定164-byte ModelMaterial命令を一つだけ使う" );

		// 成功handleだけを削除し、開始時の既存map内容数へ戻す。
		if ( LargeHandle >= 0 ) Backend->DeleteModelMaterialGeometry( LargeHandle );
		Backend->DeleteModelMaterialGeometry( FirstHandle );
		Backend->DeleteModelMaterialGeometry( SecondHandle );
		Checker.Check( Backend->GetNumModelMaterialGeometries() == BaselineCount && Backend->FindModelMaterialGeometry( FirstHandle ) == nullptr && Backend->FindModelMaterialGeometry( SecondHandle ) == nullptr && ( LargeHandle < 0 || Backend->FindModelMaterialGeometry( LargeHandle ) == nullptr ), "直接作成したgeometryを解放し既存backend資源を保つ" );
		(void)Directory;
	}
}
