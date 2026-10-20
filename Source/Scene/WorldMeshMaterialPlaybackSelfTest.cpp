// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"
#include "WorldMeshMaterialSelfTestSupport.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "Library/Core/Interfaces/Render/DrawMaterialMesh.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Render/MeshRender.h"
#include "Library/Framework/Render/MeshMaterialEffectResources.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Framework/World/Mesh/EditMeshPrimitives.h"
#include "Library/Framework/World/Mesh/EditMeshOps.h"
#include "Library/Framework/World/WorldRender.h"
#include "Library/Framework/World/WorldSpawner.h"
#include "Library/Framework/Asset/WorldAsset.h"
#include "Library/Platform/FileSystem.h"

namespace
{
	// 組み込み白画像として読み込める2x2のTGA bytesを作る。
	std::vector<uint8> MakeMeshEffectWhiteTga()
	{
		// 32bit上から下向きTGAのheaderと不透明な白pixel列。
		std::vector<uint8> Bytes = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 32, 0x28 };
		for ( int32 Index = 0; Index < 4; ++Index ) { Bytes.push_back( 255 ); Bytes.push_back( 255 ); Bytes.push_back( 255 ); Bytes.push_back( 255 ); }
		return Bytes;
	}

	// command列から指定種別の個数を数える。
	int32 CountMeshEffectCommands( const FDrawBuffer& Buffer, EDrawCommand Type )
	{
		// 一致した命令種別の累計。
		int32 Count = 0;
		// command列の各headerを確認する処理。
		const auto CountCommand = [&]( const FDrawCommandView& Command ) { if ( Command.Type == Type ) ++Count; };
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, CountCommand );
		return Count;
	}

	// rendererの現在状態を一度だけcommand列へ記録する。
	FDrawBuffer RecordMeshEffectRenderer( const FMeshRenderer& Renderer, const FTransform& World, EDrawBlend InitialBlend, int32 BlendParam )
	{
		// この記録が所有するdraw words。
		FDrawBuffer Buffer;
		// 初期合成とmesh命令を記録するcontext。
		FDrawContext Draw( Buffer );
		Draw.Blend( InitialBlend, BlendParam );
		Renderer.Record( Draw, World );
		return Buffer;
	}

	// 指定loop条件とDurationを持つ、保存・設置確認用の効果付き平面を作る。
	FEditMesh MakePlaybackEffectMesh( bool bLoop, float32 Duration )
	{
		// 一つの材質へ時間変化設定を付けた編集メッシュ。
		FEditMesh Mesh = NEditMesh::MakePlane( 2.0f, 2.0f );
		FEditMeshMaterial& Material = Mesh.Materials[Mesh.Materials.Add( FEditMeshMaterial() )];
		Material.Effect.Material.bEnabled = true;
		Material.Effect.Material.UVScroll = FVector2( 2.0f, 0.0f );
		Material.Effect.Duration = Duration;
		Material.Effect.bLoop = bLoop;
		return Mesh;
	}

	// 記録した最初の材質mesh命令を値として読む。
	bool ReadMeshEffectHeader( const FDrawBuffer& Buffer, FDrawMaterialMeshData& OutHeader )
	{
		// 有効な材質mesh命令が見つかったか。
		bool bFound = false;
		const auto ReadHeader = [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::MaterialMesh3D && Command.NumDataWords >= static_cast<int32>( sizeof( FDrawMaterialMeshData ) / 4 ) && !bFound ) { OutHeader = Command.Read<FDrawMaterialMeshData>(); bFound = true; } };
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, ReadHeader );
		return bFound;
	}
}

