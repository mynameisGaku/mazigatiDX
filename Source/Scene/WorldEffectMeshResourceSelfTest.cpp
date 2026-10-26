// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectMeshResourceSelfTest.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <vector>

#include "Library/Core/Interfaces/Render/DrawMaterialMesh.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Effect/ParticleEffectBackend.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Platform/FileSystem.h"
#include "WorldEffectSelfTestSupport.h"

namespace
{
	// mesh画像のfixtureをsandbox内へ書き出す。
	bool WriteMeshTextureFixture( const FString& Dir, const FString& RelativePath )
	{
		// fake backendと実backendの双方が読める既存BMP。
		const std::vector<uint8> Image = NAssetTest::MakeSheetBmp();
		return NFileSystem::EnsureDirectory( Dir / "mesh-resource" ) && NAssetTest::WriteFileBytes( Dir / RelativePath, Image );
	}

	// 指定形状と画像を使う材質mesh emitterを作る。
	FParticleEmitterDefinition MakeMeshResourceEmitter( const FString& MainPath, const FString& MaskPath, const FString& NoisePath, FEffectMeshDefinition::EKind Kind, bool bPositiveStrength )
	{
		// 既定の粒子設定を基にするemitter。
		FParticleEmitterDefinition Emitter = NEffectTest::QuietEmitter();
		Emitter.Name = "MeshResource";
		Emitter.Texture = MainPath;
		Emitter.Mesh.Kind = Kind;
		Emitter.Mesh.Segments = 12;
		Emitter.Mesh.InnerRadius = 0.5f;
		Emitter.Rate = 0.0f;
		Emitter.Bursts.Add( NEffectTest::Burst( 0.0f, 1 ) );
		Emitter.Lifetime = { 2.0f, 2.0f };
		Emitter.StartSize = Emitter.EndSize = { 1.0f, 1.0f };
		Emitter.StartColor = Emitter.EndColor = FColor32( 220, 230, 255, 255 );
		Emitter.Material.bEnabled = true;
		Emitter.Material.MaskTexture = MaskPath;
		Emitter.Material.Emission.Keys.Add( { 0.0f, 1.0f } );
		Emitter.Material.Emission.Keys.Add( { 1.0f, 3.0f } );
		Emitter.Material.Distortion.bEnabled = true;
		Emitter.Material.Distortion.Texture = NoisePath;
		Emitter.Material.Distortion.Strength.Keys.Add( { 0.0f, bPositiveStrength ? 0.4f : 0.0f } );
		Emitter.Material.Distortion.Strength.Keys.Add( { 1.0f, bPositiveStrength ? 0.4f : 0.0f } );
		return Emitter;
	}

	// Playbackを進め、burst粒子に描画可能な経過時間を与える。
	void AdvanceMeshPlayback( const TSharedPtr<IEffectPlayback>& Playback, int32 ExtraSteps = 0 )
	{
		if ( !Playback.IsValid() ) return;
		Playback->Update( 0.1f );
		Playback->Update( 0.1f );
		// burst後の粒子を進める追加刻み。
		for ( int32 Step = 0; Step < ExtraSteps; ++Step ) Playback->Update( 0.1f );
	}

	// PlaybackのRecord窓口でcommand bufferを作る。
	FDrawBuffer RecordMeshPlayback( const TSharedPtr<IEffectPlayback>& Playback )
	{
		// 一回の記録だけを保持するbuffer。
		FDrawBuffer Buffer;
		// bufferへ記録する既存窓口。
		FDrawContext Draw( Buffer );
		if ( Playback.IsValid() ) Playback->Record( Draw );
		return Buffer;
	}

