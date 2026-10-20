// SPDX-License-Identifier: NOASSERTION
#include "WorldMeshMaterialSelfTestSupport.h"

#include <vector>

#include "Library/Core/Interfaces/Render/DrawMaterialMesh.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Render/MeshMaterialEffectResources.h"
#include "Library/Framework/World/Mesh/EditMeshPrimitives.h"
#include "Library/Framework/World/WorldRender.h"
#include "Library/Platform/FileSystem.h"

namespace
{
	// 読み込み経路だけを検査する2x2の白TGAを作る。
	std::vector<uint8> MakeResourceTga()
	{
		std::vector<uint8> Bytes = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 32, 0x28 };
		for ( int32 Index = 0; Index < 4; ++Index ) { Bytes.push_back( 255 ); Bytes.push_back( 255 ); Bytes.push_back( 255 ); Bytes.push_back( 255 ); }
		return Bytes;
	}

	// 同一の編集面へ指定slot設定を付けてrenderer準備へ渡す。
	FEditMesh MakeResourceMesh( const FString& Source, const FString& Mask, const FString& Noise )
	{
		FEditMesh Mesh = NEditMesh::MakePlane( 2.0f, 2.0f );
		FEditMeshMaterial& Material = Mesh.Materials[Mesh.Materials.Add( FEditMeshMaterial() )];
		Material.Texture = Source;
		Material.Effect.Material.bEnabled = true;
		Material.Effect.Material.bDissolve = true;
		Material.Effect.Material.MaskTexture = Mask;
		Material.Effect.Material.Distortion.bEnabled = true;
		Material.Effect.Material.Distortion.Texture = Noise;
		Material.Effect.Material.Distortion.Strength.Keys.Add( { 0.0f, 0.5f } );
		return Mesh;
	}

	// fake backendに残っているmesh bufferが基準集合と同じか調べる。
	bool HasSameMeshBuffers( const NAssetTest::FFakeAssetBackend& Fake, const std::set<int32>& VertexBuffers, const std::set<int32>& IndexBuffers )
	{
		return Fake.VertexBuffers == VertexBuffers && Fake.IndexBuffers == IndexBuffers;
	}

	// asset managerが所有を解放した資源をfake backendへ反映する。
	void CollectMeshMaterialResources( CAssetManager& Assets )
	{
		Assets.ReleaseUnused();
		Assets.Tick( 0.0f );
	}
}