// 個体別clock、状態遷移、material commandと通常meshへの復帰を実際のrendererで確認する。
void NWorldTest::RunMeshMaterialPlaybackTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir )
{
	C.Group = "mesh-material-effect-playback";
	// 呼び出し元が使っていたAssetManager rootを復元する値。
	const FString PreviousRoot = Assets.GetRootDirectory();
	Assets.SetRootDirectory( Dir );
	// no-image slot用白TGAのrelative path。
	const FString WhitePath = "Effects/material_white.tga";
	const bool bSavedWhite = NFileSystem::EnsureDirectory( Dir / "Effects" ) && NAssetTest::WriteFileBytes( Dir / WhitePath, MakeMeshEffectWhiteTga() );
	C.Check( bSavedWhite, "組み込み白画像をmesh effect用sandboxへ保存する" );
	if ( !bSavedWhite ) { Assets.SetRootDirectory( PreviousRoot ); return; }
	// 二分割した平面へ一つのeffect slotを付けるfixture。
	FEditMesh Mesh = NEditMesh::MakePlane( 2.0f, 2.0f, 2, 2 );
	FEditMeshMaterial& Material = Mesh.Materials[Mesh.Materials.Add( FEditMeshMaterial() )];
	Material.Effect.Material.bEnabled = true;
	Material.Effect.Material.UVScroll = FVector2( 2.0f, 0.0f );
	Material.Effect.Material.bDissolve = true;
	Material.Effect.Material.Dissolve.Interpolation = FEffectScalarCurve::EInterpolation::Linear;
	Material.Effect.Material.Dissolve.Keys.Add( { 0.0f, 0.0f } );
	Material.Effect.Material.Dissolve.Keys.Add( { 1.0f, 1.0f } );
	Material.Effect.Material.Emission.Interpolation = FEffectScalarCurve::EInterpolation::Linear;
	Material.Effect.Material.Emission.Keys.Add( { 0.0f, 1.0f } );
	Material.Effect.Material.Emission.Keys.Add( { 1.0f, 3.0f } );
	Material.Effect.Duration = 0.5f;
	Material.Effect.bLoop = false;
	Material.Effect.Blend = EDrawBlend::Add;
	// mesh生成時に同じsource texture handleを返す同期loader。
	const auto LoadTexture = [&Assets]( const FString& Path ) { return Assets.LoadTexture( Path ); };
	FMeshRenderer Renderer = NWorld::MakeMeshRenderer( Mesh, LoadTexture );
	C.Check( Renderer.IsValid() && !Renderer.HasEffectPreparationFailures(), "有効な材質効果を実画像と固定mesh chunkへ準備する" );
	if ( !Renderer.IsValid() || Renderer.HasEffectPreparationFailures() ) { Assets.SetRootDirectory( PreviousRoot ); return; }
	C.Check( Renderer.IsEffectsPlaying() && !Renderer.IsEffectsPaused() && !Renderer.IsEffectsFinished() && std::abs( Renderer.GetEffectTime() ) < 1.0e-9, "生成直後のeffect slotは時刻0から再生中になる" );
	// rendererの値複製が共有形状だけを共有し時計を独立させる。
	FMeshRenderer Copy = Renderer;
	const TSharedPtr<const FMeshMaterialEffectResources> FrozenResources = Renderer.Batches[0].EffectResources;
	Mesh.Materials[0].Effect.Duration = 9.0f;
	Copy.Update( 0.125f );
	C.Check( std::abs( Copy.GetEffectTime() - 0.125 ) < 1.0e-6 && std::abs( Renderer.GetEffectTime() ) < 1.0e-9 && Copy.Batches[0].EffectResources == FrozenResources && FrozenResources.IsValid() && FrozenResources->Definition.Duration == 0.5f, "renderer copyは時計を独立させ、生成時定義を共有したままmesh編集値から固定する" );
	// Add 123の呼出状態をeffect slot前後で保つbuffer。
	const FDrawBuffer AddBuffer = RecordMeshEffectRenderer( Renderer, FTransform( FVector3( 3.0f, 0.0f, 0.0f ) ), EDrawBlend::Add, 123 );
	// 時刻0のmaterial command headerを所有値へ読み取る。
	FDrawMaterialMeshData FirstHeader;
	bool bFoundMaterialHeader = false;
	const auto ReadFirstMaterial = [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::MaterialMesh3D && Command.NumDataWords >= static_cast<int32>( sizeof( FDrawMaterialMeshData ) / 4 ) ) { FirstHeader = Command.Read<FDrawMaterialMeshData>(); bFoundMaterialHeader = true; } };
	ForEachDrawCommand( FDrawSpan{ AddBuffer.Words.data(), static_cast<int32>( AddBuffer.Words.size() ) }, ReadFirstMaterial );
	C.Check( CountMeshEffectCommands( AddBuffer, EDrawCommand::MaterialMesh3D ) == 1 && bFoundMaterialHeader && FirstHeader.Dissolve == 0.0f && FirstHeader.Emission == 1.0f && FirstHeader.Texture >= 0, "時刻0に検証済みsource textureと独立したcurve既定値を含む非空材質meshを記録する" );
	C.Check( CountMeshEffectCommands( AddBuffer, EDrawCommand::Mesh3D ) == 0, "再生中slotは旧Mesh3Dへ同時に流さない" );
	// Add blend設定を更新し、その値がslot後もcontext内に残る。
	FDrawBuffer BlendBuffer;
	FDrawContext BlendDraw( BlendBuffer );
	BlendDraw.Blend( EDrawBlend::Add, 123 );
	Renderer.Record( BlendDraw, FTransform::Identity );
	C.Check( BlendDraw.GetBlendMode() == EDrawBlend::Add && BlendDraw.GetBlendParam() == 123, "effect slot後も元contextのAdd 123を復帰する" );
	// Pauseがclockを止め、Playが保持位置から進める。
	Copy.PauseEffects();
	Copy.Update( 0.2f );
	C.Check( Copy.IsEffectsPaused() && std::abs( Copy.GetEffectTime() - 0.125 ) < 1.0e-6, "pause中は曲線clockを保持する" );
	Copy.PlayEffects();
	Copy.Update( 0.125f );
	C.Check( Copy.IsEffectsPlaying() && std::abs( Copy.GetEffectTime() - 0.25 ) < 1.0e-6, "pauseから再生すると保持位置から進む" );
	// 非loop終端は最後の材質表示を保ち、停止だけが従来Mesh3Dへ戻す。
	Copy.Update( 1.0f );
	const FDrawBuffer FinishedBuffer = RecordMeshEffectRenderer( Copy, FTransform::Identity, EDrawBlend::Opaque, 255 );
	FDrawMaterialMeshData FinishedHeader;
	const bool bFinishedHeaderFound = ReadMeshEffectHeader( FinishedBuffer, FinishedHeader );
	Copy.Update( 0.25f );
	FDrawMaterialMeshData FrozenHeader;
	const bool bFrozenHeaderFound = ReadMeshEffectHeader( RecordMeshEffectRenderer( Copy, FTransform::Identity, EDrawBlend::Opaque, 255 ), FrozenHeader );
	C.Check( Copy.IsEffectsFinished() && !Copy.IsEffectsPlaying() && CountMeshEffectCommands( FinishedBuffer, EDrawCommand::MaterialMesh3D ) == 1 && std::abs( Copy.GetEffectTime() - 0.5 ) < 1.0e-6 && bFinishedHeaderFound && bFrozenHeaderFound && FinishedHeader.UVOffset[0] == FrozenHeader.UVOffset[0], "非loop終端は最終材質表示・UV位相・終端時計を保つ" );
	Copy.PauseEffects();
	C.Check( Copy.IsEffectsFinished(), "FinishedへのPauseは終端表示を維持する" );
	Copy.PlayEffects();
	C.Check( Copy.IsEffectsPlaying() && std::abs( Copy.GetEffectTime() ) < 1.0e-9, "FinishedからPlayすると時刻0で再開する" );
	Copy.Update( 0.1f );
	Copy.StopEffects();
	const FDrawBuffer StoppedBuffer = RecordMeshEffectRenderer( Copy, FTransform::Identity, EDrawBlend::Alpha, 77 );
	C.Check( !Copy.IsEffectsPlaying() && !Copy.IsEffectsPaused() && !Copy.IsEffectsFinished() && std::abs( Copy.GetEffectTime() ) < 1.0e-9 && CountMeshEffectCommands( StoppedBuffer, EDrawCommand::MaterialMesh3D ) == 0 && CountMeshEffectCommands( StoppedBuffer, EDrawCommand::Mesh3D ) == 1, "Stopは時刻を戻して従来Mesh3Dへ切り替える" );
	Copy.RestartEffects();
	C.Check( Copy.IsEffectsPlaying() && std::abs( Copy.GetEffectTime() ) < 1.0e-9, "Restartは停止状態から時刻0で効果を始める" );
	// World座標が一度だけ変換されることをcommandの頂点と法線から確認する。
	bool bFoundTranslatedPosition = false;
	const auto ReadTranslatedVertex = [&]( const FDrawCommandView& Command )
	{
		if ( Command.Type != EDrawCommand::MaterialMesh3D || Command.NumDataWords < static_cast<int32>( sizeof( FDrawMaterialMeshData ) / 4 + sizeof( FMeshVertex ) / 4 ) ) return;
		const FDrawMaterialMeshData Header = Command.Read<FDrawMaterialMeshData>();
		FMeshVertex Vertex;
		std::memcpy( &Vertex, Command.Data + sizeof( FDrawMaterialMeshData ) / 4, sizeof( Vertex ) );
		bFoundTranslatedPosition = Header.NumVertices >= 3 && std::abs( Vertex.Position[0] - 2.0f ) < 1.0e-5f && std::abs( Vertex.Position[1] ) < 1.0e-5f && std::abs( Vertex.Position[2] + 1.0f ) < 1.0e-5f && std::abs( Vertex.Normal[1] - 1.0f ) < 1.0e-5f;
	};
	ForEachDrawCommand( FDrawSpan{ AddBuffer.Words.data(), static_cast<int32>( AddBuffer.Words.size() ) }, ReadTranslatedVertex );
	C.Check( bFoundTranslatedPosition, "MaterialMesh頂点へworld位置を一度だけ適用し法線を保つ" );
	const FTransform MirroredWorld( FVector3( 1.0f, 0.0f, 0.0f ), FQuaternion::Identity, FVector3( -2.0f, 4.0f, 0.0f ) );
	const FDrawBuffer MirroredBuffer = RecordMeshEffectRenderer( Renderer, MirroredWorld, EDrawBlend::Opaque, 255 );
	bool bFoundMirroredVertex = false;
	const auto ReadMirroredVertex = [&]( const FDrawCommandView& Command )
	{
		if ( Command.Type != EDrawCommand::MaterialMesh3D || Command.NumDataWords < static_cast<int32>( sizeof( FDrawMaterialMeshData ) / 4 + sizeof( FMeshVertex ) / 4 ) ) return;
		FMeshVertex Vertex;
		std::memcpy( &Vertex, Command.Data + sizeof( FDrawMaterialMeshData ) / 4, sizeof( Vertex ) );
		bFoundMirroredVertex = std::abs( Vertex.Position[0] - 3.0f ) < 1.0e-5f && std::abs( Vertex.Position[1] ) < 1.0e-5f && std::abs( Vertex.Position[2] ) < 1.0e-5f && std::abs( Vertex.Normal[0] ) < 1.0e-5f && std::abs( Vertex.Normal[1] - 1.0f ) < 1.0e-5f && std::abs( Vertex.Normal[2] ) < 1.0e-5f;
	};
	ForEachDrawCommand( FDrawSpan{ MirroredBuffer.Words.data(), static_cast<int32>( MirroredBuffer.Words.size() ) }, ReadMirroredVertex );
	C.Check( bFoundMirroredVertex, "負scaleとzero scaleのworld位置・inverse scale法線を期待値で確認する" );

	// 無効・停止中の同一meshが、従来経路と同じMesh3D command wordsを記録する。
	FMeshRenderer StoppableRenderer = NWorld::MakeMeshRenderer( MakePlaybackEffectMesh( false, 0.5f ), LoadTexture );
	StoppableRenderer.StopEffects();
	FMeshRenderer DisabledPathRenderer = StoppableRenderer;
	DisabledPathRenderer.Batches[0].bEffectRequested = false;
	DisabledPathRenderer.Batches[0].EffectResources.Reset();
	const FTransform CompareWorld( FVector3( 2.0f, 3.0f, 4.0f ) );
	const FDrawBuffer LegacyBuffer = RecordMeshEffectRenderer( DisabledPathRenderer, CompareWorld, EDrawBlend::Alpha, 91 );
	const FDrawBuffer StoppedLegacyBuffer = RecordMeshEffectRenderer( StoppableRenderer, CompareWorld, EDrawBlend::Alpha, 91 );
	C.Check( !LegacyBuffer.Words.empty() && LegacyBuffer.Words == StoppedLegacyBuffer.Words, "disabledとstoppedの材質slotは同じworld・blendで旧Mesh3D commandをbyte一致させる" );

	// loop境界で時計とUV位相を折り返し、巨大値と不正deltaも別々に確認する。
	FMeshRenderer LoopRenderer = NWorld::MakeMeshRenderer( MakePlaybackEffectMesh( true, 0.5f ), LoadTexture );
	LoopRenderer.Update( 0.25f );
	FDrawMaterialMeshData LoopHeader;
	const bool bHalfPhaseFound = ReadMeshEffectHeader( RecordMeshEffectRenderer( LoopRenderer, FTransform::Identity, EDrawBlend::Opaque, 255 ), LoopHeader );
	C.Check( bHalfPhaseFound && std::abs( LoopHeader.UVOffset[0] - 0.5f ) < 1.0e-6f && std::abs( LoopRenderer.GetEffectTime() - 0.25 ) < 1.0e-6, "loop途中のUVとcurve時計が同じUpdate量だけ進む" );
	LoopRenderer.Update( 0.25f );
	const bool bBoundaryFound = ReadMeshEffectHeader( RecordMeshEffectRenderer( LoopRenderer, FTransform::Identity, EDrawBlend::Opaque, 255 ), LoopHeader );
	C.Check( bBoundaryFound && std::abs( LoopHeader.UVOffset[0] ) < 1.0e-6f && std::abs( LoopRenderer.GetEffectTime() ) < 1.0e-6, "Duration境界ではcurve時計だけが折り返しUVは周期継続する" );
	const float32 LargeDelta = 1.0e20f;
	LoopRenderer.Update( LargeDelta );
	const float32 ExpectedLargeUv = static_cast<float32>( std::fmod( static_cast<float64>( LargeDelta ) * 2.0, 1.0 ) );
	const bool bLargeDeltaFound = ReadMeshEffectHeader( RecordMeshEffectRenderer( LoopRenderer, FTransform::Identity, EDrawBlend::Opaque, 255 ), LoopHeader );
	C.Check( bLargeDeltaFound && std::abs( LoopHeader.UVOffset[0] - ExpectedLargeUv ) < 1.0e-6f && LoopRenderer.GetEffectTime() >= 0.0 && LoopRenderer.GetEffectTime() < 0.5, "巨大有限deltaでもloop時計とUV位相を有界に保つ" );
	const float64 TimeBeforeInvalidDeltas = LoopRenderer.GetEffectTime();
	const float32 UvBeforeInvalidDeltas = LoopHeader.UVOffset[0];
	LoopRenderer.Update( -1.0f );
	LoopRenderer.Update( std::numeric_limits<float32>::quiet_NaN() );
	LoopRenderer.Update( std::numeric_limits<float32>::infinity() );
	const bool bInvalidDeltaFound = ReadMeshEffectHeader( RecordMeshEffectRenderer( LoopRenderer, FTransform::Identity, EDrawBlend::Opaque, 255 ), LoopHeader );
	C.Check( bInvalidDeltaFound && LoopRenderer.GetEffectTime() == TimeBeforeInvalidDeltas && LoopHeader.UVOffset[0] == UvBeforeInvalidDeltas, "負数・NaN・無限deltaは時計もUVも変えない" );

	// 同じrenderer内のslotはそれぞれ別Durationの時計と終端状態を持つ。
	FEditMesh MultiSlotMesh = NEditMesh::MakePlane( 2.0f, 2.0f, 2, 1 );
	MultiSlotMesh.Materials.Add( FEditMeshMaterial() );
	FEditMeshMaterial SecondSlotMaterial = MultiSlotMesh.Materials[0];
	SecondSlotMaterial.Effect.Material.bEnabled = true;
	SecondSlotMaterial.Effect.Duration = 0.75f;
	SecondSlotMaterial.Effect.bLoop = false;
	const int32 SecondSlot = NEditMesh::AddMaterial( MultiSlotMesh, SecondSlotMaterial );
	MultiSlotMesh.Faces[1].Material = SecondSlot;
	MultiSlotMesh.Materials[0].Effect.Material.bEnabled = true;
	MultiSlotMesh.Materials[0].Effect.Duration = 0.25f;
	MultiSlotMesh.Materials[0].Effect.bLoop = false;
	FMeshRenderer MultiSlotRenderer = NWorld::MakeMeshRenderer( MultiSlotMesh, LoadTexture );
	MultiSlotRenderer.Update( 0.5f );
	const FDrawBuffer MultiSlotBuffer = RecordMeshEffectRenderer( MultiSlotRenderer, FTransform::Identity, EDrawBlend::Opaque, 255 );
	C.Check( MultiSlotRenderer.Batches.Num() == 2 && MultiSlotRenderer.Batches[0].EffectResources.IsValid() && MultiSlotRenderer.Batches[1].EffectResources.IsValid() && MultiSlotRenderer.GetEffectTime( 0 ) == 0.25 && std::abs( MultiSlotRenderer.GetEffectTime( 1 ) - 0.5 ) < 1.0e-6 && MultiSlotRenderer.IsEffectsPlaying() && CountMeshEffectCommands( MultiSlotBuffer, EDrawCommand::MaterialMesh3D ) == 2, "複数slotは異なるDurationで独立して進み終端slotも描画する" );

	// 1024頂点境界をまたぐ一枚面を三角形の順とchunk-local indexを保って記録する。
	FEditMesh ChunkMesh;
	FEditMeshMaterial& ChunkMaterial = ChunkMesh.Materials[ChunkMesh.Materials.Add( FEditMeshMaterial() )];
	ChunkMaterial.Effect.Material.bEnabled = true;
	FEditMeshFace LargePolygon;
	for ( int32 VertexIndex = 0; VertexIndex < 1026; ++VertexIndex )
	{
		const float32 Angle = 6.283185307179586f * static_cast<float32>( VertexIndex ) / 1026.0f;
		LargePolygon.Indices.Add( ChunkMesh.Vertices.Add( FVector3( std::cos( Angle ), 0.0f, std::sin( Angle ) ) ) );
	}
	ChunkMesh.Faces.Add( LargePolygon );
	FMeshRenderer ChunkRenderer = NWorld::MakeMeshRenderer( ChunkMesh, LoadTexture );
	const FDrawBuffer ChunkBuffer = RecordMeshEffectRenderer( ChunkRenderer, FTransform::Identity, EDrawBlend::Opaque, 255 );
	int32 RecordedChunkCount = 0;
	bool bFirstChunkShape = false;
	bool bLastChunkShape = false;
	const auto CheckChunks = [&]( const FDrawCommandView& Command )
	{
		if ( Command.Type != EDrawCommand::MaterialMesh3D ) return;
		const FDrawMaterialMeshData Header = Command.Read<FDrawMaterialMeshData>();
		const uint32* Indices = reinterpret_cast<const uint32*>( Command.Data + sizeof( FDrawMaterialMeshData ) / 4 + static_cast<size_t>( Header.NumVertices ) * sizeof( FMeshVertex ) / 4 );
		if ( RecordedChunkCount == 0 ) bFirstChunkShape = Header.NumVertices == 1024 && Header.NumIndices == 3066 && Indices[0] == 0 && Indices[1] == 1 && Indices[2] == 2 && Indices[3] == 0 && Indices[4] == 2 && Indices[5] == 3;
		if ( RecordedChunkCount == 1 ) bLastChunkShape = Header.NumVertices == 4 && Header.NumIndices == 6 && Indices[0] == 0 && Indices[1] == 1 && Indices[2] == 2 && Indices[3] == 0 && Indices[4] == 2 && Indices[5] == 3;
		++RecordedChunkCount;
	};
	ForEachDrawCommand( FDrawSpan{ ChunkBuffer.Words.data(), static_cast<int32>( ChunkBuffer.Words.size() ) }, CheckChunks );
	C.Check( ChunkRenderer.IsValid() && !ChunkRenderer.HasEffectPreparationFailures() && RecordedChunkCount == 2 && bFirstChunkShape && bLastChunkShape, "chunk分割は1024頂点上限・三角形順・共有頂点とlocal remapを守る" );

	// 後半chunkだけworld座標が有限でなくなる場合はeffect slot全体を止め、後続legacy slotを記録する。
	FEditMesh OverflowMesh = ChunkMesh;
	for ( int32 VertexIndex = 0; VertexIndex < 1024; ++VertexIndex )
	{
		const float32 Angle = 6.283185307179586f * static_cast<float32>( VertexIndex ) / 1024.0f;
		OverflowMesh.Vertices[VertexIndex] = FVector3( -0.4f + 0.1f * std::cos( Angle ), 0.0f, 0.1f * std::sin( Angle ) );
	}
	OverflowMesh.Vertices[1024] = FVector3( 2.0f, 0.0f, 0.0f );
	OverflowMesh.Vertices[1025] = FVector3( 2.0f, 0.0f, 0.1f );
	FEditMeshFace LegacyFace;
	LegacyFace.Indices = { 1026, 1027, 1028 };
	const int32 LegacyMaterialIndex = NEditMesh::AddMaterial( OverflowMesh, FEditMeshMaterial() );
	LegacyFace.Material = LegacyMaterialIndex;
	OverflowMesh.Vertices.Add( FVector3( 0.0f, 0.0f, 0.0f ) );
	OverflowMesh.Vertices.Add( FVector3( 1.0f, 0.0f, 0.0f ) );
	OverflowMesh.Vertices.Add( FVector3( 0.0f, 0.0f, 1.0f ) );
	OverflowMesh.Faces.Add( LegacyFace );
	FMeshRenderer OverflowRenderer = NWorld::MakeMeshRenderer( OverflowMesh, LoadTexture );
	const FTransform OverflowWorld( FVector3( 1.0e38f, 0.0f, 0.0f ), FQuaternion::Identity, FVector3( 2.0e38f, 1.0f, 1.0f ) );
	const FDrawBuffer OverflowBuffer = RecordMeshEffectRenderer( OverflowRenderer, OverflowWorld, EDrawBlend::Opaque, 255 );
	C.Check( CountMeshEffectCommands( OverflowBuffer, EDrawCommand::MaterialMesh3D ) == 0 && CountMeshEffectCommands( OverflowBuffer, EDrawCommand::Mesh3D ) == 1, "後半chunkのworld overflowではslotを一部描画せず隣のlegacy batchを記録する" );

	// 新規保存、読み直し、通常spawner、ECS PreUpdateの経路で個体時計を進める。
	FWorldDefinition SavedWorld;
	SavedWorld.Name = "mesh material playback";
	SavedWorld.NextId = 2;
	FWorldObject SavedObject;
	SavedObject.Id = 1;
	SavedObject.Name = "effect mesh";
	SavedObject.Mesh = MakePlaybackEffectMesh( true, 0.5f );
	SavedWorld.Objects.Add( SavedObject );
	const FString SavedWorldPath = "mesh-material-playback/world.world";
	const bool bSavedPathUnused = !NFileSystem::FileExists( Dir / SavedWorldPath );
	const bool bWorldSaved = bSavedPathUnused && NFileSystem::EnsureDirectory( Dir / "mesh-material-playback" ) && Assets.SaveWorld( SavedWorldPath, SavedWorld );
	TSharedPtr<CWorldAsset> FreshWorldAsset = bWorldSaved ? Assets.LoadWorld( SavedWorldPath ) : TSharedPtr<CWorldAsset>();
	CScene SpawnScene;
	FWorldSpawnOptions SpawnOptions;
	SpawnOptions.Assets = &Assets;
	FWorldInstance SpawnedWorld = FreshWorldAsset.IsValid() ? NWorld::Spawn( SpawnScene, FreshWorldAsset->GetDefinitionShared(), FWorldBindings(), SpawnOptions ) : FWorldInstance();
	TSharedPtr<CGameObject> SpawnedObject = SpawnedWorld.FindByName( "effect mesh" );
	FMeshRenderer* SpawnedRenderer = SpawnedObject.IsValid() ? SpawnedObject->GetData<FMeshRenderer>() : nullptr;
	const bool bSpawnReady = SpawnedRenderer != nullptr && SpawnedRenderer->IsValid() && std::abs( SpawnedRenderer->GetEffectTime() ) < 1.0e-9;
	if ( bSpawnReady ) SpawnScene.GetSystems().Run( NEcs::EPhase::PreUpdate, 0.125f, ~0ull, SpawnScene.GetEcsWorld(), SpawnScene.GetTransformWorld(), nullptr );
	const FDrawBuffer SpawnedBuffer = SpawnedRenderer != nullptr ? RecordMeshEffectRenderer( *SpawnedRenderer, FTransform::Identity, EDrawBlend::Opaque, 255 ) : FDrawBuffer();
	C.Check( bWorldSaved && FreshWorldAsset.IsValid() && bSpawnReady && std::abs( SpawnedRenderer->GetEffectTime() - 0.125 ) < 1.0e-6 && CountMeshEffectCommands( SpawnedBuffer, EDrawCommand::MaterialMesh3D ) > 0, "保存worldをfresh loadして通常Spawn後にECS PreUpdateが時計を進め、scene由来rendererが材質meshを記録する" );
	SpawnedWorld.Despawn();
	SpawnScene.DestroyAllObjects();
	SpawnedObject.Reset();
	SpawnedWorld = FWorldInstance();
	FreshWorldAsset.Reset();
	Assets.ReleaseUnused();
	Assets.Tick( 0.0f );
	if ( bWorldSaved ) C.Check( NFileSystem::RemoveFile( Dir / SavedWorldPath ), "この試験で作った一時world fileを片付ける" );
	Assets.SetRootDirectory( PreviousRoot );
}