	// 指定commandの件数を返す。
	int32 CountMeshCommands( const FDrawBuffer& Buffer, EDrawCommand Type )
	{
		// 見つかったcommand数。
		int32 Count = 0;
		if ( Buffer.Words.empty() ) return Count;
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == Type ) ++Count; } );
		return Count;
	}

	// 最初のmesh commandのheaderと先頭頂点を安全にコピーする。
	bool ReadFirstMaterialMesh( const FDrawBuffer& Buffer, FDrawMaterialMeshData& OutData, FMeshVertex& OutVertex )
	{
		// command payloadを見つけたかを返す状態。
		bool bFound = false;
		if ( Buffer.Words.empty() ) return false;
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
		{
			if ( bFound || Command.Type != EDrawCommand::MaterialMesh3D || Command.NumDataWords < static_cast<int32>( sizeof( FDrawMaterialMeshData ) / 4 + sizeof( FMeshVertex ) / 4 ) ) return;
			OutData = Command.Read<FDrawMaterialMeshData>();
			// header直後に連続する最初の頂点。
			const uint8* Payload = reinterpret_cast<const uint8*>( Command.Data );
			std::memcpy( &OutVertex, Payload + sizeof( FDrawMaterialMeshData ), sizeof( FMeshVertex ) );
			bFound = true;
		} );
		return bFound;
	}

	// アセットmanagerが遅延解放できる状態までfake textureを回収する。
	void CollectMeshTextures( CAssetManager& Assets )
	{
		Assets.ReleaseUnused();
		Assets.Tick( 0.0f );
	}

	// 再生を停止してから所有資源を解放する。
	void ReleaseMeshPlayback( TSharedPtr<IEffectPlayback>& Playback )
	{
		if ( Playback.IsValid() ) Playback->Stop( true );
		Playback.Reset();
	}

	// Record中にtexture読込・ready照会・handle作成が起きていないか比較する。
	bool SameMeshResourceCounters( const NAssetTest::FFakeAssetBackend& Fake, int32 Loads, int32 ReadyQueries, int32 RegionCalls, int32 UvCalls, int32 NextHandle, usize TextureCount )
	{
		return Fake.NumTextureLoads == Loads && Fake.NumLoadingQueries == ReadyQueries && Fake.NumTextureRegionCalls == RegionCalls && Fake.NumTextureUvCalls == UvCalls && Fake.Next == NextHandle && Fake.Textures.size() == TextureCount;
	}

	// PlaneとRingのfactory再生が画像を開始時に固定しmesh commandだけを記録する。
	void TestReadyMeshResources( NAssetTest::FChecker& C, CAssetManager& Assets, NAssetTest::FFakeAssetBackend& Fake, const FString& Dir )
	{
		// 形状ごとに分離した三画像の相対path。
		const FString MainPaths[] = { "mesh-resource/ready-plane-main.bmp", "mesh-resource/ready-ring-main.bmp" };
		const FString MaskPaths[] = { "mesh-resource/ready-plane-mask.bmp", "mesh-resource/ready-ring-mask.bmp" };
		const FString NoisePaths[] = { "mesh-resource/ready-plane-noise.bmp", "mesh-resource/ready-ring-noise.bmp" };
		// 二形状分のfixture保存結果。
		bool bFixturesSaved = true;
		for ( int32 Index = 0; Index < 2; ++Index ) bFixturesSaved &= WriteMeshTextureFixture( Dir, MainPaths[Index] ) && WriteMeshTextureFixture( Dir, MaskPaths[Index] ) && WriteMeshTextureFixture( Dir, NoisePaths[Index] );
		C.Check( bFixturesSaved, "PlaneとRingのmain/mask/noise画像fixtureを保存する" );
		if ( !bFixturesSaved ) return;
		// PlaneとRingを同じ検査順で扱うkind列。
		const FEffectMeshDefinition::EKind Kinds[] = { FEffectMeshDefinition::EKind::Plane, FEffectMeshDefinition::EKind::Ring };
		for ( int32 Index = 0; Index < 2; ++Index )
		{
			// 今回検証する形状と画像のemitter。
			const FParticleEmitterDefinition Emitter = MakeMeshResourceEmitter( MainPaths[Index], MaskPaths[Index], NoisePaths[Index], Kinds[Index], true );
			// factoryへ渡す保存可能なdefinition。
			const TSharedPtr<FParticleEffectDefinition> Definition = NEffectTest::MakeEffect( { Emitter }, 0.0f, true, 1200u + static_cast<uint32>( Index ) );
			// factoryが始まる前の読み込み数。
			const int32 LoadsBefore = Fake.NumTextureLoads;
			// factoryが作る再生と開始時資源snapshot。
			TSharedPtr<IEffectPlayback> Playback = NEffect::CreateParticlePlayback( Definition, &Assets, FEffectPlayParams(), FTransform::Identity, "mesh-resource-ready" );
			C.Check( Definition.IsValid() && !FParticleEffectDefinition::HasErrors( Definition->Validate() ), "ready mesh fixtureの定義が有効 (kind=%d)", static_cast<int32>( Kinds[Index] ) );
			C.Check( Playback.IsValid() && Fake.NumTextureLoads == LoadsBefore + 3, "mesh材質がmain/mask/noiseを開始時に読む (kind=%d loaded=%d)", static_cast<int32>( Kinds[Index] ), Fake.NumTextureLoads - LoadsBefore );
			AdvanceMeshPlayback( Playback );
			// Record前に記録する外部資源操作数。
			const int32 LoadsBeforeRecord = Fake.NumTextureLoads;
			const int32 QueriesBeforeRecord = Fake.NumLoadingQueries;
			const int32 RegionsBeforeRecord = Fake.NumTextureRegionCalls;
			const int32 UvBeforeRecord = Fake.NumTextureUvCalls;
			const int32 NextBeforeRecord = Fake.Next;
			const usize HandlesBeforeRecord = Fake.Textures.size();
			// 実際の再生が出したcommand列。
			const FDrawBuffer Buffer = RecordMeshPlayback( Playback );
			// commandから取り出す固定headerと先頭頂点。
			FDrawMaterialMeshData Data;
			FMeshVertex FirstVertex;
			const bool bReadMesh = ReadFirstMaterialMesh( Buffer, Data, FirstVertex );
			C.Check( Playback.IsValid() && Playback->GetNumParticles() > 0 && CountMeshCommands( Buffer, EDrawCommand::MaterialMesh3D ) == 1 && CountMeshCommands( Buffer, EDrawCommand::Quad3D ) == 0 && CountMeshCommands( Buffer, EDrawCommand::MaterialQuad3D ) == 0 && bReadMesh, "Plane/Ring再生はmesh commandを一つ記録しquadへfallbackしない (kind=%d particles=%d)", static_cast<int32>( Kinds[Index] ), Playback.IsValid() ? Playback->GetNumParticles() : -1 );
			C.Check( bReadMesh && Data.Texture >= 0 && Data.Material.MaskTexture >= 0 && Data.DistortionTexture >= 0 && Data.NumVertices >= 3 && Data.NumIndices >= 3 && NMath::IsFinite( FirstVertex.Position[0] ), "ready mesh commandはmain/mask/noise handleと有効なgeometryを保持する (kind=%d)", static_cast<int32>( Kinds[Index] ) );
			C.Check( SameMeshResourceCounters( Fake, LoadsBeforeRecord, QueriesBeforeRecord, RegionsBeforeRecord, UvBeforeRecord, NextBeforeRecord, HandlesBeforeRecord ), "mesh Recordはtexture読込・ready照会・handle作成を行わない (kind=%d)", static_cast<int32>( Kinds[Index] ) );
			ReleaseMeshPlayback( Playback );
			CollectMeshTextures( Assets );
		}

		// 同じassetを共有する二再生で、寿命値とワールド位置を個別に記録するfixture。
		const FString SharedMain = "mesh-resource/shared-main.bmp";
		const FString SharedMask = "mesh-resource/shared-mask.bmp";
		const FString SharedNoise = "mesh-resource/shared-noise.bmp";
		// 共有textureの実在fixture保存結果。
		const bool bSharedFixtures = WriteMeshTextureFixture( Dir, SharedMain ) && WriteMeshTextureFixture( Dir, SharedMask ) && WriteMeshTextureFixture( Dir, SharedNoise );
		C.Check( bSharedFixtures, "二再生で共有するmesh texture fixtureを保存する" );
		if ( !bSharedFixtures ) return;
		// 二再生が参照する共通emitter設定。
		const FParticleEmitterDefinition SharedEmitter = MakeMeshResourceEmitter( SharedMain, SharedMask, SharedNoise, FEffectMeshDefinition::EKind::Plane, true );
		// 一再生目のimmutable definition。
		const TSharedPtr<FParticleEffectDefinition> FirstDefinition = NEffectTest::MakeEffect( { SharedEmitter }, 0.0f, true, 1210u );
		// 二再生目のimmutable definition。
		const TSharedPtr<FParticleEffectDefinition> SecondDefinition = NEffectTest::MakeEffect( { SharedEmitter }, 0.0f, true, 1210u );
		// 同じテクスチャのcacheを共有する先行Playback。
		TSharedPtr<IEffectPlayback> First = NEffect::CreateParticlePlayback( FirstDefinition, &Assets, FEffectPlayParams(), FTransform( FVector3( 0.0f, 0.0f, 0.0f ) ), "mesh-resource-shared-first" );
		// 位置と更新時間を分ける後続Playback。
		TSharedPtr<IEffectPlayback> Second = NEffect::CreateParticlePlayback( SecondDefinition, &Assets, FEffectPlayParams(), FTransform( FVector3( 6.0f, 0.0f, 0.0f ) ), "mesh-resource-shared-second" );
		AdvanceMeshPlayback( First );
		AdvanceMeshPlayback( Second, 4 );
		// 先行Playbackのmesh出力。
		const FDrawBuffer FirstBuffer = RecordMeshPlayback( First );
		// 後続Playbackのmesh出力。
		const FDrawBuffer SecondBuffer = RecordMeshPlayback( Second );
		// 二再生のheaderと頂点を別々に読む。
		FDrawMaterialMeshData FirstData, SecondData;
		FMeshVertex FirstVertex, SecondVertex;
		const bool bFirstRead = ReadFirstMaterialMesh( FirstBuffer, FirstData, FirstVertex );
		const bool bSecondRead = ReadFirstMaterialMesh( SecondBuffer, SecondData, SecondVertex );
		C.Check( First.IsValid() && Second.IsValid() && bFirstRead && bSecondRead, "共有資源の二Playbackがそれぞれmesh commandを記録する" );
		C.Check( bFirstRead && bSecondRead && FirstData.Texture == SecondData.Texture && FirstData.Material.MaskTexture == SecondData.Material.MaskTexture && FirstData.DistortionTexture == SecondData.DistortionTexture, "二Playbackは同じready main/mask/noise handleを共有する" );
		C.Check( bFirstRead && bSecondRead && NMath::Abs( FirstVertex.Position[0] - SecondVertex.Position[0] ) > 4.0f && FirstData.Emission != SecondData.Emission, "二PlaybackのtransformとAge由来Emissionは個別に反映される (x=%.3f/%.3f emission=%.3f/%.3f)", FirstVertex.Position[0], SecondVertex.Position[0], FirstData.Emission, SecondData.Emission );
		ReleaseMeshPlayback( First );
		ReleaseMeshPlayback( Second );
		CollectMeshTextures( Assets );
	}

	// zero Strengthとmissing noiseが異なる描画結果になることを確認する。
	void TestMeshNoiseReadiness( NAssetTest::FChecker& C, CAssetManager& Assets, NAssetTest::FFakeAssetBackend& Fake, const FString& Dir )
	{
		// main/mask/noiseの実在fixture path。
		const FString MainPath = "mesh-resource/zero-main.bmp";
		const FString MaskPath = "mesh-resource/zero-mask.bmp";
		const FString NoisePath = "mesh-resource/zero-noise.bmp";
		// fixtureを書き出した結果。
		const bool bFixturesSaved = WriteMeshTextureFixture( Dir, MainPath ) && WriteMeshTextureFixture( Dir, MaskPath ) && WriteMeshTextureFixture( Dir, NoisePath );
		C.Check( bFixturesSaved, "zero Strength用に実在するnoise画像を保存する" );
		if ( !bFixturesSaved ) return;
		// noise画像は実在するが強度を全寿命0にしたemitter。
		const FParticleEmitterDefinition ZeroEmitter = MakeMeshResourceEmitter( MainPath, MaskPath, NoisePath, FEffectMeshDefinition::EKind::Ring, false );
		// factory前のtexture読み込み回数。
		const int32 ZeroLoadsBefore = Fake.NumTextureLoads;
		// zero Strengthを開始時に評価する再生。
		TSharedPtr<IEffectPlayback> ZeroPlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { ZeroEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "mesh-resource-zero-noise" );
		C.Check( ZeroPlayback.IsValid() && Fake.NumTextureLoads == ZeroLoadsBefore + 2, "Strength全zeroなら実在noiseを読まずmainとmaskだけ読む (loads=%d)", Fake.NumTextureLoads - ZeroLoadsBefore );
		AdvanceMeshPlayback( ZeroPlayback );
		// zero-strength meshの実command。
		const FDrawBuffer ZeroBuffer = RecordMeshPlayback( ZeroPlayback );
		// zero-strength mesh payload。
		FDrawMaterialMeshData ZeroData;
		FMeshVertex ZeroVertex;
		const bool bZeroRead = ReadFirstMaterialMesh( ZeroBuffer, ZeroData, ZeroVertex );
		C.Check( bZeroRead && CountMeshCommands( ZeroBuffer, EDrawCommand::MaterialMesh3D ) == 1 && ZeroData.DistortionStrength == 0.0f && ZeroData.DistortionTexture == -1, "zero Strengthでもmeshは描き、noise handleを持たない" );
		ReleaseMeshPlayback( ZeroPlayback );
		CollectMeshTextures( Assets );

		// noiseの読込だけ失敗させる実在fixtureと設定。
		const FString MissingMain = "mesh-resource/missing-noise-main.bmp";
		const FString MissingMask = "mesh-resource/missing-noise-mask.bmp";
		const FString FailedNoise = "mesh-resource/missing-noise-source.bmp";
		// fake読込失敗を正しいslotで注入するため三画像を作る。
		const bool bMissingFixtures = WriteMeshTextureFixture( Dir, MissingMain ) && WriteMeshTextureFixture( Dir, MissingMask ) && WriteMeshTextureFixture( Dir, FailedNoise );
		C.Check( bMissingFixtures, "noise失敗試験用main/mask/noise fixtureを保存する" );
		if ( !bMissingFixtures ) return;
		// 正のStrengthを保ったmesh emitter。
		const FParticleEmitterDefinition MissingEmitter = MakeMeshResourceEmitter( MissingMain, MissingMask, FailedNoise, FEffectMeshDefinition::EKind::Plane, true );
		// noiseだけを失敗させる絶対load番号。
		const int32 PreviousFailure = Fake.FailTextureLoadCall;
		// このfixtureを開始する前のtexture読込回数。
		const int32 MissingLoadsBefore = Fake.NumTextureLoads;
		Fake.FailTextureLoadCall = Fake.NumTextureLoads + 3;
		// factoryがnoise欠落をsnapshotする再生。
		TSharedPtr<IEffectPlayback> MissingPlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { MissingEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "mesh-resource-missing-noise" );
		Fake.FailTextureLoadCall = PreviousFailure;
		C.Check( MissingPlayback.IsValid(), "noise読込失敗でも再生objectを作る" );
		AdvanceMeshPlayback( MissingPlayback );
		// noiseがない場合の再生command列。
		const FDrawBuffer MissingBuffer = RecordMeshPlayback( MissingPlayback );
		C.Check( Fake.NumTextureLoads == MissingLoadsBefore + 3 && CountMeshCommands( MissingBuffer, EDrawCommand::MaterialMesh3D ) == 0 && CountMeshCommands( MissingBuffer, EDrawCommand::Quad3D ) == 0 && CountMeshCommands( MissingBuffer, EDrawCommand::MaterialQuad3D ) == 0, "正のStrengthでnoiseが欠けたmeshは描画せずquadへfallbackしない (loads=%d)", Fake.NumTextureLoads - MissingLoadsBefore );
		ReleaseMeshPlayback( MissingPlayback );
		CollectMeshTextures( Assets );
	}

	// public constructorがpending資源を再生開始時の無効handleとして固定する。
	void TestDirectPendingMeshResources( NAssetTest::FChecker& C, CAssetManager& Assets, NAssetTest::FFakeAssetBackend& Fake, const FString& Dir )
	{
		// pending main/mask/noiseそれぞれの独立path。
		const FString MainPendingPath = "mesh-resource/pending-main.bmp";
		const FString MaskPendingPath = "mesh-resource/pending-mask.bmp";
		const FString NoisePendingPath = "mesh-resource/pending-noise.bmp";
		// ready資源とpending資源を含むfixture保存結果。
		const bool bFixturesSaved = WriteMeshTextureFixture( Dir, MainPendingPath ) && WriteMeshTextureFixture( Dir, MaskPendingPath ) && WriteMeshTextureFixture( Dir, NoisePendingPath );
		C.Check( bFixturesSaved, "direct constructorのpending画像fixtureを保存する" );
		if ( !bFixturesSaved ) return;
		// Plane用の同一mesh定義。
		const FParticleEmitterDefinition Emitter = MakeMeshResourceEmitter( MainPendingPath, MaskPendingPath, NoisePendingPath, FEffectMeshDefinition::EKind::Plane, true );
		// constructorとfactoryで共有する定義。
		const TSharedPtr<FParticleEffectDefinition> Definition = NEffectTest::MakeEffect( { Emitter } );

		// mainだけpendingにして開始時handleを固定する。
		{
			// 直接渡すpending mainとready mask/noise。
			TSharedPtr<CTexture> Main = Assets.LoadTexture( MainPendingPath, true );
			TSharedPtr<CTexture> Mask = Assets.LoadTexture( MaskPendingPath, false );
			TSharedPtr<CTexture> Noise = Assets.LoadTexture( NoisePendingPath, false );
			// mainの後からready化を追跡するhandle。
			const int32 MainHandle = Main.IsValid() ? Main->GetHandle() : -1;
			// constructorへ渡すmain画像配列。
			TArray<TSharedPtr<CTexture>> Textures;
			Textures.Add( Main );
			// constructorへ渡すmask配列。
			TArray<TSharedPtr<CTexture>> Masks;
			Masks.Add( Mask );
			// constructorへ渡す空のflipbook配列。
			TArray<FEffectFlipbookFrames> Frames;
			Frames.Add( FEffectFlipbookFrames() );
			// constructorへ渡すnoise配列。
			TArray<TSharedPtr<CTexture>> Noises;
			Noises.Add( Noise );
			// seven-argument constructorの直接結果。
			TSharedPtr<FParticlePlayback> Playback;
			if ( Definition.IsValid() && Main.IsValid() && Mask.IsValid() && Noise.IsValid() ) Playback = MakeShared<FParticlePlayback>( Definition, Textures, FEffectPlayParams(), FTransform::Identity, Masks, Frames, Noises );
			C.Check( Playback.IsValid() && Main.IsValid() && !Main->IsReady(), "main画像がpendingのままpublic constructorへ渡る" );
			Fake.Loading.erase( MainHandle );
			AdvanceMeshPlayback( Playback );
			// ready化後もmain handle snapshotを採用しないcommand列。
			const FDrawBuffer Buffer = RecordMeshPlayback( Playback );
			C.Check( Playback.IsValid() && CountMeshCommands( Buffer, EDrawCommand::MaterialMesh3D ) == 0 && CountMeshCommands( Buffer, EDrawCommand::Quad3D ) == 0, "main画像は開始時pendingなら後からreadyでもmeshへ採用しない" );
			if ( Playback.IsValid() ) Playback->Stop( true );
			Playback.Reset();
			Textures.Reset(); Masks.Reset(); Noises.Reset(); Main.Reset(); Mask.Reset(); Noise.Reset();
			CollectMeshTextures( Assets );
		}

		// maskだけpendingならprocedural -1を固定し、後からreadyに変わってもhandleを差し替えない。
		{
			// ready main/noiseとpending mask。
			TSharedPtr<CTexture> Main = Assets.LoadTexture( MainPendingPath, false );
			TSharedPtr<CTexture> Mask = Assets.LoadTexture( MaskPendingPath, true );
			TSharedPtr<CTexture> Noise = Assets.LoadTexture( NoisePendingPath, false );
			// pending maskの後からready化に使うhandle。
			const int32 MaskHandle = Mask.IsValid() ? Mask->GetHandle() : -1;
			// main textureのconstructor配列。
			TArray<TSharedPtr<CTexture>> Textures;
			Textures.Add( Main );
			// pending maskのconstructor配列。
			TArray<TSharedPtr<CTexture>> Masks;
			Masks.Add( Mask );
			// flipbook未使用の配列。
			TArray<FEffectFlipbookFrames> Frames;
			Frames.Add( FEffectFlipbookFrames() );
			// ready noiseのconstructor配列。
			TArray<TSharedPtr<CTexture>> Noises;
			Noises.Add( Noise );
			// pending mask snapshotを作る直接再生。
			TSharedPtr<FParticlePlayback> Playback;
			if ( Definition.IsValid() && Main.IsValid() && Mask.IsValid() && Noise.IsValid() ) Playback = MakeShared<FParticlePlayback>( Definition, Textures, FEffectPlayParams(), FTransform::Identity, Masks, Frames, Noises );
			Fake.Loading.erase( MaskHandle );
			AdvanceMeshPlayback( Playback );
			// pending時のprocedural mask snapshotを含むmesh command。
			const FDrawBuffer Buffer = RecordMeshPlayback( Playback );
			FDrawMaterialMeshData Data;
			FMeshVertex Vertex;
			const bool bReadMesh = ReadFirstMaterialMesh( Buffer, Data, Vertex );
			C.Check( Playback.IsValid() && bReadMesh && Data.Material.MaskTexture == -1, "maskは開始時pendingならprocedural handle -1を固定する" );
			// maskが後からreadyになった状態での二回目の記録。
			const FDrawBuffer ReadyLaterBuffer = RecordMeshPlayback( Playback );
			FDrawMaterialMeshData ReadyLaterData;
			FMeshVertex ReadyLaterVertex;
			const bool bReadyLaterMesh = ReadFirstMaterialMesh( ReadyLaterBuffer, ReadyLaterData, ReadyLaterVertex );
			C.Check( bReadyLaterMesh && ReadyLaterData.Material.MaskTexture == -1, "maskが後からreadyでも既存Playbackのprocedural snapshotを保つ" );
			if ( Playback.IsValid() ) Playback->Stop( true );
			Playback.Reset();
			Textures.Reset(); Masks.Reset(); Noises.Reset(); Main.Reset(); Mask.Reset(); Noise.Reset();
			CollectMeshTextures( Assets );
		}

		// noiseだけpendingなら開始時に破棄し、後からreadyでも強度付きmeshを描かない。
		{
			// ready main/maskとpending noise。
			TSharedPtr<CTexture> Main = Assets.LoadTexture( MainPendingPath, false );
			TSharedPtr<CTexture> Mask = Assets.LoadTexture( MaskPendingPath, false );
			TSharedPtr<CTexture> Noise = Assets.LoadTexture( NoisePendingPath, true );
			// noiseを後からreadyへ移すhandle。
			const int32 NoiseHandle = Noise.IsValid() ? Noise->GetHandle() : -1;
			// main textureのconstructor配列。
			TArray<TSharedPtr<CTexture>> Textures;
			Textures.Add( Main );
			// mask textureのconstructor配列。
			TArray<TSharedPtr<CTexture>> Masks;
			Masks.Add( Mask );
			// 空のflipbook配列。
			TArray<FEffectFlipbookFrames> Frames;
			Frames.Add( FEffectFlipbookFrames() );
			// pending noiseのconstructor配列。
			TArray<TSharedPtr<CTexture>> Noises;
			Noises.Add( Noise );
			// pending noiseを渡す直接再生。
			TSharedPtr<FParticlePlayback> Playback;
			if ( Definition.IsValid() && Main.IsValid() && Mask.IsValid() && Noise.IsValid() ) Playback = MakeShared<FParticlePlayback>( Definition, Textures, FEffectPlayParams(), FTransform::Identity, Masks, Frames, Noises );
			Fake.Loading.erase( NoiseHandle );
			AdvanceMeshPlayback( Playback );
			// noise ready化後の記録結果。
			const FDrawBuffer Buffer = RecordMeshPlayback( Playback );
			C.Check( Playback.IsValid() && CountMeshCommands( Buffer, EDrawCommand::MaterialMesh3D ) == 0 && CountMeshCommands( Buffer, EDrawCommand::Quad3D ) == 0, "noiseは開始時pendingなら後からreadyでもmesh/quadへ採用しない" );
			if ( Playback.IsValid() ) Playback->Stop( true );
			Playback.Reset();
			Textures.Reset(); Masks.Reset(); Noises.Reset(); Main.Reset(); Mask.Reset(); Noise.Reset();
			CollectMeshTextures( Assets );
		}
	}

	// Mesh値域の境界と、失敗時にgeometry出力を保つ契約を確認する。
	void TestMeshBoundaries( NAssetTest::FChecker& C )
	{
		C.Group = "effect-mesh-value-boundaries";
		// 下限の分割数を検査する設定。
		FEffectMeshDefinition MinimumSegments;
		MinimumSegments.Kind = FEffectMeshDefinition::EKind::Ring;
		MinimumSegments.Segments = 3;
		// 上限の分割数を検査する設定。
		FEffectMeshDefinition MaximumSegments = MinimumSegments;
		MaximumSegments.Segments = 128;
		// 下限未満の分割数を検査する設定。
		FEffectMeshDefinition BelowSegments = MinimumSegments;
		BelowSegments.Segments = 2;
		// 上限を一つ超えた分割数を検査する設定。
		FEffectMeshDefinition AboveSegments = MaximumSegments;
		AboveSegments.Segments = 129;
		C.Check( MinimumSegments.IsValidSettings() && MinimumSegments.Validate().IsEmpty() && MaximumSegments.IsValidSettings() && MaximumSegments.Validate().IsEmpty(), "MeshのSegments境界3と128を受け付ける" );
		C.Check( !BelowSegments.IsValidSettings() && !BelowSegments.Validate().IsEmpty() && !AboveSegments.IsValidSettings() && !AboveSegments.Validate().IsEmpty(), "MeshのSegments境界外2と129を拒否する" );
		// 最小の内半径を検査する設定。
		FEffectMeshDefinition MinimumRadius = MinimumSegments;
		MinimumRadius.InnerRadius = 0.05f;
		// 最大の内半径を検査する設定。
		FEffectMeshDefinition MaximumRadius = MinimumSegments;
		MaximumRadius.InnerRadius = 0.95f;
		// float32表現で下限を一つ下回る内半径。
		FEffectMeshDefinition BelowRadius = MinimumRadius;
		BelowRadius.InnerRadius = std::nextafter( 0.05f, 0.0f );
		// float32表現で上限を一つ上回る内半径。
		FEffectMeshDefinition AboveRadius = MaximumRadius;
		AboveRadius.InnerRadius = std::nextafter( 0.95f, 1.0f );
		C.Check( MinimumRadius.IsValidSettings() && MinimumRadius.Validate().IsEmpty() && MaximumRadius.IsValidSettings() && MaximumRadius.Validate().IsEmpty(), "MeshのInnerRadius境界0.05と0.95を受け付ける" );
		C.Check( !BelowRadius.IsValidSettings() && !BelowRadius.Validate().IsEmpty() && !AboveRadius.IsValidSettings() && !AboveRadius.Validate().IsEmpty(), "MeshのInnerRadiusを0.05未満と0.95超で拒否する" );
		// 各軸で許される最大角度を設定する値。
		FEffectMeshDefinition MaximumRotation = MinimumSegments;
		MaximumRotation.RotationDegrees = FVector3( 360000.0f, -360000.0f, 360000.0f );
		// 反対符号側の境界角度を設定する値。
		FEffectMeshDefinition NegativeMaximumRotation = MinimumSegments;
		NegativeMaximumRotation.RotationDegrees = FVector3( -360000.0f, 360000.0f, -360000.0f );
		C.Check( MaximumRotation.IsValidSettings() && MaximumRotation.Validate().IsEmpty() && NegativeMaximumRotation.IsValidSettings() && NegativeMaximumRotation.Validate().IsEmpty(), "Mesh回転の各軸で±360000度を受け付ける" );
		// 各軸を最大値の次の表現可能floatへ進めた設定。
		const FVector3 InvalidRotations[] = {
			FVector3( std::nextafter( 360000.0f, TNumericLimits<float32>::Infinity() ), 0.0f, 0.0f ),
			FVector3( std::nextafter( -360000.0f, -TNumericLimits<float32>::Infinity() ), 0.0f, 0.0f ),
			FVector3( 0.0f, std::nextafter( 360000.0f, TNumericLimits<float32>::Infinity() ), 0.0f ),
			FVector3( 0.0f, std::nextafter( -360000.0f, -TNumericLimits<float32>::Infinity() ), 0.0f ),
			FVector3( 0.0f, 0.0f, std::nextafter( 360000.0f, TNumericLimits<float32>::Infinity() ) ),
			FVector3( 0.0f, 0.0f, std::nextafter( -360000.0f, -TNumericLimits<float32>::Infinity() ) ),
			FVector3( TNumericLimits<float32>::QuietNaN(), 0.0f, 0.0f ),
			FVector3( 0.0f, TNumericLimits<float32>::Infinity(), 0.0f ),
		};
		// 非有限または上限超過を個別設定で検証する集約結果。
		bool bEveryRotationRejected = true;
		for ( const FVector3& Rotation : InvalidRotations )
		{
			// 一軸だけを不正にした形状設定。
			FEffectMeshDefinition InvalidRotation = MinimumSegments;
			InvalidRotation.RotationDegrees = Rotation;
			bEveryRotationRejected = bEveryRotationRejected && !InvalidRotation.IsValidSettings() && !InvalidRotation.Validate().IsEmpty();
		}
		C.Check( bEveryRotationRejected, "Mesh回転の各軸で360000度超・NaN・Infを拒否する" );
		// Kind=Noneでも保存されているmesh値を検証する設定。
		FEffectMeshDefinition InvalidDisabledMesh;
		InvalidDisabledMesh.Segments = 129;
		C.Check( !InvalidDisabledMesh.IsValidSettings() && !InvalidDisabledMesh.Validate().IsEmpty(), "MeshがNoneでも範囲外の保存値を拒否する" );
		// 失敗時の出力比較に使う既存のPlane geometry。
		FEffectMeshGeometry Output;
		FEffectMeshDefinition Plane;
		Plane.Kind = FEffectMeshDefinition::EKind::Plane;
		const bool bPlaneBuilt = NEffectMesh::BuildGeometry( Plane, Output );
		// 不正呼出し前に保持する頂点列。
		const TArray<FMeshVertex> OriginalVertices = Output.Vertices;
		// 不正呼出し前に保持するindex列。
		const TArray<uint32> OriginalIndices = Output.Indices;
		// 不正分割のRing設定。
		FEffectMeshDefinition InvalidSegments = MinimumSegments;
		InvalidSegments.Segments = 129;
		const bool bRejectedSegments = !NEffectMesh::BuildGeometry( InvalidSegments, Output );
		// 不正半径のRing設定。
		FEffectMeshDefinition InvalidRadius = MinimumSegments;
		InvalidRadius.InnerRadius = AboveRadius.InnerRadius;
		const bool bRejectedRadius = !NEffectMesh::BuildGeometry( InvalidRadius, Output );
		// 失敗した二回の呼出し後も元の全頂点・index byteが保たれたか。
		const bool bOutputPreserved = bPlaneBuilt && bRejectedSegments && bRejectedRadius && Output.Vertices.Num() == OriginalVertices.Num() && Output.Indices.Num() == OriginalIndices.Num() && std::memcmp( Output.Vertices.GetData(), OriginalVertices.GetData(), static_cast<size_t>( OriginalVertices.Num() ) * sizeof( FMeshVertex ) ) == 0 && std::memcmp( Output.Indices.GetData(), OriginalIndices.GetData(), static_cast<size_t>( OriginalIndices.Num() ) * sizeof( uint32 ) ) == 0;
		C.Check( bOutputPreserved, "Segments129とInnerRadius超過のBuildGeometry失敗は既存頂点・indexをbyte保持する" );
	}
}

