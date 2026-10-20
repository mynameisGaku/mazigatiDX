// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"
#include "WorldMeshMaterialSelfTestSupport.h"

#include <cmath>
#include <vector>

#include "Library/Core/Interfaces/Render/DrawMaterialMesh.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Render/MeshRender.h"
#include "Library/Framework/World/Mesh/EditMeshPrimitives.h"
#include "Library/Framework/World/WorldRender.h"
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
	FEditMeshMaterial& Material = Mesh.Materials.Add();
	Material.Effect.Material.bEnabled = true;
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
	Copy.Update( 0.125f );
	C.Check( std::abs( Copy.GetEffectTime() - 0.125 ) < 1.0e-6 && std::abs( Renderer.GetEffectTime() ) < 1.0e-9, "renderer copyの更新は元個体の時計を変えない" );
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
	C.Check( Copy.IsEffectsFinished() && !Copy.IsEffectsPlaying() && CountMeshEffectCommands( FinishedBuffer, EDrawCommand::MaterialMesh3D ) == 1 && std::abs( Copy.GetEffectTime() - 0.5 ) < 1.0e-6, "非loop終端は最終材質表示と終端時計を保つ" );
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
	// World座標が一度だけ変換されることをcommandの頂点から確認する。
	bool bFoundTranslatedPosition = false;
	const auto ReadTranslatedVertex = [&]( const FDrawCommandView& Command )
	{
		if ( Command.Type != EDrawCommand::MaterialMesh3D || Command.NumDataWords < static_cast<int32>( sizeof( FDrawMaterialMeshData ) / 4 + sizeof( FMeshVertex ) / 4 ) ) return;
		const FDrawMaterialMeshData Header = Command.Read<FDrawMaterialMeshData>();
		FMeshVertex Vertex;
		std::memcpy( &Vertex, Command.Data + sizeof( FDrawMaterialMeshData ) / 4, sizeof( Vertex ) );
		bFoundTranslatedPosition = Header.NumVertices >= 3 && std::abs( Vertex.Position[0] - 2.0f ) < 1.0e-5f;
	};
	ForEachDrawCommand( FDrawSpan{ AddBuffer.Words.data(), static_cast<int32>( AddBuffer.Words.size() ) }, ReadTranslatedVertex );
	C.Check( bFoundTranslatedPosition, "MaterialMesh頂点へworld位置を一度だけ適用する" );
	Assets.SetRootDirectory( PreviousRoot );
}
