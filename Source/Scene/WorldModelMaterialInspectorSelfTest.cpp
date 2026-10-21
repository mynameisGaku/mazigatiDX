// SPDX-License-Identifier: NOASSERTION
#include "WorldModelMaterialInspectorSelfTest.h"

#include <set>
#include <thread>
#include <vector>

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/EditorAssetInfo.h"
#include "Library/Framework/Asset/StaticModelSurface.h"
#include "Library/Platform/FileSystem.h"

namespace NWorldTest
{
	namespace
	{
		// cache検査へ既知の三角形・材質・借用画像handleを渡す。
		FStaticModelSurfaceData MakeInspectorSurfaceData( int32 BorrowedTextureHandle )
		{
			FStaticModelSurfaceData Data; // 返却する既知の静的面データ
			FMeshVertex First; // 三角形の左下頂点
			First.Position[0] = -2.0f;
			First.Position[1] = -1.0f;
			First.Normal[2] = 1.0f;
			First.U = 0.125f;
			First.V = 0.25f;
			FMeshVertex Second; // 三角形の右下頂点
			Second.Position[0] = 2.0f;
			Second.Position[1] = -1.0f;
			Second.Normal[2] = 1.0f;
			Second.U = 0.875f;
			Second.V = 0.25f;
			FMeshVertex Third; // 三角形の上頂点
			Third.Position[1] = 3.0f;
			Third.Normal[2] = 1.0f;
			Third.U = 0.5f;
			Third.V = 0.75f;
			Data.Vertices.Add( First );
			Data.Vertices.Add( Second );
			Data.Vertices.Add( Third );
			Data.Indices.Add( 0 );
			Data.Indices.Add( 1 );
			Data.Indices.Add( 2 );
			FStaticModelSurfaceSection Section; // 既知triangleを覆う単一material section
			Section.FirstIndex = 0;
			Section.NumIndices = 3;
			Section.MaterialIndex = 0;
			Section.SourceFrameIndex = 4;
			Section.SourceMeshIndex = 6;
			Data.Sections.Add( Section );
			FStaticModelSurfaceMaterial Material; // 借用画像と拡散色を持つmaterial値
			Material.Diffuse[0] = 0.2f;
			Material.Diffuse[1] = 0.4f;
			Material.Diffuse[2] = 0.6f;
			Material.Diffuse[3] = 0.8f;
			Material.BorrowedDiffuseTexture = BorrowedTextureHandle;
			Data.Materials.Add( Material );
			return Data;
		}

		// fake model mapが検査前のhandle key集合へ戻ったかを調べる。
		bool HasSameModelDataKeys( const std::map<int32, NAssetTest::FFakeAssetBackend::FModelData>& Data, const std::set<int32>& Expected )
		{
			if ( Data.size() != Expected.size() ) return false;
			for ( const int32 Handle : Expected ) if ( Data.find( Handle ) == Data.end() ) return false;
			return true;
		}
	}