// mesh材質画像の開始snapshot、再生分離、欠落時skipとfake資源解放を確認する。
void NWorldTest::RunEffectMeshResourceTests( NAssetTest::FChecker& C, CAssetManager& Assets, NAssetTest::FFakeAssetBackend& Fake, const FString& Dir )
{
	C.Group = "effect-mesh-resources";
	TestMeshBoundaries( C );
	C.Group = "effect-mesh-resources";
	// 呼出し元が使っているasset rootを保存する。
	const FString PreviousRoot = Assets.GetRootDirectory();
	Assets.SetRootDirectory( Dir );
	// 前テストの遅延解放資源を片付けて基準集合を安定させる。
	CollectMeshTextures( Assets );
	// cleanup後に残る初期fake texture handle。
	const std::set<int32> InitialTextures = Fake.Textures;
	TestReadyMeshResources( C, Assets, Fake, Dir );
	TestMeshNoiseReadiness( C, Assets, Fake, Dir );
	TestDirectPendingMeshResources( C, Assets, Fake, Dir );
	CollectMeshTextures( Assets );
	// handleの追加・欠落を両方検出するcleanup結果。
	const bool bTextureSetRestored = Fake.Textures == InitialTextures;
	C.Check( bTextureSetRestored, "mesh resource tests後にfake texture handle集合を初期状態へ戻す (initial=%d current=%d)", static_cast<int32>( InitialTextures.size() ), static_cast<int32>( Fake.Textures.size() ) );
	Assets.SetRootDirectory( PreviousRoot );
}