// fake backendで読み込み時固定、失敗時skip、Record時の純粋性と寿命を確かめる。
void NWorldTest::RunMeshMaterialResourceTests( NAssetTest::FChecker& C, CAssetManager& Assets, NAssetTest::FFakeAssetBackend& Fake, const FString& Dir )
{
	C.Group = "world-mesh-material-effect-resources";
	const FString PreviousRoot = Assets.GetRootDirectory();
	Assets.SetRootDirectory( Dir );
	const int32 InitialTextureLoadCount = Fake.NumTextureLoads;
	const FString SourcePath = "mesh-material-resource/source.tga";
	const FString MaskPath = "mesh-material-resource/mask.tga";
	const FString NoisePath = "mesh-material-resource/noise.tga";
	const bool bFixturesSaved = NFileSystem::EnsureDirectory( Dir / "mesh-material-resource" ) && NAssetTest::WriteFileBytes( Dir / SourcePath, MakeResourceTga() ) && NAssetTest::WriteFileBytes( Dir / MaskPath, MakeResourceTga() ) && NAssetTest::WriteFileBytes( Dir / NoisePath, MakeResourceTga() );
	C.Check( bFixturesSaved, "source・mask・noiseのTGA fixtureをsandboxへ保存する" );
	if ( !bFixturesSaved ) { Assets.SetRootDirectory( PreviousRoot ); return; }
	const std::set<int32> InitialTextures = Fake.Textures;
	const std::set<int32> InitialVertexBuffers = Fake.VertexBuffers;
	const std::set<int32> InitialIndexBuffers = Fake.IndexBuffers;

	// 共有済みsourceにmask/noiseを足す成功時の読込回数とhandleを検査する。
	FMeshRenderer Renderer = NWorld::MakeMeshRenderer( MakeResourceMesh( SourcePath, MaskPath, NoisePath ), [&Assets]( const FString& Path ) { return Assets.LoadTexture( Path ); } );
	C.Check( Renderer.IsValid() && !Renderer.HasEffectPreparationFailures(), "readyなsource・mask・noiseからeffect資源とmeshを準備する" );
	C.Check( Fake.NumTextureLoads == InitialTextureLoadCount + 3 && Renderer.Batches.Num() == 1 && Renderer.Batches[0].EffectResources.IsValid(), "同一sourceは通常描画とeffect準備で共有し画像を3回だけ読む" );
	if ( Renderer.Batches.Num() == 1 && Renderer.Batches[0].EffectResources.IsValid() )
	{
		const FMeshMaterialEffectResources& Resources = *Renderer.Batches[0].EffectResources;
		C.Check( Resources.TextureHandle >= 0 && Resources.MaskTextureHandle >= 0 && Resources.DistortionTextureHandle >= 0 && Resources.Texture.IsValid() && Resources.MaskTexture.IsValid() && Resources.DistortionTexture.IsValid(), "必要な3画像のready handleとownerを保持する" );
		C.Check( Resources.Chunks.Num() == 1 && Resources.Chunks[0].Indices.Num() == 6 && Resources.Chunks[0].Vertices.Num() == 4, "resourceは生成時に検証済みchunk形状を固定する" );
	}
	const int32 QueriesBeforeRecord = Fake.NumLoadingQueries;
	const int32 LoadsBeforeRecord = Fake.NumTextureLoads;
	const int32 BuffersBeforeRecord = Fake.NumMeshBuffersMade;
	FDrawBuffer Recorded;
	FDrawContext RecordContext( Recorded );
	Renderer.Record( RecordContext, FTransform::Identity );
	int32 ReadyMaterialCommands = 0;
	if ( !Recorded.Words.empty() ) ForEachDrawCommand( FDrawSpan{ Recorded.Words.data(), static_cast<int32>( Recorded.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::MaterialMesh3D ) ++ReadyMaterialCommands; } );
	FDrawBuffer RecordedAgain;
	FDrawContext RecordAgainContext( RecordedAgain );
	Renderer.Record( RecordAgainContext, FTransform::Identity );
	C.Check( ReadyMaterialCommands >= 1, "ready effect resourceから材質mesh命令を実際に記録する" );
	C.Check( Recorded.Words == RecordedAgain.Words && Fake.NumLoadingQueries == QueriesBeforeRecord && Fake.NumTextureLoads == LoadsBeforeRecord && Fake.NumMeshBuffersMade == BuffersBeforeRecord, "Record反復は同じcommand wordsを保ちready再照会・画像読込・mesh資源作成を行わない" );
	Renderer = FMeshRenderer();
	CollectMeshMaterialResources( Assets );
	C.Check( Fake.Textures == InitialTextures && HasSameMeshBuffers( Fake, InitialVertexBuffers, InitialIndexBuffers ), "最後のrenderer解放後にfake画像とmesh bufferを残さない" );

	// source・mask・noiseが同じpathなら、mesh内で一度だけ読み同じownerを共有する。
	const FString SharedPath = "mesh-material-resource/shared.tga";
	const bool bSharedSaved = NAssetTest::WriteFileBytes( Dir / SharedPath, MakeResourceTga() );
	const int32 SharedLoadsBefore = Fake.NumTextureLoads;
	FMeshRenderer SharedRenderer = NWorld::MakeMeshRenderer( MakeResourceMesh( SharedPath, SharedPath, SharedPath ), [&Assets]( const FString& Path ) { return Assets.LoadTexture( Path ); } );
	const bool bSharedOwner = SharedRenderer.Batches.IsValidIndex( 0 ) && SharedRenderer.Batches[0].EffectResources.IsValid() && SharedRenderer.Batches[0].EffectResources->Texture == SharedRenderer.Batches[0].EffectResources->MaskTexture && SharedRenderer.Batches[0].EffectResources->Texture == SharedRenderer.Batches[0].EffectResources->DistortionTexture;
	C.Check( bSharedSaved && bSharedOwner && Fake.NumTextureLoads == SharedLoadsBefore + 1, "source・mask・noiseの同一pathは一度だけ読み同じtexture ownerを共有する" );
	SharedRenderer = FMeshRenderer();
	CollectMeshMaterialResources( Assets );
	C.Check( Fake.Textures == InitialTextures && HasSameMeshBuffers( Fake, InitialVertexBuffers, InitialIndexBuffers ), "同一path共有slotも最後のowner解放でhandle集合を復元する" );

	// blank sourceは既存asset loader経由の組み込み白画像を保持する。
	const FString WhitePath = "Effects/material_white.tga";
	const bool bWhiteSaved = NFileSystem::EnsureDirectory( Dir / "Effects" ) && NAssetTest::WriteFileBytes( Dir / WhitePath, MakeResourceTga() );
	C.Check( bWhiteSaved, "白画像fixtureを既存の組み込みpathへ保存する" );
	if ( bWhiteSaved )
	{
		FEditMesh WhiteMesh = MakeResourceMesh( FString(), FString(), FString() );
		WhiteMesh.Materials[0].Effect.Material.bDissolve = false;
		WhiteMesh.Materials[0].Effect.Material.Distortion.bEnabled = false;
		const int32 WhiteLoadsBefore = Fake.NumTextureLoads;
		FMeshRenderer WhiteRenderer = NWorld::MakeMeshRenderer( WhiteMesh, [&Assets]( const FString& Path ) { return Assets.LoadTexture( Path ); } );
		const bool bWhiteRetained = WhiteRenderer.Batches.IsValidIndex( 0 ) && WhiteRenderer.Batches[0].EffectResources.IsValid() && WhiteRenderer.Batches[0].EffectResources->Texture.IsValid() && WhiteRenderer.Batches[0].EffectResources->TextureHandle >= 0;
		C.Check( bWhiteRetained && Fake.NumTextureLoads == WhiteLoadsBefore + 1, "画像なしslotは白画像だけを一度読みready ownerとして保持する" );
		WhiteRenderer = FMeshRenderer();
		CollectMeshMaterialResources( Assets );

		// 空・全zeroのstrengthではnoise画像無しを0強度として保持する。
		FEditMesh EmptyStrengthMesh = MakeResourceMesh( FString(), FString(), FString() );
		EmptyStrengthMesh.Materials[0].Effect.Material.bDissolve = false;
		EmptyStrengthMesh.Materials[0].Effect.Material.Distortion.Strength.Keys.Reset();
		const int32 EmptyLoadsBefore = Fake.NumTextureLoads;
		FMeshRenderer EmptyStrengthRenderer = NWorld::MakeMeshRenderer( EmptyStrengthMesh, [&Assets]( const FString& Path ) { return Assets.LoadTexture( Path ); } );
		FDrawBuffer EmptyStrengthBuffer;
		FDrawContext EmptyStrengthDraw( EmptyStrengthBuffer );
		EmptyStrengthRenderer.Record( EmptyStrengthDraw, FTransform::Identity );
		bool bEmptyHasZeroDistortion = false;
		if ( !EmptyStrengthBuffer.Words.empty() ) ForEachDrawCommand( FDrawSpan{ EmptyStrengthBuffer.Words.data(), static_cast<int32>( EmptyStrengthBuffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::MaterialMesh3D && Command.NumDataWords >= static_cast<int32>( sizeof( FDrawMaterialMeshData ) / 4 ) ) { const FDrawMaterialMeshData Header = Command.Read<FDrawMaterialMeshData>(); bEmptyHasZeroDistortion = Header.DistortionStrength == 0.0f && Header.DistortionTexture == -1; } } );
		C.Check( EmptyStrengthRenderer.Batches.IsValidIndex( 0 ) && EmptyStrengthRenderer.Batches[0].EffectResources.IsValid() && bEmptyHasZeroDistortion && Fake.NumTextureLoads == EmptyLoadsBefore + 1, "空Strengthでは白sourceだけを読み、Noise無しの零強度命令を記録する" );
		EmptyStrengthRenderer = FMeshRenderer();
		CollectMeshMaterialResources( Assets );

		FEditMesh ZeroStrengthMesh = EmptyStrengthMesh;
		ZeroStrengthMesh.Materials[0].Effect.Material.Distortion.Strength.Keys.Add( { 0.0f, 0.0f } );
		FMeshRenderer ZeroStrengthRenderer = NWorld::MakeMeshRenderer( ZeroStrengthMesh, [&Assets]( const FString& Path ) { return Assets.LoadTexture( Path ); } );
		C.Check( ZeroStrengthRenderer.Batches.IsValidIndex( 0 ) && ZeroStrengthRenderer.Batches[0].EffectResources.IsValid(), "全zeroStrengthでもNoise画像を要求しない" );
		ZeroStrengthRenderer = FMeshRenderer();
		CollectMeshMaterialResources( Assets );

		FEditMesh MissingNoiseMesh = MakeResourceMesh( FString(), FString(), FString() );
		MissingNoiseMesh.Materials[0].Effect.Material.bDissolve = false;
		FMeshRenderer MissingNoise = NWorld::MakeMeshRenderer( MissingNoiseMesh, [&Assets]( const FString& Path ) { return Assets.LoadTexture( Path ); } );
		C.Check( MissingNoise.HasEffectPreparationFailures(), "正Strengthで空のNoise pathは部分資源を作らずslot準備を失敗する" );
		MissingNoise = FMeshRenderer();
		CollectMeshMaterialResources( Assets );
	}

	// 明示maskの読み込みが失敗したslotは部分資源を使わず描画を飛ばす。
	const int32 PreviousFailure = Fake.FailTextureLoadCall;
	Fake.FailTextureLoadCall = Fake.NumTextureLoads + 2;
	FMeshRenderer FailedMask = NWorld::MakeMeshRenderer( MakeResourceMesh( SourcePath, MaskPath, NoisePath ), [&Assets]( const FString& Path ) { return Assets.LoadTexture( Path ); } );
	Fake.FailTextureLoadCall = PreviousFailure;
	C.Check( FailedMask.IsValid() && FailedMask.HasEffectPreparationFailures(), "mask読み込み失敗をeffect準備失敗として保持する" );
	const int32 FailedLoadsBeforeRecord = Fake.NumTextureLoads;
	FDrawBuffer FailedMaskBuffer;
	FDrawContext FailedMaskDraw( FailedMaskBuffer );
	FailedMask.Record( FailedMaskDraw, FTransform::Identity );
	int32 FailedMaterialCommands = 0;
	int32 FailedLegacyCommands = 0;
	if ( !FailedMaskBuffer.Words.empty() ) ForEachDrawCommand( FDrawSpan{ FailedMaskBuffer.Words.data(), static_cast<int32>( FailedMaskBuffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::MaterialMesh3D ) ++FailedMaterialCommands; if ( Command.Type == EDrawCommand::Mesh3D ) ++FailedLegacyCommands; } );
	C.Check( FailedMaterialCommands == 0 && FailedLegacyCommands == 0 && Fake.NumTextureLoads == FailedLoadsBeforeRecord, "準備に失敗した有効slotはopaqueへ代替せずRecordも再読しない" );
	FailedMask = FMeshRenderer();
	CollectMeshMaterialResources( Assets );

	// 必須画像を一つずつ失敗させ、失敗slotがどの役割でも部分描画しない。
	for ( int32 FailedRole = 0; FailedRole < 3; ++FailedRole )
	{
		// 役割ごとにcacheを共有しない読み込みpath。
		const FString CasePrefix = FString::Printf( "mesh-material-resource/failed-%d", FailedRole );
		const FString CaseSource = CasePrefix + "-source.tga";
		const FString CaseMask = CasePrefix + "-mask.tga";
		const FString CaseNoise = CasePrefix + "-noise.tga";
		const bool bCaseSaved = NAssetTest::WriteFileBytes( Dir / CaseSource, MakeResourceTga() ) && NAssetTest::WriteFileBytes( Dir / CaseMask, MakeResourceTga() ) && NAssetTest::WriteFileBytes( Dir / CaseNoise, MakeResourceTga() );
		const int32 PreviousFailureCall = Fake.FailTextureLoadCall;
		Fake.FailTextureLoadCall = Fake.NumTextureLoads + FailedRole + 1;
		FMeshRenderer FailedRenderer = NWorld::MakeMeshRenderer( MakeResourceMesh( CaseSource, CaseMask, CaseNoise ), [&Assets]( const FString& Path ) { return Assets.LoadTexture( Path ); } );
		Fake.FailTextureLoadCall = PreviousFailureCall;
		FDrawBuffer FailedBuffer;
		FDrawContext FailedContext( FailedBuffer );
		FailedRenderer.Record( FailedContext, FTransform::Identity );
		C.Check( bCaseSaved && FailedRenderer.IsValid() && FailedRenderer.HasEffectPreparationFailures() && FailedBuffer.Words.empty(), "source・mask・noise各必須画像の読み込み失敗でeffect slot全体を描かない" );
		FailedRenderer = FMeshRenderer();
		CollectMeshMaterialResources( Assets );
		C.Check( Fake.Textures == InitialTextures && HasSameMeshBuffers( Fake, InitialVertexBuffers, InitialIndexBuffers ), "画像失敗slotごとに既存fake handle集合へ戻る" );
	}

	// pending sourceは準備時点で拒否し、後からreadyになってもrendererへ採用しない。
	const FString PendingPath = "mesh-material-resource/pending.tga";
	const bool bPendingSaved = NAssetTest::WriteFileBytes( Dir / PendingPath, MakeResourceTga() );
	TSharedPtr<CTexture> Pending = Assets.LoadTexture( PendingPath, true );
	const int32 PendingHandle = Pending.IsValid() ? Pending->GetHandle() : -1;
	TWeakPtr<CTexture> PendingWeak = Pending;
	FEditMesh PendingMesh = MakeResourceMesh( PendingPath, FString(), FString() );
	PendingMesh.Materials[0].Effect.Material.Distortion.bEnabled = false;
	FMeshRenderer PendingRenderer = NWorld::MakeMeshRenderer( PendingMesh, [&Assets]( const FString& Path ) { return Assets.LoadTexture( Path ); } );
	C.Check( bPendingSaved && Pending.IsValid() && !Pending->IsReady() && PendingRenderer.HasEffectPreparationFailures(), "pending sourceをMake時に拒否する" );
	Fake.Loading.erase( PendingHandle );
	const int32 PendingLoadsBeforeRecord = Fake.NumTextureLoads;
	FDrawBuffer PendingBuffer;
	FDrawContext PendingDraw( PendingBuffer );
	PendingRenderer.Record( PendingDraw, FTransform::Identity );
	C.Check( PendingBuffer.Words.empty() && Fake.NumTextureLoads == PendingLoadsBeforeRecord, "後からreadyになったsourceも既存rendererへ取り込まず描画を見送る" );
	PendingRenderer = FMeshRenderer();
	Pending.Reset();
	CollectMeshMaterialResources( Assets );
	C.Check( !PendingWeak.Pin().IsValid(), "pending拒否後にownerが無くなればsource imageを解放する" );

	// pending画像をmask位置へ置き、sourceだけがreadyでもeffect commandを作らない。
	const FString PendingMaskPath = "mesh-material-resource/pending-mask.tga";
	const bool bPendingMaskSaved = NAssetTest::WriteFileBytes( Dir / PendingMaskPath, MakeResourceTga() );
	TSharedPtr<CTexture> PendingMask = Assets.LoadTexture( PendingMaskPath, true );
	const int32 PendingMaskHandle = PendingMask.IsValid() ? PendingMask->GetHandle() : -1;
	FMeshRenderer PendingMaskRenderer = NWorld::MakeMeshRenderer( MakeResourceMesh( SourcePath, PendingMaskPath, NoisePath ), [&Assets]( const FString& Path ) { return Assets.LoadTexture( Path ); } );
	C.Check( bPendingMaskSaved && PendingMask.IsValid() && !PendingMask->IsReady() && PendingMaskRenderer.HasEffectPreparationFailures(), "pending maskだけが未readyでもeffect slot準備を失敗する" );
	Fake.Loading.erase( PendingMaskHandle );
	FDrawBuffer PendingMaskBuffer;
	FDrawContext PendingMaskDraw( PendingMaskBuffer );
	PendingMaskRenderer.Record( PendingMaskDraw, FTransform::Identity );
	C.Check( PendingMaskBuffer.Words.empty(), "後からreadyになったmaskも既存rendererへ採用しない" );
	PendingMaskRenderer = FMeshRenderer();
	PendingMask.Reset();
	CollectMeshMaterialResources( Assets );

	// pending画像を正Strengthが要求するnoise位置へ置いて同じ失敗を確認する。
	const FString PendingNoisePath = "mesh-material-resource/pending-noise.tga";
	const bool bPendingNoiseSaved = NAssetTest::WriteFileBytes( Dir / PendingNoisePath, MakeResourceTga() );
	TSharedPtr<CTexture> PendingNoise = Assets.LoadTexture( PendingNoisePath, true );
	const int32 PendingNoiseHandle = PendingNoise.IsValid() ? PendingNoise->GetHandle() : -1;
	FMeshRenderer PendingNoiseRenderer = NWorld::MakeMeshRenderer( MakeResourceMesh( SourcePath, MaskPath, PendingNoisePath ), [&Assets]( const FString& Path ) { return Assets.LoadTexture( Path ); } );
	C.Check( bPendingNoiseSaved && PendingNoise.IsValid() && !PendingNoise->IsReady() && PendingNoiseRenderer.HasEffectPreparationFailures(), "pending noiseだけが未readyでもeffect slot準備を失敗する" );
	Fake.Loading.erase( PendingNoiseHandle );
	FDrawBuffer PendingNoiseBuffer;
	FDrawContext PendingNoiseDraw( PendingNoiseBuffer );
	PendingNoiseRenderer.Record( PendingNoiseDraw, FTransform::Identity );
	C.Check( PendingNoiseBuffer.Words.empty(), "後からreadyになったnoiseも既存rendererへ採用しない" );
	PendingNoiseRenderer = FMeshRenderer();
	PendingNoise.Reset();
	CollectMeshMaterialResources( Assets );
	C.Check( Fake.Textures == InitialTextures && HasSameMeshBuffers( Fake, InitialVertexBuffers, InitialIndexBuffers ), "全resource失敗・停止経路の終了後にhandleを残さない" );
	Assets.SetRootDirectory( PreviousRoot );
}