	void RunWorldModelInspectorFakeChecks( NAssetTest::FChecker& Checker, CAssetManager& Assets, NAssetTest::FFakeAssetBackend& Fake, const FString& Directory )
	{
		const char* OldGroup = Checker.Group; // 呼出元の検査group
		const FString OldRoot = Assets.GetRootDirectory(); // 呼出元のasset root
		const bool bOldSurfaceSupport = Fake.bSupportStaticModelSurface; // 呼出元のsurface対応設定
		const FStaticModelSurfaceData OldSurfaceData = Fake.StaticModelSurfaceData; // 呼出元のsurface fixture
		const NAssetTest::FFakeAssetBackend::FModelData OldNextModelData = Fake.NextModelData; // 次回LoadModelの元設定
		const int32 OldNextHandle = Fake.Next; // 呼出元の資源handle採番位置
		const int32 OldSurfaceQueries = Fake.NumStaticModelSurfaceQueries; // 呼出元のsurface照会数
		const int32 OldLoadingQueries = Fake.NumLoadingQueries; // 呼出元のready照会数
		const std::vector<FString> OldLoadedModelPaths = Fake.LoadedModelPaths; // 呼出元のmodel load履歴
		const std::set<int32> OldModels = Fake.Models; // 呼出元のlive model handles
		const std::set<int32> OldTextures = Fake.Textures; // 呼出元のlive texture handles
		const std::set<int32> OldLoading = Fake.Loading; // 呼出元のpending handles
		std::set<int32> OldModelDataKeys; // 呼出元のmodel metadata handle keys
		for ( const auto& Pair : Fake.ModelData ) OldModelDataKeys.insert( Pair.first ); // 既存model metadataのhandle key
		const size_t OldDeletedTextures = Fake.DeletedTextures.size(); // 呼出元のborrowed texture解放履歴
		const FBackendMemoryInfo OldMemory = Fake.GetMemoryInfo(); // fake backendの開始資源数
		const FString FixtureRoot = Directory / "world_model_material_inspector_fake"; // この検査専用の一時root
		const FString ModelPath = "inspector_cache_source.mv1"; // 静的面cache成功fixture
		const FString PendingPath = "inspector_cache_pending.mv1"; // loadingからreadyへ移るfixture
		const FString FailurePath = "inspector_cache_failure.mv1"; // 恒久失敗とClear後retry fixture
		const FString MissingPath = "inspector_cache_missing.mv1"; // 存在しないpath fixture
		const FString WorkerPath = "inspector_cache_worker.mv1"; // worker guard用にだけ存在するpath
		const int32 BorrowedTextureHandle = 43127; // fake modelが借用する不透明な画像handle
		Checker.Group = "world-model-inspector-fake";

		Assets.SetRootDirectory( FixtureRoot );
		Fake.bSupportStaticModelSurface = true;
		Fake.StaticModelSurfaceData = MakeInspectorSurfaceData( BorrowedTextureHandle );
		Fake.NextModelData = NAssetTest::FFakeAssetBackend::FModelData();
		Fake.NextModelData.MeshCount = 1;
		const bool bFixtureWritten = NFileSystem::EnsureDirectory( FixtureRoot ) && NAssetTest::WriteFileText( FixtureRoot / ModelPath, "fake inspector model" ) && NAssetTest::WriteFileText( FixtureRoot / PendingPath, "fake pending inspector model" ) && NAssetTest::WriteFileText( FixtureRoot / FailurePath, "fake failed surface model" ) && NAssetTest::WriteFileText( FixtureRoot / WorkerPath, "fake worker guard model" ); // fake sourceファイル群の作成結果
		Checker.Check( bFixtureWritten, "Inspector fake用のmodel fixtureを書き込める" );

		FAssetEditorInfo MainCache( Assets ); // 成功sourceと外部snapshotを所有するeditor cache
		FAssetEditorInfo PendingCache( Assets ); // pending状態だけを扱うeditor cache
		FAssetEditorInfo FailureCache( Assets ); // 恒久失敗とClear後の再試行cache
		TSharedPtr<const FStaticModelSurface> ExternalSnapshot; // cache Clear後も外部に残すsnapshot owner
		TWeakPtr<const CModel> SourceWeak; // snapshotがsourceを保持するか調べる弱参照
		TSharedPtr<CModel> PendingSource; // 非同期ready遷移を維持するsource owner
		TSharedPtr<const FStaticModelSurface> RetriedSnapshot; // Clear後に成功したsnapshotの一時owner
		const int32 StartSurfaceQueries = Fake.NumStaticModelSurfaceQueries; // 新しいsurface照会を数える開始位置
		const size_t StartModelLoads = Fake.LoadedModelPaths.size(); // 新しいmodel loadを数える開始位置
		const int32 StartReadyQueries = Fake.NumLoadingQueries; // ready照会を数える開始位置
		const size_t StartModelHandles = Fake.Models.size(); // sourceを読み込むpositive controlの開始handle数
		const FBackendMemoryInfo StartMemory = Fake.GetMemoryInfo(); // model handle positive controlの開始値

		// 初回の読み取り専用検索はentryもsourceも作らない。
		const TSharedPtr<const FStaticModelSurface> InitialLookup = MainCache.FindPreparedModelSurface( ModelPath ); // source未登録pathの初回検索結果
		const TSharedPtr<const FStaticModelSurface> EmptyLookup = MainCache.FindPreparedModelSurface( FString() ); // 空pathの初回検索結果
		const bool bInitialLookupPure = !InitialLookup.IsValid() && !EmptyLookup.IsValid() && Fake.NumStaticModelSurfaceQueries == StartSurfaceQueries && Fake.LoadedModelPaths.size() == StartModelLoads && Fake.NumLoadingQueries == StartReadyQueries && Fake.Models.size() == StartModelHandles; // cache検索に副作用がないか
		Checker.Check( bInitialLookupPure, "初回Findは空を返し、load・ready・surface照会を起こさない" );

		// 明示Prepareだけがready sourceとその静的面snapshotを作る。
		ExternalSnapshot = bFixtureWritten ? MainCache.PrepareModelSurface( ModelPath ) : TSharedPtr<const FStaticModelSurface>();
		const bool bSnapshotArraysValid = ExternalSnapshot.IsValid() && ExternalSnapshot->GetData().Vertices.Num() == 3 && ExternalSnapshot->GetData().Indices.Num() == 3 && ExternalSnapshot->GetData().Sections.Num() == 1 && ExternalSnapshot->GetData().Materials.Num() == 1; // index参照前に全配列長を確認
		const bool bSnapshotValuesValid = bSnapshotArraysValid && ExternalSnapshot->GetData().Vertices[0].Position[0] == -2.0f && ExternalSnapshot->GetData().Vertices[2].Position[1] == 3.0f && ExternalSnapshot->GetData().Indices[0] == 0 && ExternalSnapshot->GetData().Indices[1] == 1 && ExternalSnapshot->GetData().Indices[2] == 2 && ExternalSnapshot->GetData().Sections[0].FirstIndex == 0 && ExternalSnapshot->GetData().Sections[0].NumIndices == 3 && ExternalSnapshot->GetData().Sections[0].MaterialIndex == 0 && ExternalSnapshot->GetData().Sections[0].SourceFrameIndex == 4 && ExternalSnapshot->GetData().Sections[0].SourceMeshIndex == 6 && ExternalSnapshot->GetData().Materials[0].Diffuse[0] == 0.2f && ExternalSnapshot->GetData().Materials[0].Diffuse[1] == 0.4f && ExternalSnapshot->GetData().Materials[0].Diffuse[2] == 0.6f && ExternalSnapshot->GetData().Materials[0].Diffuse[3] == 0.8f && ExternalSnapshot->GetData().Materials[0].BorrowedDiffuseTexture == BorrowedTextureHandle; // 既知の全頂点・材質値との一致
		const FBackendMemoryInfo AfterMainPrepareMemory = Fake.GetMemoryInfo(); // sourceと個体instanceを作る正の対照
		Checker.Check( bSnapshotValuesValid && Fake.NumStaticModelSurfaceQueries == StartSurfaceQueries + 1 && Fake.LoadedModelPaths.size() == StartModelLoads + 1 && Fake.Models.size() == StartModelHandles + 2 && AfterMainPrepareMemory.NumModelHandles == StartMemory.NumModelHandles + 2, "Prepareが既知の面値を一度写し、sourceとinstanceのhandleを実際に作る" );

		// 同じpathのPrepareとFindは同じsnapshotを返し、追加照会しない。
		const int32 BeforeRepeatedSurfaceQueries = Fake.NumStaticModelSurfaceQueries; // 繰返し確認前のsurface照会数
		const int32 BeforeRepeatedReadyQueries = Fake.NumLoadingQueries; // 繰返し確認前のready照会数
		TSharedPtr<const FStaticModelSurface> RepeatedPrepare = MainCache.PrepareModelSurface( ModelPath );
		TSharedPtr<const FStaticModelSurface> RepeatedLookup = MainCache.FindPreparedModelSurface( ModelPath );
		Checker.Check( ExternalSnapshot.IsValid() && RepeatedPrepare.Get() == ExternalSnapshot.Get() && RepeatedLookup.Get() == ExternalSnapshot.Get() && Fake.NumStaticModelSurfaceQueries == BeforeRepeatedSurfaceQueries && Fake.NumLoadingQueries == BeforeRepeatedReadyQueries, "繰返しPrepareとFindは同じsnapshotを返しbackendへ再照会しない" );
		RepeatedPrepare.Reset();
		RepeatedLookup.Reset();

		// 未ready中はcache試行済みにならず、同じsourceのready後に初回snapshotを作る。
		const size_t BeforePendingLoads = Fake.LoadedModelPaths.size(); // pending sourceのload前path数
		PendingSource = bFixtureWritten ? Assets.LoadModel( PendingPath, true ) : TSharedPtr<CModel>();
		const int32 PendingHandle = PendingSource.IsValid() ? PendingSource->GetHandle() : NAsset::InvalidHandle; // pending sourceのnative handle
		const int32 BeforePendingSurfaceQueries = Fake.NumStaticModelSurfaceQueries; // pending prepare前の面照会数
		const TSharedPtr<const FStaticModelSurface> PendingResult = PendingCache.PrepareModelSurface( PendingPath );
		const bool bStillPending = PendingSource.IsValid() && !PendingResult.IsValid() && Fake.NumStaticModelSurfaceQueries == BeforePendingSurfaceQueries && Fake.LoadedModelPaths.size() == BeforePendingLoads + 1 && Fake.Loading.count( PendingHandle ) == 1; // loading中のcacheが未完了sourceを保つか
		Checker.Check( bStillPending, "loading中Prepareは面照会せず、ready前の失敗をcacheしない" );
		const size_t BeforePendingFindLoads = Fake.LoadedModelPaths.size(); // pending Find前のmodel load履歴数
		const int32 BeforePendingFindReadyQueries = Fake.NumLoadingQueries; // pending Find前のready照会数
		const int32 BeforePendingFindSurfaceQueries = Fake.NumStaticModelSurfaceQueries; // pending Find前の面照会数
		const std::set<int32> BeforePendingFindModels = Fake.Models; // pending Find前のlive model handle集合
		const int32 BeforePendingFindNextHandle = Fake.Next; // pending Find前のhandle採番値
		const TSharedPtr<const FStaticModelSurface> PendingLookup = PendingCache.FindPreparedModelSurface( PendingPath ); // 既存pending entryの読み取り結果
		const bool bPendingFindPure = !PendingLookup.IsValid() && Fake.LoadedModelPaths.size() == BeforePendingFindLoads && Fake.NumLoadingQueries == BeforePendingFindReadyQueries && Fake.NumStaticModelSurfaceQueries == BeforePendingFindSurfaceQueries && Fake.Models == BeforePendingFindModels && Fake.Next == BeforePendingFindNextHandle && Fake.Loading.count( PendingHandle ) == 1; // pending Findがentryとbackendを変更しないか
		Checker.Check( bPendingFindPure, "pending entryのFindは空を返し、load・ready・surface照会・handle状態を変えない" );
		if ( PendingHandle >= 0 ) Fake.Loading.erase( PendingHandle ); // このfixtureだけを明示的にreadyへ進める
		TSharedPtr<const FStaticModelSurface> ReadyResult = PendingCache.PrepareModelSurface( PendingPath );
		Checker.Check( ReadyResult.IsValid() && ReadyResult->GetSource().Get() == PendingSource.Get() && Fake.NumStaticModelSurfaceQueries == BeforePendingSurfaceQueries + 1, "ready遷移後に同じsourceから一度だけsnapshotを作る" );
		ReadyResult.Reset();

		// backend未対応の失敗は同じcacheで再試行せず、Clear後だけ再試行する。
		Fake.bSupportStaticModelSurface = false;
		const int32 BeforeFailureQuery = Fake.NumStaticModelSurfaceQueries; // 恒久失敗の1回目query前
		const TSharedPtr<const FStaticModelSurface> FirstFailure = FailureCache.PrepareModelSurface( FailurePath );
		Fake.bSupportStaticModelSurface = true;
		const TSharedPtr<const FStaticModelSurface> RepeatedFailure = FailureCache.PrepareModelSurface( FailurePath );
		const bool bFailureMemoized = !FirstFailure.IsValid() && !RepeatedFailure.IsValid() && Fake.NumStaticModelSurfaceQueries == BeforeFailureQuery + 1; // 同pathへの失敗queryが一度だけか
		Checker.Check( bFailureMemoized, "ready後の未対応結果を同じpathで繰り返しbackendへ送らない" );
		FailureCache.Clear();
		Assets.ReleaseUnused();
		Assets.Tick( 0.0f );
		RetriedSnapshot = FailureCache.PrepareModelSurface( FailurePath );
		const bool bClearAllowsRetry = RetriedSnapshot.IsValid() && Fake.NumStaticModelSurfaceQueries == BeforeFailureQuery + 2; // Clear後に同pathが再試行されたか
		Checker.Check( bClearAllowsRetry, "Clear後は恒久失敗を忘れ、同じpathを再取得する" );

		// 空pathと欠落pathは準備済み検索・Prepareを繰り返しても資源を作らない。
		const size_t BeforeMissingLoads = Fake.LoadedModelPaths.size(); // 欠落確認前のmodel load数
		const int32 BeforeMissingSurfaceQueries = Fake.NumStaticModelSurfaceQueries; // 欠落確認前のsurface query数
		const int32 BeforeMissingReadyQueries = Fake.NumLoadingQueries; // 欠落確認前のready照会数
		const TSharedPtr<const FStaticModelSurface> MissingFirst = MainCache.PrepareModelSurface( MissingPath ); // 欠落pathの最初のPrepare
		const TSharedPtr<const FStaticModelSurface> MissingAgain = MainCache.PrepareModelSurface( MissingPath ); // 欠落pathの再Prepare
		const TSharedPtr<const FStaticModelSurface> EmptyFirst = MainCache.PrepareModelSurface( FString() ); // 空pathの最初のPrepare
		const TSharedPtr<const FStaticModelSurface> EmptyAgain = MainCache.PrepareModelSurface( FString() ); // 空pathの再Prepare
		Checker.Check( !MissingFirst.IsValid() && !MissingAgain.IsValid() && !EmptyFirst.IsValid() && !EmptyAgain.IsValid() && Fake.LoadedModelPaths.size() == BeforeMissingLoads && Fake.NumStaticModelSurfaceQueries == BeforeMissingSurfaceQueries && Fake.NumLoadingQueries == BeforeMissingReadyQueries, "欠落・空pathは再呼出ししてもmodelやsurfaceを無駄に読み込まない" );

		// worker呼出しはmain-thread guardでcacheとbackendの両方へ触れない。
		bool bWorkerReturnedEmpty = false; // worker結果が空だったか
		const size_t BeforeWorkerLoads = Fake.LoadedModelPaths.size(); // worker前のmodel load履歴数
		const int32 BeforeWorkerSurfaceQueries = Fake.NumStaticModelSurfaceQueries; // worker前のsurface query数
		const int32 BeforeWorkerReadyQueries = Fake.NumLoadingQueries; // worker前のready query数
		const std::set<int32> BeforeWorkerModels = Fake.Models; // worker前のlive model集合
		const int32 BeforeWorkerNextHandle = Fake.Next; // worker前のhandle採番位置
		std::thread Worker( [&MainCache, &WorkerPath, &bWorkerReturnedEmpty]() { bWorkerReturnedEmpty = !MainCache.PrepareModelSurface( WorkerPath ).IsValid(); } ); // main-thread guardだけを呼ぶworker
		Worker.join();
		Checker.Check( bWorkerReturnedEmpty && Fake.LoadedModelPaths.size() == BeforeWorkerLoads && Fake.NumStaticModelSurfaceQueries == BeforeWorkerSurfaceQueries && Fake.NumLoadingQueries == BeforeWorkerReadyQueries && Fake.Models == BeforeWorkerModels && Fake.Next == BeforeWorkerNextHandle, "worker Prepareはcache・query・handleを変えずnullptrを返す" );

		// cacheを消しても外部snapshotが元modelと借用handle値を保持する。
		if ( ExternalSnapshot.IsValid() ) SourceWeak = TWeakPtr<const CModel>( ExternalSnapshot->GetSource() ); // 外部snapshot元modelの弱参照
		const int32 SourceHandle = ExternalSnapshot.IsValid() ? ExternalSnapshot->GetSource()->GetHandle() : NAsset::InvalidHandle; // 外部snapshotが保持するsource handle
		MainCache.Clear();
		const bool bExternalOwnerPreserved = ExternalSnapshot.IsValid() && SourceWeak.IsValid() && Fake.Models.count( SourceHandle ) == 1 && ExternalSnapshot->GetData().Materials.Num() == 1 && ExternalSnapshot->GetData().Materials[0].BorrowedDiffuseTexture == BorrowedTextureHandle && Fake.DeletedTextures.size() == OldDeletedTextures; // snapshotがClear後もsourceと借用handle値を保つか
		Checker.Check( bExternalOwnerPreserved, "Clear後も外部snapshotがsourceと借用handle値を保ち、借用画像を削除しない" );

		// 全test ownerを切ってから実際のlive registryが開始値へ戻ることを検査する。
		PendingCache.Clear();
		FailureCache.Clear();
		ExternalSnapshot.Reset();
		RetriedSnapshot.Reset();
		PendingSource.Reset();
		Assets.ReleaseUnused();
		Assets.Tick( 0.0f );
		const bool bModelDataKeysRestored = HasSameModelDataKeys( Fake.ModelData, OldModelDataKeys ); // fake model metadataの復元状態
		const FBackendMemoryInfo EndMemory = Fake.GetMemoryInfo(); // cleanup後のfake backend資源数
		const bool bLiveOwnersRestored = Fake.Models == OldModels && Fake.Textures == OldTextures && Fake.Loading == OldLoading && bModelDataKeysRestored && EndMemory.NumModelHandles == OldMemory.NumModelHandles && EndMemory.NumTextureHandles == OldMemory.NumTextureHandles; // fakeの実handle集合とmemory数が開始値へ戻ったか
		Checker.Check( !SourceWeak.IsValid() && bLiveOwnersRestored, "snapshot・cache・pending owner解放後にmodelとtextureの実handle集合が開始値へ戻る" );

		// 呼出元の設定・履歴counterを戻す。live registryは上書きせず自然解放を確認済み。
		Assets.SetRootDirectory( OldRoot );
		Fake.bSupportStaticModelSurface = bOldSurfaceSupport;
		Fake.StaticModelSurfaceData = OldSurfaceData;
		Fake.NextModelData = OldNextModelData;
		Fake.Next = OldNextHandle;
		Fake.NumStaticModelSurfaceQueries = OldSurfaceQueries;
		Fake.NumLoadingQueries = OldLoadingQueries;
		Fake.LoadedModelPaths = OldLoadedModelPaths;
		Checker.Group = OldGroup;
	}
}
