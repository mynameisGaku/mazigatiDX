// SPDX-License-Identifier: NOASSERTION
#include "AssetSelfTestModelSurface.h"

#include "AssetSelfTestFbx.h"
#include "AssetSelfTestSupport.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/Model.h"
#include "Library/Framework/Asset/StaticModelSurface.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace NAssetTest
{
	namespace
	{
		// 比較用の三角形と材質を持つfake snapshot値。
		FStaticModelSurfaceData MakeFakeSurfaceData( float32 X, float32 Red )
		{
			// 頂点・index・section・材質を持つfake値。
			FStaticModelSurfaceData Data;
			// 先頭頂点とその派生2頂点。
			FMeshVertex A;
			A.Position[0] = X;
			// 2番目と3番目のtriangle頂点。
			FMeshVertex B = A;
			B.Position[1] = 1.0f;
			FMeshVertex C = A;
			C.Position[0] += 1.0f;
			Data.Vertices.Add( A );
			Data.Vertices.Add( B );
			Data.Vertices.Add( C );
			Data.Indices.Add( 0 );
			Data.Indices.Add( 1 );
			Data.Indices.Add( 2 );
			// triangleが属するsource位置と材質。
			FStaticModelSurfaceSection Section;
			Section.NumIndices = 3;
			Section.MaterialIndex = 0;
			Section.SourceFrameIndex = 2;
			Section.SourceMeshIndex = 4;
			Data.Sections.Add( Section );
			// 比較用diffuse色と借用handle。
			FStaticModelSurfaceMaterial Material;
			Material.Diffuse[0] = Red;
			Material.BorrowedDiffuseTexture = 9876;
			Data.Materials.Add( Material );
			return Data;
		}

		// 失敗時に出力配列すべてが保持されたかを調べる。
		bool SameSurfaceData( const FStaticModelSurfaceData& A, const FStaticModelSurfaceData& B )
		{
			if ( A.Vertices.Num() != B.Vertices.Num() || A.Indices.Num() != B.Indices.Num() || A.Sections.Num() != B.Sections.Num() || A.Materials.Num() != B.Materials.Num() ) return false;
			for ( int32 Index = 0; Index < A.Vertices.Num(); ++Index )
			{
				// 対応する頂点値。
				const FMeshVertex& Left = A.Vertices[Index];
				const FMeshVertex& Right = B.Vertices[Index];
				if ( std::memcmp( &Left, &Right, sizeof( FMeshVertex ) ) != 0 ) return false;
			}
			for ( int32 Index = 0; Index < A.Indices.Num(); ++Index ) if ( A.Indices[Index] != B.Indices[Index] ) return false;
			for ( int32 Index = 0; Index < A.Sections.Num(); ++Index )
			{
				// 対応するsection範囲値。
				const FStaticModelSurfaceSection& Left = A.Sections[Index];
				const FStaticModelSurfaceSection& Right = B.Sections[Index];
				if ( Left.FirstIndex != Right.FirstIndex || Left.NumIndices != Right.NumIndices || Left.MaterialIndex != Right.MaterialIndex || Left.SourceFrameIndex != Right.SourceFrameIndex || Left.SourceMeshIndex != Right.SourceMeshIndex ) return false;
			}
			for ( int32 Index = 0; Index < A.Materials.Num(); ++Index )
			{
				// 対応する材質値。
				const FStaticModelSurfaceMaterial& Left = A.Materials[Index];
				const FStaticModelSurfaceMaterial& Right = B.Materials[Index];
				if ( Left.BorrowedDiffuseTexture != Right.BorrowedDiffuseTexture ) return false;
				for ( int32 Channel = 0; Channel < 4; ++Channel ) if ( Left.Diffuse[Channel] != Right.Diffuse[Channel] ) return false;
			}
			return true;
		}

		// 成功後の既知の参照値が崩れていないかを確かめる。
		bool HasExpectedVertex( const FStaticModelSurfaceData& Data, float32 X, float32 Y, float32 U, float32 V, uint8 Red, uint8 Green, uint8 Blue, uint8 Alpha )
		{
			for ( const FMeshVertex& Vertex : Data.Vertices )
			{
				if ( std::fabs( Vertex.Position[0] - X ) > 0.001f || std::fabs( Vertex.Position[1] - Y ) > 0.001f || std::fabs( Vertex.Position[2] ) > 0.001f ) continue;
				if ( std::fabs( Vertex.U - U ) > 0.001f || std::fabs( Vertex.V - V ) > 0.001f ) continue;
				if ( std::fabs( Vertex.Normal[0] ) > 0.001f || std::fabs( Vertex.Normal[1] ) > 0.001f || std::fabs( Vertex.Normal[2] - 1.0f ) > 0.001f ) continue;
				if ( Vertex.Color[0] == Red && Vertex.Color[1] == Green && Vertex.Color[2] == Blue && Vertex.Color[3] == Alpha ) return true;
			}
			return false;
		}

		// 2つのframeに三角形を1つずつ置き、片方だけ画像を使うDirectX .x fixture。
		std::string MakeSurfaceProbeX()
		{
			return "xof 0303txt 0032\r\n"
				   "Frame RootFrame {\r\n"
				   "FrameTransformMatrix { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1;; }\r\n"
				   "Mesh RootTriangle { 3; 0;0;0;, 2;0;0;, 0;2;0;; 1; 3;0,1,2;;\r\n"
				   "MeshNormals { 1; 0;0;1;; 1; 3;0,0,0;; }\r\n"
				   "MeshTextureCoords { 3; 0;0;, 1;0;, 0;1;; }\r\n"
				   "MeshMaterialList { 2; 1; 0;\r\n"
				   "Material { 0.9;0.2;0.1;1.0;; 8.0; 0.0;0.0;0.0;; 0.0;0.0;0.0;; }\r\n"
				   "Material { 0.1;0.3;0.9;1.0;; 8.0; 0.0;0.0;0.0;; 0.0;0.0;0.0;; TextureFilename { \"probe_2x2.tga\"; } } } }\r\n"
				   "Frame ChildTranslatedFrame { FrameTransformMatrix { 1,0,0,0, 0,1,0,0, 0,0,1,0, 4,0,0,1;; }\r\n"
				   "Mesh ChildTriangle { 3; 0;0;0;, 0;2;0;, -2;0;0;; 1; 3;0,1,2;;\r\n"
				   "MeshNormals { 1; 0;0;1;; 1; 3;0,0,0;; }\r\n"
				   "MeshTextureCoords { 3; 0;0;, 0;1;, 1;0;; }\r\n"
				   "MeshMaterialList { 2; 1; 1;\r\n"
				   "Material { 0.9;0.2;0.1;1.0;; 8.0; 0.0;0.0;0.0;; 0.0;0.0;0.0;; }\r\n"
				   "Material { 0.1;0.3;0.9;1.0;; 8.0; 0.0;0.0;0.0;; 0.0;0.0;0.0;; TextureFilename { \"probe_2x2.tga\"; } } } } } }\r\n";
		}

		// 画像を参照しない単一三角形のDirectX .x fixture。
		const char* UntexturedSurfaceX()
		{
			return "xof 0303txt 0032\r\n"
				   "Frame Root { Mesh Triangle { 3; 0;0;0;, 1;0;0;, 0;1;0;; 1; 3;0,1,2;;\r\n"
				   "MeshNormals { 1; 0;0;1;; 1; 3;0,0,0;; }\r\n"
				   "MeshTextureCoords { 3; 0;0;, 1;0;, 0;1;; }\r\n"
				   "MeshMaterialList { 1; 1; 0; Material { 0.3;0.4;0.5;1.0;; 8.0; 0;0;0;; 0;0;0;; } } } }\r\n";
		}

		// 比較用行列が各要素で同じかを調べる。
		bool SameMatrix( const MATRIX& A, const MATRIX& B )
		{
			for ( int32 Row = 0; Row < 4; ++Row ) for ( int32 Column = 0; Column < 4; ++Column ) if ( std::fabs( A.m[Row][Column] - B.m[Row][Column] ) > 0.0001f ) return false;
			return true;
		}
	}

	void RunStaticModelSurfaceFakeChecks( FChecker& Checker, CAssetManager& Assets, FFakeAssetBackend& Fake, const FString& Directory )
	{
		// 呼出し元のcheck groupを保存し、以後の既存検査名を維持する。
		const char* OldGroup = Checker.Group;
		// fake設定と呼出し計数は既存scene検査へ漏らさないよう退避する。
		const bool bOldSupport = Fake.bSupportStaticModelSurface;
		const FStaticModelSurfaceData OldSurface = Fake.StaticModelSurfaceData;
		const FFakeAssetBackend::FModelData OldModelData = Fake.NextModelData;
		const int32 OldSurfaceQueries = Fake.NumStaticModelSurfaceQueries;
		const int32 OldLoadingQueries = Fake.NumLoadingQueries;
		const std::set<int32> OldLoading = Fake.Loading;
		const size_t OldDeletedTextures = Fake.DeletedTextures.size();
		const bool bOldCaptureFiles = Fake.bCaptureLoadedFiles;
		const std::vector<FString> OldLoadedPaths = Fake.LoadedModelPaths;
		const std::map<std::string, std::string> OldLoadedContents = Fake.LoadedFileContents;

		Checker.Group = "model-surface-fake";
		// base falseのOut不変更を比べるsentinel。
		FStaticModelSurfaceData Sentinel = MakeFakeSurfaceData( 91.0f, 0.17f );
		// 呼出し前の全配列。
		FStaticModelSurfaceData Unchanged = Sentinel;
		// base実装前のoverride query数。
		const int32 BeforeBaseQuery = Fake.NumStaticModelSurfaceQueries;
		// qualified base呼出し結果。
		const bool bBaseSupported = Fake.IAssetBackend::GetStaticModelSurface( 1000, Sentinel );
		Checker.Check( !bBaseSupported && SameSurfaceData( Sentinel, Unchanged ) && Fake.NumStaticModelSurfaceQueries == BeforeBaseQuery, "interface既定実装は未対応を返し、Outとfake override計数を変更しない" );

		Fake.bSupportStaticModelSurface = true;
		Fake.StaticModelSurfaceData = MakeFakeSurfaceData( 3.0f, 0.25f );
		Fake.NextModelData = FFakeAssetBackend::FModelData();
		WriteFileText( Directory / "surface_fake.mv1", "fake model" );
		// 既存fake handle集合の基準数。
		const size_t ModelsBefore = Fake.Models.size();
		const size_t TexturesBefore = Fake.Textures.size();
		// manager cacheを通して作るsource model。
		TSharedPtr<CModel> Model = Assets.LoadModel( "surface_fake.mv1" );
		Checker.Check( Model.IsValid(), "fake model fixture loads through CAssetManager" );
		if ( Model.IsValid() )
		{
			// fake modelのnative handleと寿命確認用weak参照。
			const int32 ModelHandle = Model->GetHandle();
			TWeakPtr<CModel> SourceWeak = Model;
			Fake.bSupportStaticModelSurface = false;
			const int32 BeforeUnsupportedQuery = Fake.NumStaticModelSurfaceQueries;
			const TSharedPtr<const FStaticModelSurface> Unsupported = Model->CreateStaticSurfaceSnapshot();
			Checker.Check( !Unsupported.IsValid() && Fake.NumStaticModelSurfaceQueries == BeforeUnsupportedQuery + 1, "fake backendの未対応応答はfactoryでnullptrになる" );
			Fake.bSupportStaticModelSurface = true;
			// backend値を受け取る最初の不変snapshot。
			TSharedPtr<const FStaticModelSurface> First = Model->CreateStaticSurfaceSnapshot();
			Checker.Check( First.IsValid() && First->GetSource() == Model && First->GetData().Vertices.Num() == 3 && First->GetData().Indices.Num() == 3 && First->GetData().Sections.Num() == 1 && First->GetData().Materials.Num() == 1, "成功snapshotはsourceを保持し、fakeの全配列を値で受け取る" );
			Fake.StaticModelSurfaceData = MakeFakeSurfaceData( 12.0f, 0.75f );
			// 値変更後に独立して作る2つ目のsnapshot。
			TSharedPtr<const FStaticModelSurface> Second = Model->CreateStaticSurfaceSnapshot();
			const bool bBothHaveExpectedArrays = First.IsValid() && Second.IsValid() && First->GetData().Vertices.Num() == 3 && First->GetData().Indices.Num() == 3 && First->GetData().Sections.Num() == 1 && First->GetData().Materials.Num() == 1 && Second->GetData().Vertices.Num() == 3 && Second->GetData().Indices.Num() == 3 && Second->GetData().Sections.Num() == 1 && Second->GetData().Materials.Num() == 1;
			Checker.Check( bBothHaveExpectedArrays && First.Get() != Second.Get() && First->GetData().Vertices[0].Position[0] == 3.0f && First->GetData().Materials[0].Diffuse[0] == 0.25f && Second->GetData().Vertices[0].Position[0] == 12.0f && Second->GetData().Materials[0].Diffuse[0] == 0.75f, "別snapshotは独立し、backend値を後から変えても既存値は不変" );
			Model.Reset();
			Assets.ReleaseUnused();
			// snapshot経由でsourceを一時的にpinする参照。
			TSharedPtr<CModel> PinnedSource = SourceWeak.Pin();
			Checker.Check( PinnedSource.IsValid() && First.IsValid() && First->GetSource() == PinnedSource && Fake.Models.count( ModelHandle ) == 1, "外側のmodel参照を手放してもsnapshotが元modelを生存させる" );
			PinnedSource.Reset();
			First.Reset();
			Checker.Check( SourceWeak.IsValid(), "2つ目のsnapshotが残る間はsourceが保持される" );
			Second.Reset();
			Assets.ReleaseUnused();
			Assets.Tick( 0.0f );
			Checker.Check( !SourceWeak.IsValid() && Fake.Models.size() == ModelsBefore && Fake.Textures.size() == TexturesBefore && Fake.DeletedTextures.size() == OldDeletedTextures, "全snapshot解放後にfake modelだけが解放されborrowed textureは削除しない" );
		}

		// backend呼出しを避ける無効handleのmodel。
		TSharedPtr<CModel> Invalid = MakeShared<CModel>( "invalid-surface", NAsset::InvalidHandle, false );
		const int32 BeforeInvalidQuery = Fake.NumStaticModelSurfaceQueries;
		Checker.Check( !Invalid->CreateStaticSurfaceSnapshot().IsValid() && Fake.NumStaticModelSurfaceQueries == BeforeInvalidQuery, "invalid handleではbackendを呼ばずnullptr" );
		Invalid.Reset();

		WriteFileText( Directory / "surface_pending.mv1", "fake pending model" );
		// Loading状態を明示する非同期fake model。
		TSharedPtr<CModel> Pending = Assets.LoadModel( "surface_pending.mv1", true );
		const int32 BeforePendingSurfaceQuery = Fake.NumStaticModelSurfaceQueries;
		const int32 BeforePendingReadyQuery = Fake.NumLoadingQueries;
		// 読込中modelのfactory結果。
		const TSharedPtr<const FStaticModelSurface> PendingSurface = Pending.IsValid() ? Pending->CreateStaticSurfaceSnapshot() : TSharedPtr<const FStaticModelSurface>();
		Checker.Check( Pending.IsValid() && !PendingSurface.IsValid() && Fake.NumStaticModelSurfaceQueries == BeforePendingSurfaceQuery && Fake.NumLoadingQueries == BeforePendingReadyQuery + 1, "async loading中はready確認だけを行い面取得を呼ばない" );
		Fake.FinishLoading();
		Pending.Reset();
		Assets.ReleaseUnused();
		Assets.Tick( 0.0f );

		WriteFileText( Directory / "surface_worker.mv1", "fake worker model" );
		// worker呼出しとdeferred解放に使うfake model。
		TSharedPtr<CModel> WorkerModel = Assets.LoadModel( "surface_worker.mv1" );
		const int32 BeforeWorkerSurfaceQuery = Fake.NumStaticModelSurfaceQueries;
		const int32 BeforeWorkerReadyQuery = Fake.NumLoadingQueries;
		// worker側factoryが誤って成功しないことを記録する値。
		bool bWorkerCreatedSnapshot = true;
		if ( WorkerModel.IsValid() )
		{
			std::thread Worker( [&WorkerModel, &bWorkerCreatedSnapshot]() { bWorkerCreatedSnapshot = WorkerModel->CreateStaticSurfaceSnapshot().IsValid(); } );
			Worker.join();
		}
		Checker.Check( WorkerModel.IsValid() && !bWorkerCreatedSnapshot && Fake.NumStaticModelSurfaceQueries == BeforeWorkerSurfaceQuery && Fake.NumLoadingQueries == BeforeWorkerReadyQuery, "main thread外の呼出しはbackendのready・surface queryを行わずnullptr" );
		// 最後のsnapshot参照を別threadで解放する値。
		TSharedPtr<const FStaticModelSurface> WorkerRelease = WorkerModel.IsValid() ? WorkerModel->CreateStaticSurfaceSnapshot() : TSharedPtr<const FStaticModelSurface>();
		// native handleがtickまで残るか確認するmodel番号。
		const int32 WorkerModelHandle = WorkerModel.IsValid() ? WorkerModel->GetHandle() : NAsset::InvalidHandle;
		TWeakPtr<CModel> WorkerWeak = WorkerModel;
		WorkerModel.Reset();
		Assets.ReleaseUnused();
		if ( WorkerRelease.IsValid() )
		{
			std::thread Worker( [Moved = std::move( WorkerRelease )]() mutable { Moved.Reset(); } );
			Worker.join();
		}
		// tick直前にdeferred破棄待ちnative handleを確認する値。
		Checker.Check( !WorkerWeak.IsValid() && Fake.Models.count( WorkerModelHandle ) == 1, "workerでsnapshotを解放するとCModelは破棄されnative model handleだけがtick待ち" );
		Assets.Tick( 0.0f );
		Checker.Check( !WorkerWeak.IsValid() && Fake.Models.count( WorkerModelHandle ) == 0 && Fake.Models.size() == ModelsBefore && Fake.Textures.size() == TexturesBefore, "main thread tick後にdeferred model handle解放が完了" );

		Fake.FinishLoading();
		Fake.bSupportStaticModelSurface = bOldSupport;
		Fake.StaticModelSurfaceData = OldSurface;
		Fake.NextModelData = OldModelData;
		Fake.NumStaticModelSurfaceQueries = OldSurfaceQueries;
		Fake.NumLoadingQueries = OldLoadingQueries;
		Fake.Loading = OldLoading;
		Fake.bCaptureLoadedFiles = bOldCaptureFiles;
		Fake.LoadedModelPaths = OldLoadedPaths;
		Fake.LoadedFileContents = OldLoadedContents;
		Checker.Group = OldGroup;
	}

	void RunStaticModelSurfaceRealChecks( FChecker& Checker, CAssetManager& Assets, const FString& Directory )
	{
		Checker.Group = "model-surface-real";
		// DxLibで読み込ませる面構造と埋込み画像の画素。
		const std::string ProbeText = MakeSurfaceProbeX();
		const std::vector<uint8> TgaBytes = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 24, 0, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255 };
		// fixtureの書込結果と読込前のnative資源数。
		const bool bProbeTextWritten = WriteFileText( Directory / "surface_probe.x", ProbeText.c_str() );
		const bool bProbeImageWritten = WriteFileBytes( Directory / "probe_2x2.tga", TgaBytes );
		Checker.Check( bProbeTextWritten && bProbeImageWritten, "static surfaceの2triangle・texture fixtureを書き出す" );
		const FBackendMemoryInfo BeforeLoad = Assets.GetBackend() != nullptr ? Assets.GetBackend()->GetMemoryInfo() : FBackendMemoryInfo();
		// 2mesh fixtureのAssetManager所有source。
		TSharedPtr<CModel> Model = bProbeTextWritten && bProbeImageWritten ? Assets.LoadModel( "surface_probe.x" ) : TSharedPtr<CModel>();
		Checker.Check( Model.IsValid() && Model->GetMeshCount() == 4, "DirectX fixtureは4材質meshと実triangle2つを読み込む" );
		if ( Model.IsValid() )
		{
			// 元model handle、行列、既存reference meshの比較用状態。
			const int32 ModelHandle = Model->GetHandle();
			const MATRIX BeforeMatrix = MV1GetMatrix( ModelHandle );
			const MATRIX TranslatedMatrix = MGetTranslate( VGet( 10.0f, 20.0f, 30.0f ) );
			const bool bMatrixSet = MV1SetMatrix( ModelHandle, TranslatedMatrix ) >= 0;
			const MATRIX MatrixBeforeSnapshot = MV1GetMatrix( ModelHandle );
			const bool bReferenceReady = MV1SetupReferenceMesh( ModelHandle, -1, TRUE, FALSE ) >= 0;
			const MV1_REF_POLYGONLIST ReferenceBefore = bReferenceReady ? MV1GetReferenceMesh( ModelHandle, -1, TRUE, FALSE ) : MV1_REF_POLYGONLIST();
			const bool bReferenceHasVertex = bReferenceReady && ReferenceBefore.VertexNum > 0 && ReferenceBefore.Vertexs != nullptr;
			const VECTOR FirstReferencePosition = bReferenceHasVertex ? ReferenceBefore.Vertexs[0].Position : VGet( 0.0f, 0.0f, 0.0f );
			// native材質の開始時点をsnapshot後の値と照合する写し。
			std::vector<FStaticModelSurfaceMaterial> NativeMaterialsBefore;
			for ( int32 MaterialIndex = 0; MaterialIndex < MV1GetMaterialNum( ModelHandle ); ++MaterialIndex )
			{
				const COLOR_F Diffuse = MV1GetMaterialDifColor( ModelHandle, MaterialIndex );
				const int32 TextureIndex = MV1GetMaterialDifMapTexture( ModelHandle, MaterialIndex );
				FStaticModelSurfaceMaterial Material;
				Material.Diffuse[0] = Diffuse.r;
				Material.Diffuse[1] = Diffuse.g;
				Material.Diffuse[2] = Diffuse.b;
				Material.Diffuse[3] = Diffuse.a;
				Material.BorrowedDiffuseTexture = TextureIndex >= 0 ? MV1GetTextureGraphHandle( ModelHandle, TextureIndex ) : -1;
				NativeMaterialsBefore.push_back( Material );
			}
			const FBackendMemoryInfo BeforeSnapshot = Assets.GetBackend() != nullptr ? Assets.GetBackend()->GetMemoryInfo() : FBackendMemoryInfo();
			Checker.Check( BeforeSnapshot.NumModelHandles > BeforeLoad.NumModelHandles && BeforeSnapshot.NumTextureHandles > BeforeLoad.NumTextureHandles, "fixture loadはnative modelとembedded texture handleを増やす" );
			// wrapperが返す値snapshotと取得後native状態。
			TSharedPtr<const FStaticModelSurface> Surface = Model->CreateStaticSurfaceSnapshot();
			const MV1_REF_POLYGONLIST ReferenceAfter = bReferenceReady ? MV1GetReferenceMesh( ModelHandle, -1, TRUE, FALSE ) : MV1_REF_POLYGONLIST();
			const MATRIX MatrixAfterSnapshot = MV1GetMatrix( ModelHandle );
			Checker.Check( bMatrixSet && bReferenceHasVertex && ReferenceAfter.Vertexs == ReferenceBefore.Vertexs && ReferenceAfter.Polygons == ReferenceBefore.Polygons && ReferenceAfter.VertexNum == ReferenceBefore.VertexNum && ReferenceAfter.PolygonNum == ReferenceBefore.PolygonNum && SameMatrix( MatrixBeforeSnapshot, MatrixAfterSnapshot ) && std::fabs( FirstReferencePosition.x - ReferenceAfter.Vertexs[0].Position.x ) < 0.001f && std::fabs( FirstReferencePosition.y - ReferenceAfter.Vertexs[0].Position.y ) < 0.001f && std::fabs( FirstReferencePosition.z - ReferenceAfter.Vertexs[0].Position.z ) < 0.001f, "snapshot取得は元modelのmatrixと既存reference meshのpointer・値を変えない" );
			Checker.Check( Surface.IsValid() && Surface->GetSource() == Model, "real backendは元modelを保持するimmutable snapshotを返す" );
			if ( Surface.IsValid() )
			{
				// 公開値を一度参照し、以降の全配列確認に使うview。
				const FStaticModelSurfaceData& Data = Surface->GetData();
				Checker.Check( Data.Vertices.Num() == 6 && Data.Indices.Num() == 6 && Data.Sections.Num() == 2 && Data.Materials.Num() == MV1GetMaterialNum( ModelHandle ), "real snapshotの頂点・index・2section・材質数がfixtureと一致" );
				bool bSectionsCover = Data.Indices.Num() == 6 && Data.Sections.Num() == 2;
				int32 NextIndex = 0;
				for ( const FStaticModelSurfaceSection& Section : Data.Sections )
				{
					const bool bSectionRangeValid = Section.FirstIndex == NextIndex && Section.NumIndices > 0 && Section.NumIndices <= Data.Indices.Num() - NextIndex;
					bSectionsCover = bSectionsCover && bSectionRangeValid && Section.NumIndices == 3 && Section.MaterialIndex >= 0 && Section.MaterialIndex < Data.Materials.Num() && Section.SourceFrameIndex >= 0 && Section.SourceFrameIndex < MV1GetFrameNum( ModelHandle ) && Section.SourceMeshIndex >= 0 && Section.SourceMeshIndex < MV1GetMeshNum( ModelHandle );
					if ( bSectionRangeValid ) NextIndex += Section.NumIndices;
				}
				bSectionsCover = bSectionsCover && NextIndex == Data.Indices.Num();
				Checker.Check( bSectionsCover, "ordered sectionがtriangle列全体を隙間なく覆いframe・mesh・material mappingが有効" );
				bool bNamedSectionMapping = false;
				bool bRootUsesRed = false;
				bool bChildUsesBlue = false;
				for ( const FStaticModelSurfaceSection& Section : Data.Sections )
				{
					if ( Section.SourceFrameIndex < 0 || Section.SourceFrameIndex >= MV1GetFrameNum( ModelHandle ) || Section.MaterialIndex < 0 || Section.MaterialIndex >= Data.Materials.Num() ) continue;
					const char* FrameName = MV1GetFrameName( ModelHandle, Section.SourceFrameIndex );
					if ( FrameName == nullptr ) continue;
					// geometry frameが親fixture frameを指す実index。
					const int32 ParentIndex = MV1GetFrameParent( ModelHandle, Section.SourceFrameIndex );
					const char* ParentName = ParentIndex >= 0 && ParentIndex < MV1GetFrameNum( ModelHandle ) ? MV1GetFrameName( ModelHandle, ParentIndex ) : nullptr;
					// sectionに対応する借用材質値。
					const FStaticModelSurfaceMaterial& Material = Data.Materials[Section.MaterialIndex];
					if ( std::strcmp( FrameName, "RootTriangle" ) == 0 && ParentIndex == 0 && ParentName != nullptr && std::strcmp( ParentName, "RootFrame" ) == 0 && Section.SourceMeshIndex == 0 && Material.Diffuse[0] > Material.Diffuse[2] ) bRootUsesRed = true;
					if ( std::strcmp( FrameName, "ChildTriangle" ) == 0 && ParentIndex == 2 && ParentName != nullptr && std::strcmp( ParentName, "ChildTranslatedFrame" ) == 0 && Section.SourceMeshIndex == 3 && Material.Diffuse[2] > Material.Diffuse[0] ) bChildUsesBlue = true;
				}
				bNamedSectionMapping = bRootUsesRed && bChildUsesBlue;
				Checker.Check( bNamedSectionMapping, "RootTriangle(親RootFrame)とChildTriangle(親ChildTranslatedFrame)のsectionを赤青材質へ対応" );
				// 空配列を成功と数えず、fixtureの正しいindex順も検証する。
				bool bIndexOrderValid = Data.Indices.Num() == 6;
				for ( int32 Index = 0; Index < Data.Indices.Num(); ++Index ) bIndexOrderValid = bIndexOrderValid && Data.Indices[Index] == static_cast<uint32>( Index );
				Checker.Check( bIndexOrderValid, "fixtureの2triangle index順を保ち、頂点順を並べ替えない" );
				// source frameの位置・法線・UV0・vertex diffuseの既知値。
				const bool bKnownVertices = HasExpectedVertex( Data, 0.0f, 0.0f, 0.0f, 0.0f, 229, 51, 25, 255 ) && HasExpectedVertex( Data, 2.0f, 0.0f, 1.0f, 0.0f, 229, 51, 25, 255 ) && HasExpectedVertex( Data, 0.0f, 2.0f, 0.0f, 1.0f, 229, 51, 25, 255 ) && HasExpectedVertex( Data, 4.0f, 0.0f, 0.0f, 0.0f, 25, 76, 229, 255 ) && HasExpectedVertex( Data, 4.0f, 2.0f, 0.0f, 1.0f, 25, 76, 229, 255 ) && HasExpectedVertex( Data, 2.0f, 0.0f, 1.0f, 0.0f, 25, 76, 229, 255 );
				Checker.Check( bKnownVertices, "frame変換後かつmodel world移動を含まない既知位置・法線・UV0・vertex diffuseを写す" );
				if ( !bNamedSectionMapping || !bKnownVertices )
				{
					LOG( LogAssetSelfTest, Error, "surface diagnostic: vertices=%d sections=%d frames=%d meshes=%d materials=%d", Data.Vertices.Num(), Data.Sections.Num(), MV1GetFrameNum( ModelHandle ), MV1GetMeshNum( ModelHandle ), Data.Materials.Num() );
					for ( int32 VertexIndex = 0; VertexIndex < Data.Vertices.Num(); ++VertexIndex )
					{
						// 失敗時に実際の変換結果を照合する頂点値。
						const FMeshVertex& Vertex = Data.Vertices[VertexIndex];
						LOG( LogAssetSelfTest, Error, "surface vertex[%d] P=(%.6f,%.6f,%.6f) N=(%.6f,%.6f,%.6f) UV=(%.6f,%.6f) Color=(%u,%u,%u,%u)", VertexIndex, Vertex.Position[0], Vertex.Position[1], Vertex.Position[2], Vertex.Normal[0], Vertex.Normal[1], Vertex.Normal[2], Vertex.U, Vertex.V, static_cast<unsigned int>( Vertex.Color[0] ), static_cast<unsigned int>( Vertex.Color[1] ), static_cast<unsigned int>( Vertex.Color[2] ), static_cast<unsigned int>( Vertex.Color[3] ) );
					}
					for ( int32 FrameIndex = 0; FrameIndex < MV1GetFrameNum( ModelHandle ); ++FrameIndex )
					{
						// DxLibが作る全frameの名前と親index。
						const char* FrameName = MV1GetFrameName( ModelHandle, FrameIndex );
						const int32 ParentIndex = MV1GetFrameParent( ModelHandle, FrameIndex );
						LOG( LogAssetSelfTest, Error, "surface frame[%d] name='%s' parent=%d", FrameIndex, FrameName != nullptr ? FrameName : "<null>", ParentIndex );
					}
					for ( int32 SectionIndex = 0; SectionIndex < Data.Sections.Num(); ++SectionIndex )
					{
						// 各triangle runが参照するframe/mesh/material対応。
						const FStaticModelSurfaceSection& Section = Data.Sections[SectionIndex];
						LOG( LogAssetSelfTest, Error, "surface section[%d] first=%d count=%d frame=%d mesh=%d material=%d", SectionIndex, Section.FirstIndex, Section.NumIndices, Section.SourceFrameIndex, Section.SourceMeshIndex, Section.MaterialIndex );
					}
				}
				// native材質写しの一致と色別positive control。
				bool bMaterialsMatch = Data.Materials.Num() == MV1GetMaterialNum( ModelHandle );
				bool bHasRed = false;
				bool bHasBlue = false;
				bool bNativeMaterialsUnchanged = Data.Materials.Num() == static_cast<int32>( NativeMaterialsBefore.size() );
				int32 BorrowedTexture = -1;
				for ( int32 MaterialIndex = 0; MaterialIndex < Data.Materials.Num(); ++MaterialIndex )
				{
					// native側の今の材質値とsnapshot前の写し。
					const COLOR_F NativeDiffuse = MV1GetMaterialDifColor( ModelHandle, MaterialIndex );
					const FStaticModelSurfaceMaterial& Material = Data.Materials[MaterialIndex];
					bMaterialsMatch = bMaterialsMatch && std::fabs( Material.Diffuse[0] - NativeDiffuse.r ) < 0.001f && std::fabs( Material.Diffuse[1] - NativeDiffuse.g ) < 0.001f && std::fabs( Material.Diffuse[2] - NativeDiffuse.b ) < 0.001f && std::fabs( Material.Diffuse[3] - NativeDiffuse.a ) < 0.001f;
					bHasRed = bHasRed || ( Material.Diffuse[0] > Material.Diffuse[2] && Material.Diffuse[1] < 0.5f );
					bHasBlue = bHasBlue || ( Material.Diffuse[2] > Material.Diffuse[0] && Material.Diffuse[1] < 0.5f );
					const int32 TextureIndex = MV1GetMaterialDifMapTexture( ModelHandle, MaterialIndex );
					const int32 NativeGraph = TextureIndex >= 0 ? MV1GetTextureGraphHandle( ModelHandle, TextureIndex ) : -1;
					bMaterialsMatch = bMaterialsMatch && Material.BorrowedDiffuseTexture == NativeGraph;
					const FStaticModelSurfaceMaterial* BeforeMaterial = MaterialIndex < static_cast<int32>( NativeMaterialsBefore.size() ) ? &NativeMaterialsBefore[MaterialIndex] : nullptr;
					bNativeMaterialsUnchanged = bNativeMaterialsUnchanged && BeforeMaterial != nullptr && Material.BorrowedDiffuseTexture == BeforeMaterial->BorrowedDiffuseTexture && std::fabs( Material.Diffuse[0] - BeforeMaterial->Diffuse[0] ) < 0.001f && std::fabs( Material.Diffuse[1] - BeforeMaterial->Diffuse[1] ) < 0.001f && std::fabs( Material.Diffuse[2] - BeforeMaterial->Diffuse[2] ) < 0.001f && std::fabs( Material.Diffuse[3] - BeforeMaterial->Diffuse[3] ) < 0.001f;
					if ( NativeGraph >= 0 ) BorrowedTexture = NativeGraph;
				}
				Checker.Check( bMaterialsMatch && bHasRed && bHasBlue && bNativeMaterialsUnchanged, "2色材質RGBA・diffuse mapがsnapshot前native値と一致" );
				// 借用graphが元modelの2x2画像であることを調べる出力値。
				int32 TextureWidth = 0;
				int32 TextureHeight = 0;
				Checker.Check( BorrowedTexture >= 0 && GetGraphSize( BorrowedTexture, &TextureWidth, &TextureHeight ) == 0 && TextureWidth == 2 && TextureHeight == 2, "借用texture handleは元modelが所有する2x2画像" );
				const FBackendMemoryInfo AfterSnapshot = Assets.GetBackend() != nullptr ? Assets.GetBackend()->GetMemoryInfo() : FBackendMemoryInfo();
				Checker.Check( BeforeSnapshot.NumModelHandles == AfterSnapshot.NumModelHandles && BeforeSnapshot.NumTextureHandles == AfterSnapshot.NumTextureHandles, "snapshotと再取得はtemporary modelやtexture handleを残さない" );
				TSharedPtr<const FStaticModelSurface> Again = Model->CreateStaticSurfaceSnapshot();
				Checker.Check( Again.IsValid() && SameSurfaceData( Data, Again->GetData() ), "同じsourceを再取得しても値とsection順は一致" );
				Again.Reset();
				if ( bReferenceReady ) MV1TerminateReferenceMesh( ModelHandle, -1, TRUE, FALSE );
				MV1SetMatrix( ModelHandle, BeforeMatrix );
				TWeakPtr<CModel> ModelWeak = Model;
				Model.Reset();
				Assets.ReleaseUnused();
				// snapshot保持中・最終解放後のgraph寸法出力。
				int32 LiveWidth = 0;
				int32 LiveHeight = 0;
				Checker.Check( ModelWeak.IsValid() && BorrowedTexture >= 0 && GetGraphSize( BorrowedTexture, &LiveWidth, &LiveHeight ) == 0, "sourceを手放してもsnapshot保有中はborrowed textureが有効" );
				// snapshotの最終参照を外した後にmodelと内蔵imageのnative handleが解放される。
				Surface.Reset();
				Assets.ReleaseUnused();
				Assets.Tick( 0.0f );
				// 解放済みgraph照会の出力先。
				int32 DeadWidth = 0;
				int32 DeadHeight = 0;
				Checker.Check( !ModelWeak.IsValid() && GetGraphSize( BorrowedTexture, &DeadWidth, &DeadHeight ) != 0, "最後のsnapshot解放後はsource modelとborrowed graph handleが解放" );
			}
			else
			{
				if ( bReferenceReady ) MV1TerminateReferenceMesh( ModelHandle, -1, TRUE, FALSE );
				MV1SetMatrix( ModelHandle, BeforeMatrix );
				Model.Reset();
				Assets.ReleaseUnused();
				Assets.Tick( 0.0f );
			}
		}
		const FBackendMemoryInfo AfterLoadCleanup = Assets.GetBackend() != nullptr ? Assets.GetBackend()->GetMemoryInfo() : FBackendMemoryInfo();
		Checker.Check( BeforeLoad.NumModelHandles == AfterLoadCleanup.NumModelHandles && BeforeLoad.NumTextureHandles == AfterLoadCleanup.NumTextureHandles, "real fixtureのmodelとembedded graphをcleanupしnative handle数を復元" );
		if ( Assets.GetBackend() != nullptr )
		{
			FStaticModelSurfaceData Out = MakeFakeSurfaceData( 67.0f, 0.55f );
			const FStaticModelSurfaceData Before = Out;
			const bool bInvalidAccepted = Assets.GetBackend()->GetStaticModelSurface( NAsset::InvalidHandle, Out );
			Checker.Check( !bInvalidAccepted && SameSurfaceData( Out, Before ), "invalid native model handleは拒否しOut sentinelを保持" );
		}

		const bool bUntexturedWritten = WriteFileText( Directory / "surface_untextured.x", UntexturedSurfaceX() );
		TSharedPtr<CModel> Untextured = bUntexturedWritten ? Assets.LoadModel( "surface_untextured.x" ) : TSharedPtr<CModel>();
		TSharedPtr<const FStaticModelSurface> UntexturedSurface = Untextured.IsValid() ? Untextured->CreateStaticSurfaceSnapshot() : TSharedPtr<const FStaticModelSurface>();
		bool bNoTexture = UntexturedSurface.IsValid() && !UntexturedSurface->GetData().Materials.IsEmpty();
		if ( UntexturedSurface.IsValid() ) for ( const FStaticModelSurfaceMaterial& Material : UntexturedSurface->GetData().Materials ) bNoTexture = bNoTexture && Material.BorrowedDiffuseTexture == -1;
		Checker.Check( bUntexturedWritten && UntexturedSurface.IsValid() && bNoTexture, "画像なしstatic modelは有効なsnapshotで画像handleを-1にする" );
		UntexturedSurface.Reset();
		Untextured.Reset();
		Assets.ReleaseUnused();
		Assets.Tick( 0.0f );

		TSharedPtr<CModel> Animated = Assets.LoadModel( "model.x" );
		bool bAnimatedControl = Animated.IsValid() && MV1GetAnimNum( Animated->GetHandle() ) > 0;
		Checker.Check( bAnimatedControl, "animated拒否fixtureが少なくとも1 animationを持つ" );
		if ( bAnimatedControl && Assets.GetBackend() != nullptr )
		{
			FStaticModelSurfaceData Out = MakeFakeSurfaceData( 75.0f, 0.8f );
			const FStaticModelSurfaceData Before = Out;
			const bool bAccepted = Assets.GetBackend()->GetStaticModelSurface( Animated->GetHandle(), Out );
			Checker.Check( !bAccepted && SameSurfaceData( Out, Before ) && !Animated->CreateStaticSurfaceSnapshot().IsValid(), "animation付きmodelは拒否され、backend Out sentinelを保持" );
		}
		Animated.Reset();
		Assets.ReleaseUnused();
		Assets.Tick( 0.0f );

		FFbxSpec EmptySpec;
		EmptySpec.bMesh = false;
		EmptySpec.bSkeleton = true;
		EmptySpec.bAnimation = true;
		EmptySpec.TextureFile = nullptr;
		const std::string EmptyFbx = MakeBarFbx( EmptySpec );
		const bool bEmptyWritten = WriteFileText( Directory / "surface_empty.fbx", EmptyFbx.c_str() );
		TSharedPtr<CModel> EmptyModel = bEmptyWritten ? Assets.LoadModel( "surface_empty.fbx" ) : TSharedPtr<CModel>();
		const bool bEmptyControl = EmptyModel.IsValid() && EmptyModel->GetMeshCount() == 0;
		Checker.Check( bEmptyControl, "empty model拒否fixtureにmeshが無い" );
		if ( bEmptyControl && Assets.GetBackend() != nullptr )
		{
			FStaticModelSurfaceData Out = MakeFakeSurfaceData( 69.0f, 0.45f );
			const FStaticModelSurfaceData Before = Out;
			const bool bAccepted = Assets.GetBackend()->GetStaticModelSurface( EmptyModel->GetHandle(), Out );
			Checker.Check( !bAccepted && SameSurfaceData( Out, Before ) && !EmptyModel->CreateStaticSurfaceSnapshot().IsValid(), "meshなしmodelは拒否されOut sentinelを保持" );
		}
		EmptyModel.Reset();
		Assets.ReleaseUnused();
		Assets.Tick( 0.0f );

		FFbxSpec SkinSpec;
		SkinSpec.bMesh = true;
		SkinSpec.bSkeleton = true;
		SkinSpec.bAnimation = false;
		SkinSpec.TextureFile = nullptr;
		const std::string SkinFbx = MakeBarFbx( SkinSpec );
		const bool bSkinWritten = WriteFileText( Directory / "surface_skinned.fbx", SkinFbx.c_str() );
		TSharedPtr<CModel> Skinned = bSkinWritten ? Assets.LoadModel( "surface_skinned.fbx" ) : TSharedPtr<CModel>();
		bool bSkinControl = Skinned.IsValid() && MV1GetAnimNum( Skinned->GetHandle() ) == 0 && MV1GetTriangleListNum( Skinned->GetHandle() ) > 0;
		if ( bSkinControl )
		{
			bool bHasDeformedVertexType = false;
			for ( int32 ListIndex = 0; ListIndex < MV1GetTriangleListNum( Skinned->GetHandle() ); ++ListIndex ) bHasDeformedVertexType = bHasDeformedVertexType || MV1GetTriangleListVertexType( Skinned->GetHandle(), ListIndex ) != DX_MV1_VERTEX_TYPE_1FRAME;
			bSkinControl = bHasDeformedVertexType;
		}
		Checker.Check( bSkinControl, "skin拒否fixtureはanimationなしで1FRAME以外のtriangle listを持つ" );
		if ( bSkinControl && Assets.GetBackend() != nullptr )
		{
			FStaticModelSurfaceData Out = MakeFakeSurfaceData( 83.0f, 0.65f );
			const FStaticModelSurfaceData Before = Out;
			const bool bAccepted = Assets.GetBackend()->GetStaticModelSurface( Skinned->GetHandle(), Out );
			Checker.Check( !bAccepted && SameSurfaceData( Out, Before ) && !Skinned->CreateStaticSurfaceSnapshot().IsValid(), "skin deformation付きmodelは拒否され、backend Out sentinelを保持" );
		}
		Skinned.Reset();
		Assets.ReleaseUnused();
		Assets.Tick( 0.0f );
	}
}
