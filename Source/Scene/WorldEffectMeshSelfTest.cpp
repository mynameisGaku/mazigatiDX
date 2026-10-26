// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/EffectEditorInternal.h"
#include "Library/Core/Interfaces/Render/DrawMaterialMesh.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Effect/ParticleEffectBackend.h"
#include "Library/Framework/Effect/EffectPathGeometry.h"
#include "Library/Framework/Effect/ParticleSimulation.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/World/WorldDefinition.h"
#include "Library/Platform/FileSystem.h"
#include "VendorModules/ImGui/imgui_internal.h"

namespace
{
	// mesh描画命令のheaderと配列位置をひとまとめにする読み取り結果。
	struct FRecordedMesh
	{
		// 描画命令の固定設定。
		FDrawMaterialMeshData Data;
		// command bufferから型付きで所有する頂点payload。
		TArray<FMeshVertex> Vertices;
		// command bufferから型付きで所有するindex payload。
		TArray<uint32> Indices;
	};

	// bufferから材質mesh命令を検証して型付き配列へ所有コピーする。
	TArray<FRecordedMesh> ReadMaterialMeshes( const FDrawBuffer& Buffer )
	{
		// 記録順に見つかったmesh command view。
		TArray<FRecordedMesh> Meshes;
		if ( Buffer.Words.empty() ) return Meshes;
		// mesh命令の固定部分から後続配列の先頭を求める。
		const auto ReadCommand = [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type != EDrawCommand::MaterialMesh3D || Command.NumDataWords < static_cast<int32>( sizeof( FDrawMaterialMeshData ) / 4 ) ) return;
			// 固定headerと配列の境界を確認する。
			const FDrawMaterialMeshData Data = Command.Read<FDrawMaterialMeshData>();
			if ( !NDrawMaterialMesh::IsValidHeader( Data ) ) return;
			const int64 RequiredWords = static_cast<int64>( sizeof( FDrawMaterialMeshData ) / 4 ) + static_cast<int64>( Data.NumVertices ) * sizeof( FMeshVertex ) / 4 + Data.NumIndices;
			if ( RequiredWords != Command.NumDataWords ) return;
			// command payload内の頂点とindex列の位置。
			const uint32* VertexWords = Command.Data + sizeof( FDrawMaterialMeshData ) / 4;
			FRecordedMesh Mesh;
			Mesh.Data = Data;
			Mesh.Vertices.SetNum( Data.NumVertices );
			Mesh.Indices.SetNum( Data.NumIndices );
			std::memcpy( Mesh.Vertices.GetData(), VertexWords, static_cast<size_t>( Data.NumVertices ) * sizeof( FMeshVertex ) );
			std::memcpy( Mesh.Indices.GetData(), VertexWords + Data.NumVertices * sizeof( FMeshVertex ) / 4, static_cast<size_t>( Data.NumIndices ) * sizeof( uint32 ) );
			if ( !NDrawMaterialMesh::IsValid( Mesh.Data, Mesh.Vertices.GetData(), Mesh.Indices.GetData() ) ) return;
			Meshes.Add( Mesh );
		};
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, ReadCommand );
		return Meshes;
	}

	// 既定値省略、不正値の永続化、geometryの位相と失敗時原子性を検査する。
	void TestMeshDefinitionAndGeometry( NAssetTest::FChecker& C )
	{
		C.Group = "effect-mesh-definition-geometry";
		// Meshを追加する前の既定emitter相当の定義。
		FParticleEffectDefinition Legacy;
		FParticleEmitterDefinition LegacyEmitter;
		Legacy.Emitters.Add( LegacyEmitter );
		// 新規Mesh fieldが旧JSONとhashへ影響しないことを確認する。
		const NlohmannJson LegacyJson = Legacy.ToJson();
		FParticleEffectDefinition LegacyRoundTrip;
		C.Check( Legacy.ComputeHash() == 0xA811CEF10E93C86Eull && !LegacyJson["Emitters"][0].contains( "Mesh" ) && FParticleEffectDefinition::FromJson( LegacyJson, LegacyRoundTrip ) && LegacyRoundTrip.ComputeHash() == Legacy.ComputeHash(), "既定meshは旧JSONから省略されlegacy golden hashを保つ" );
		// 有効な円環の値を保存形式へ変換する定義。
		FParticleEffectDefinition RingDefinition = Legacy;
		RingDefinition.Emitters[0].Mesh.Kind = FEffectMeshDefinition::EKind::Ring;
		RingDefinition.Emitters[0].Mesh.Segments = 12;
		RingDefinition.Emitters[0].Mesh.InnerRadius = 0.4f;
		RingDefinition.Emitters[0].Mesh.RotationDegrees = FVector3( 10.0f, -20.0f, 35.0f );
		RingDefinition.Emitters[0].MaxParticles = 1;
		// geometry試験に使う保存値。
		const FEffectMeshDefinition& Ring = RingDefinition.Emitters[0].Mesh;
		// JSONを読み直してmesh値・hash・Validate結果を照合する。
		FParticleEffectDefinition RingRoundTrip;
		C.Check( !FParticleEffectDefinition::HasErrors( RingDefinition.Validate() ) && FParticleEffectDefinition::FromJson( RingDefinition.ToJson(), RingRoundTrip ) && RingRoundTrip.ComputeHash() == RingDefinition.ComputeHash() && RingRoundTrip.Emitters[0].Mesh.Kind == FEffectMeshDefinition::EKind::Ring && RingRoundTrip.Emitters[0].Mesh.Segments == 12 && RingRoundTrip.Emitters[0].Mesh.InnerRadius == 0.4f && RingRoundTrip.Emitters[0].Mesh.RotationDegrees == FVector3( 10.0f, -20.0f, 35.0f ), "meshのKind・分割・穴・角度がJSONとhashで往復する" );
		C.Check( RingRoundTrip.ComputeHash() != Legacy.ComputeHash(), "非既定meshはlegacy定義と異なるhashを持つ" );
		// Unknown kind、unsigned overflow、float整数風、boolを含む不正JSON例。
		const NlohmannJson InvalidSamples[] = {
			NlohmannJson::parse( R"({"Emitters":[{"Mesh":{"Kind":"Disk"}}]})" ),
			NlohmannJson::parse( R"({"Emitters":[{"Mesh":{"Kind":"Ring","Segments":4294967344}}]})" ),
			NlohmannJson::parse( R"({"Emitters":[{"Mesh":{"Kind":"Ring","Segments":48.0}}]})" ),
			NlohmannJson::parse( R"({"Emitters":[{"Mesh":{"Kind":"Ring","Segments":true}}]})" ),
		};
		// 不正値をjson往復しても保存検証が通らないことを個別に確かめる。
		bool bEveryInvalidRemainsInvalid = true;
		for ( const NlohmannJson& Json : InvalidSamples )
		{
			FParticleEffectDefinition Invalid;
			FParticleEffectDefinition InvalidRoundTrip;
			const bool bParsed = FParticleEffectDefinition::FromJson( Json, Invalid ) && FParticleEffectDefinition::FromJson( Invalid.ToJson(), InvalidRoundTrip );
			bEveryInvalidRemainsInvalid = bEveryInvalidRemainsInvalid && bParsed && FParticleEffectDefinition::HasErrors( Invalid.Validate() ) && FParticleEffectDefinition::HasErrors( InvalidRoundTrip.Validate() );
		}
		C.Check( bEveryInvalidRemainsInvalid, "unknown kind・overflow・float・boolをJSON往復後もValidateで拒否する" );
		// Planeは外枠の2三角形、Ringは実際に中心の開いた帯を作る。
		FEffectMeshDefinition Plane;
		Plane.Kind = FEffectMeshDefinition::EKind::Plane;
		FEffectMeshGeometry PlaneGeometry;
		const bool bPlaneBuilt = NEffectMesh::BuildGeometry( Plane, PlaneGeometry );
		C.Check( bPlaneBuilt && PlaneGeometry.Vertices.Num() == 4 && PlaneGeometry.Indices.Num() == 6 && PlaneGeometry.Vertices[0].Position[0] == -0.5f && PlaneGeometry.Vertices[0].Position[1] == -0.5f && PlaneGeometry.Vertices[2].Position[0] == 0.5f && PlaneGeometry.Vertices[2].Position[1] == 0.5f, "Plane geometryはXY面の4頂点と6 indexを作る" );
		FEffectMeshGeometry RingGeometry;
		const bool bRingBuilt = NEffectMesh::BuildGeometry( Ring, RingGeometry );
		C.Check( bRingBuilt && RingGeometry.Vertices.Num() == 26 && RingGeometry.Indices.Num() == 72, "12分割Ringは26頂点と72 indexを作る" );
		if ( bRingBuilt && RingGeometry.Vertices.Num() == 26 && RingGeometry.Indices.Num() == 72 )
		{
			// seamは先頭頂点の全fieldを保ち、Uだけ1へ変更する。
			FMeshVertex SeamCopy = RingGeometry.Vertices[24];
			SeamCopy.U = 0.0f;
			const bool bSeamMatches = std::memcmp( &SeamCopy, &RingGeometry.Vertices[0], sizeof( FMeshVertex ) ) == 0 && RingGeometry.Vertices[24].U == 1.0f && RingGeometry.Vertices[25].U == 1.0f;
			// 三角形ごとに実index範囲・向き・穴の内側を確認する。
			// 全三角形の向き・穴・半径検査が通ったか。
			bool bTrianglesValid = true;
			// 実indexが示す先頭三角形の符号付き面積。
			float32 FirstTriangleArea = 0.0f;
			for ( int32 Index = 0; Index < RingGeometry.Indices.Num(); Index += 3 )
			{
				// 現在の三角形が参照する頂点番号。
				const uint32 AIndex = RingGeometry.Indices[Index];
				const uint32 BIndex = RingGeometry.Indices[Index + 1];
				const uint32 CIndex = RingGeometry.Indices[Index + 2];
				if ( AIndex >= static_cast<uint32>( RingGeometry.Vertices.Num() ) || BIndex >= static_cast<uint32>( RingGeometry.Vertices.Num() ) || CIndex >= static_cast<uint32>( RingGeometry.Vertices.Num() ) ) { bTrianglesValid = false; break; }
				// index検査後に参照する三角形の型付き頂点。
				const FMeshVertex& A = RingGeometry.Vertices[static_cast<int32>( AIndex )];
				const FMeshVertex& B = RingGeometry.Vertices[static_cast<int32>( BIndex )];
				const FMeshVertex& CVertex = RingGeometry.Vertices[static_cast<int32>( CIndex )];
				// ABとACのXY差分から向きと面積を求める。
				const float32 ABx = B.Position[0] - A.Position[0];
				const float32 ABy = B.Position[1] - A.Position[1];
				const float32 ACx = CVertex.Position[0] - A.Position[0];
				const float32 ACy = CVertex.Position[1] - A.Position[1];
				// 三頂点の中心からの距離が穴と外周の間にあるか調べる。
				const float32 Area = ABx * ACy - ABy * ACx;
				const float32 ARadius = std::sqrt( A.Position[0] * A.Position[0] + A.Position[1] * A.Position[1] );
				const float32 BRadius = std::sqrt( B.Position[0] * B.Position[0] + B.Position[1] * B.Position[1] );
				const float32 CRadius = std::sqrt( CVertex.Position[0] * CVertex.Position[0] + CVertex.Position[1] * CVertex.Position[1] );
				// 原点が三辺すべての内側にある場合だけ穴を埋めたと判定する。
				const float32 ABOrigin = ABx * -A.Position[1] - ABy * -A.Position[0];
				const float32 BCx = CVertex.Position[0] - B.Position[0];
				const float32 BCy = CVertex.Position[1] - B.Position[1];
				const float32 BCOrigin = BCx * -B.Position[1] - BCy * -B.Position[0];
				const float32 CAx = A.Position[0] - CVertex.Position[0];
				const float32 CAy = A.Position[1] - CVertex.Position[1];
				const float32 CAOrigin = CAx * -CVertex.Position[1] - CAy * -CVertex.Position[0];
				// 三辺の符号がそろえば原点は三角形内にある。
				const bool bOriginInside = ABOrigin >= -1.0e-7f && BCOrigin >= -1.0e-7f && CAOrigin >= -1.0e-7f;
				bTrianglesValid = bTrianglesValid && Area > 0.0f && ARadius >= 0.2f - 1.0e-6f && BRadius >= 0.2f - 1.0e-6f && CRadius >= 0.2f - 1.0e-6f && ARadius <= 0.5f + 1.0e-6f && BRadius <= 0.5f + 1.0e-6f && CRadius <= 0.5f + 1.0e-6f && !bOriginInside;
				if ( Index == 0 ) FirstTriangleArea = Area;
			}
			// 最初の三角形から参照頂点を読む前にindex範囲を確かめる。
			const bool bFirstTriangleIndicesValid = RingGeometry.Indices[0] < static_cast<uint32>( RingGeometry.Vertices.Num() ) && RingGeometry.Indices[1] < static_cast<uint32>( RingGeometry.Vertices.Num() ) && RingGeometry.Indices[2] < static_cast<uint32>( RingGeometry.Vertices.Num() );
			if ( bFirstTriangleIndicesValid )
			{
				// 実indexが指す最初の三角形の内外UVとwindingを確認する。
				const FMeshVertex& Inner = RingGeometry.Vertices[static_cast<int32>( RingGeometry.Indices[0] )];
				const FMeshVertex& Outer = RingGeometry.Vertices[static_cast<int32>( RingGeometry.Indices[1] )];
				C.Check( bSeamMatches && bTrianglesValid && NMath::Abs( Inner.Position[0] - 0.2f ) < 1.0e-6f && NMath::Abs( Outer.Position[0] - 0.5f ) < 1.0e-6f && Inner.V == 0.0f && Outer.V == 1.0f && FirstTriangleArea > 0.0f, "Ringは実穴・外半径・UV・実index正面winding・byte一致seamを持つ" );
			}
			else C.Check( false, "Ring最初の三角形のindexが頂点範囲内にある" );
		}
		// 失敗したgeometry生成が既存出力を部分的に消さない。
		FEffectMeshGeometry Preserved = RingGeometry;
		FEffectMeshDefinition InvalidGeometry = Ring;
		InvalidGeometry.Segments = 2;
		C.Check( !NEffectMesh::BuildGeometry( InvalidGeometry, RingGeometry ) && RingGeometry.Vertices.Num() == Preserved.Vertices.Num() && RingGeometry.Indices.Num() == Preserved.Indices.Num(), "不正geometry生成は既存出力を変更しない" );
	}

	// Playbackの実mesh commandを記録し、変換・材質・枠順を確認する。
	void TestMeshPlayback( NAssetTest::FChecker& C, CAssetManager& Assets )
	{
		C.Group = "effect-mesh-playback";
		// 画像なしでは旧softdotが選ばれるためsource試験用にmanager所有textureを作る。
		const std::vector<uint8> Pixel = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 32, 0x28, 255, 255, 255, 255 };
		const TSharedPtr<CTexture> Texture = Assets.CreateTextureFromMemory( "effect-mesh-selftest-source", Pixel.data(), Pixel.size() );
		C.Check( Texture.IsValid() && Texture->GetHandle() >= 0, "Playback用source textureを用意する" );
		if ( !Texture.IsValid() ) return;
		// 固定角度と非一様effect scaleを持つ1粒子mesh。
		FParticleEmitterDefinition Emitter = NEffectTest::QuietEmitter();
		Emitter.Name = "Mesh playback";
		Emitter.Space = EParticleSpace::Local;
		Emitter.Texture = "Effects/mesh-selftest-source.tga";
		Emitter.MaxParticles = 1;
		Emitter.Lifetime = { 10.0f, 10.0f };
		Emitter.StartSize = { 1.0f, 1.0f };
		Emitter.EndSize = { 1.0f, 1.0f };
		Emitter.StartColor = FColor32( 210, 150, 100, 128 );
		Emitter.EndColor = Emitter.StartColor;
		Emitter.StartAngle = { 90.0f, 90.0f };
		Emitter.Spin = { 0.0f, 0.0f };
		Emitter.Bursts.Add( NEffectTest::Burst( 0.0f, 1 ) );
		Emitter.Mesh.Kind = FEffectMeshDefinition::EKind::Plane;
		Emitter.Mesh.RotationDegrees = FVector3( 0.0f, 20.0f, 15.0f );
		Emitter.Material.bEnabled = false;
		// 同じsimulationへ適用するワールド位置・回転・非一様倍率。
		const FTransform World( FVector3( 3.0f, -2.0f, 5.0f ), FQuaternion::EulerToQuat( 0.0f, 0.0f, 35.0f * NMath::DEG2RAD ), FVector3( 2.0f, 4.0f, 6.0f ) );
		const TSharedPtr<FParticleEffectDefinition> Definition = NEffectTest::MakeEffect( { Emitter } );
		TArray<TSharedPtr<CTexture>> Textures;
		Textures.Add( Texture );
		// 既存4引数constructorからも定義snapshotとgeometryを作る。
		FParticlePlayback Playback( Definition, Textures, FEffectPlayParams(), World );
		Playback.Update( 0.01f );
		// Playbackの現在粒子と実draw commandを照合する。
		TArray<NParticle::FDrawParticle> DrawParticles;
		NParticle::CollectDrawParticles( Playback.GetSimulation(), DrawParticles );
		FDrawBuffer FirstBuffer;
		FDrawContext FirstContext( FirstBuffer );
		Playback.Record( FirstContext );
		const TArray<FRecordedMesh> FirstMeshes = ReadMaterialMeshes( FirstBuffer );
		C.Check( DrawParticles.Num() == 1 && FirstMeshes.Num() == 1 && FirstMeshes[0].Data.NumVertices == 4 && FirstMeshes[0].Data.NumIndices == 6, "Playbackは固定Planeを一つの材質mesh命令に記録する" );
		if ( DrawParticles.Num() == 1 && FirstMeshes.Num() == 1 )
		{
			const NParticle::FDrawParticle& Particle = DrawParticles[0];
			const FRecordedMesh& Recorded = FirstMeshes[0];
			// geometry→spin→mesh角度→effect角度の契約順。
			const FQuaternion Spin = FQuaternion::FromAxisAngle( FVector3::Forward, Particle.Angle );
			const FVector3 MeshRadians = Emitter.Mesh.RotationDegrees * NMath::DEG2RAD;
			const FQuaternion ShapeRotation = FQuaternion::EulerToQuat( MeshRadians );
			const FQuaternion ExpectedRotation = World.Rotation * ShapeRotation * Spin;
			const FVector3 LocalFirst( -0.5f * Particle.Size, -0.5f * Particle.Size, 0.0f );
			const FVector3 ExpectedFirst = Particle.Position + ExpectedRotation.Rotate( LocalFirst );
			const bool bPositionMatches = NMath::Abs( Recorded.Vertices[0].Position[0] - ExpectedFirst.x ) < 1.0e-4f && NMath::Abs( Recorded.Vertices[0].Position[1] - ExpectedFirst.y ) < 1.0e-4f && NMath::Abs( Recorded.Vertices[0].Position[2] - ExpectedFirst.z ) < 1.0e-4f;
			// mesh command先頭頂点が粒子RGBAを変えず持つか。
			const bool bVertexColorMatches = Recorded.Vertices[0].Color[0] == 210 && Recorded.Vertices[0].Color[1] == 150 && Recorded.Vertices[0].Color[2] == 100 && Recorded.Vertices[0].Color[3] == 128;
			C.Check( bPositionMatches && NMath::Abs( Particle.Size - 4.0f ) < 1.0e-4f && bVertexColorMatches, "mesh頂点へ粒子のRGBAをそのまま写しsizeと回転順を保つ" );
			C.Check( Recorded.Data.Texture == Texture->GetHandle() && Recorded.Data.bTwoSided == 1 && Recorded.Data.Material.UVScale[0] == 1.0f && Recorded.Data.Material.MaskScale == 1.0f && Recorded.Data.DistortionTexture == -1 && Recorded.Data.DistortionStrength == 0.0f && Recorded.Data.Emission == 1.0f, "材質OFF meshはneutral材質と固定sourceを使う" );
		}
		// 同じstateの繰り返し記録は完全に同じbytesを作る。
		FDrawBuffer SecondBuffer;
		FDrawContext SecondContext( SecondBuffer );
		Playback.Record( SecondContext );
		C.Check( FirstBuffer.Words == SecondBuffer.Words, "mesh PlaybackのRecordを繰り返してもcommand bytesが一致する" );
		// 定義編集は開始済みPlaybackのmesh snapshotを変えない。
		Definition->Emitters[0].Mesh.RotationDegrees = FVector3::Zero;
		FDrawBuffer AfterDefinitionEdit;
		FDrawContext AfterDefinitionEditContext( AfterDefinitionEdit );
		Playback.Record( AfterDefinitionEditContext );
		C.Check( FirstBuffer.Words == AfterDefinitionEdit.Words, "開始後の元定義変更はPlayback geometry snapshotへ波及しない" );
		// 透明な最初の64粒子が枠を使い、65番目の可視粒子は記録されない。
		FParticleEmitterDefinition InvisibleEmitter = NEffectTest::QuietEmitter();
		InvisibleEmitter.Name = "Invisible capacity owner";
		InvisibleEmitter.Space = EParticleSpace::Local;
		InvisibleEmitter.MaxParticles = 64;
		InvisibleEmitter.Bursts.Add( NEffectTest::Burst( 0.0f, 64 ) );
		InvisibleEmitter.StartColor.a = 0;
		InvisibleEmitter.EndColor.a = 0;
		InvisibleEmitter.Mesh.Kind = FEffectMeshDefinition::EKind::Plane;
		FParticleEmitterDefinition VisibleEmitter = InvisibleEmitter;
		VisibleEmitter.Name = "After the global limit";
		VisibleEmitter.MaxParticles = 1;
		VisibleEmitter.Bursts.Reset();
		VisibleEmitter.Bursts.Add( NEffectTest::Burst( 0.0f, 1 ) );
		VisibleEmitter.StartColor.a = 255;
		VisibleEmitter.EndColor.a = 255;
		const TSharedPtr<FParticleEffectDefinition> CapacityDefinition = NEffectTest::MakeEffect( { InvisibleEmitter, VisibleEmitter } );
		// 各emitterから同じready sourceを選べるようtexture枠を揃える。
		TArray<TSharedPtr<CTexture>> CapacityTextures;
		CapacityTextures.Add( Texture );
		CapacityTextures.Add( Texture );
		FParticlePlayback CapacityPlayback( CapacityDefinition, CapacityTextures, FEffectPlayParams(), FTransform::Identity );
		CapacityPlayback.Update( 0.01f );
		FDrawBuffer CapacityBuffer;
		FDrawContext CapacityContext( CapacityBuffer );
		CapacityPlayback.Record( CapacityContext );
		C.Check( CapacityPlayback.GetNumParticles() == 65 && ReadMaterialMeshes( CapacityBuffer ).IsEmpty(), "64枠は透明度確認より前にemitter順で固定する" );
		// 同じ65粒子をすべて可視にして、mesh枠が64命令を実際に通すことを確認する。
		FParticleEmitterDefinition VisibleMeshEmitter = VisibleEmitter;
		VisibleMeshEmitter.Name = "Visible mesh capacity";
		VisibleMeshEmitter.MaxParticles = 64;
		VisibleMeshEmitter.Bursts.Reset();
		VisibleMeshEmitter.Bursts.Add( NEffectTest::Burst( 0.0f, 64 ) );
		// 65番目もmeshにして枠上限でだけ止まるpositive control。
		FParticleEmitterDefinition VisibleMeshTail = VisibleEmitter;
		VisibleMeshTail.Name = "Visible mesh tail";
		VisibleMeshTail.Mesh.Kind = FEffectMeshDefinition::EKind::Plane;
		const TSharedPtr<FParticleEffectDefinition> VisibleCapacityDefinition = NEffectTest::MakeEffect( { VisibleMeshEmitter, VisibleMeshTail } );
		FParticlePlayback VisibleCapacityPlayback( VisibleCapacityDefinition, CapacityTextures, FEffectPlayParams(), FTransform::Identity );
		VisibleCapacityPlayback.Update( 0.01f );
		FDrawBuffer VisibleCapacityBuffer;
		FDrawContext VisibleCapacityContext( VisibleCapacityBuffer );
		VisibleCapacityPlayback.Record( VisibleCapacityContext );
		C.Check( VisibleCapacityPlayback.GetNumParticles() == 65 && ReadMaterialMeshes( VisibleCapacityBuffer ).Num() == 64, "65個の可視meshから先頭64個を記録する" );
		// 不正Meshは描画せず、後続emitterの有効mesh枠も奪わない。
		FParticleEmitterDefinition InvalidCapacityEmitter = VisibleMeshEmitter;
		InvalidCapacityEmitter.Name = "Invalid mesh capacity owner";
		InvalidCapacityEmitter.Mesh.Segments = 129;
		FParticleEmitterDefinition ValidCapacityTail = VisibleEmitter;
		ValidCapacityTail.Name = "Valid mesh after invalid capacity";
		const TSharedPtr<FParticleEffectDefinition> InvalidCapacityDefinition = NEffectTest::MakeEffect( { InvalidCapacityEmitter, ValidCapacityTail } );
		FParticlePlayback InvalidCapacityPlayback( InvalidCapacityDefinition, CapacityTextures, FEffectPlayParams(), FTransform::Identity );
		InvalidCapacityPlayback.Update( 0.01f );
		FDrawBuffer InvalidCapacityBuffer;
		FDrawContext InvalidCapacityContext( InvalidCapacityBuffer );
		InvalidCapacityPlayback.Record( InvalidCapacityContext );
		C.Check( !InvalidCapacityEmitter.Mesh.IsValidSettings() && InvalidCapacityPlayback.GetNumParticles() == 65 && ReadMaterialMeshes( InvalidCapacityBuffer ).Num() == 1, "不正な先行meshは64枠を消費せず後続の有効meshを描く" );
		// meshとquadのemitter順を替えた同数playbackを交互に記録する。
		FParticleEmitterDefinition OneQuad = VisibleEmitter;
		OneQuad.Name = "Interleaved quad";
		OneQuad.Mesh = FEffectMeshDefinition();
		const TSharedPtr<FParticleEffectDefinition> MeshThenQuadDefinition = NEffectTest::MakeEffect( { VisibleMeshEmitter, OneQuad } );
		const TSharedPtr<FParticleEffectDefinition> QuadThenMeshDefinition = NEffectTest::MakeEffect( { OneQuad, VisibleMeshEmitter } );
		FParticlePlayback MeshThenQuadPlayback( MeshThenQuadDefinition, CapacityTextures, FEffectPlayParams(), FTransform::Identity );
		FParticlePlayback QuadThenMeshPlayback( QuadThenMeshDefinition, CapacityTextures, FEffectPlayParams(), FTransform::Identity );
		MeshThenQuadPlayback.Update( 0.01f );
		QuadThenMeshPlayback.Update( 0.01f );
		// emitter順が違う再生を続けても、記録時のmesh選択枠を持ち越さない。
		FDrawBuffer MeshThenQuadBuffer;
		FDrawBuffer QuadThenMeshBuffer;
		FDrawBuffer MeshThenQuadAgainBuffer;
		FDrawContext MeshThenQuadContext( MeshThenQuadBuffer );
		FDrawContext QuadThenMeshContext( QuadThenMeshBuffer );
		FDrawContext MeshThenQuadAgainContext( MeshThenQuadAgainBuffer );
		MeshThenQuadPlayback.Record( MeshThenQuadContext );
		QuadThenMeshPlayback.Record( QuadThenMeshContext );
		MeshThenQuadPlayback.Record( MeshThenQuadAgainContext );
		C.Check( MeshThenQuadPlayback.GetNumParticles() == 65 && QuadThenMeshPlayback.GetNumParticles() == 65 && ReadMaterialMeshes( MeshThenQuadBuffer ).Num() == 64 && ReadMaterialMeshes( QuadThenMeshBuffer ).Num() == 64 && ReadMaterialMeshes( MeshThenQuadAgainBuffer ).Num() == 64, "meshとquadの順序を替えても同じ64枠で独立記録する" );
		// 互換不能なPath/Flipbook併用は形状を変えず、どの命令にも置換しない。
		FParticleEmitterDefinition ConflictEmitter = Emitter;
		ConflictEmitter.Flipbook.bEnabled = true;
		const TSharedPtr<FParticleEffectDefinition> ConflictDefinition = NEffectTest::MakeEffect( { ConflictEmitter } );
		FParticlePlayback ConflictPlayback( ConflictDefinition, Textures, FEffectPlayParams(), FTransform::Identity );
		ConflictPlayback.Update( 0.01f );
		FDrawBuffer ConflictBuffer;
		FDrawContext ConflictContext( ConflictBuffer );
		ConflictPlayback.Record( ConflictContext );
		C.Check( ConflictBuffer.Words.empty(), "MeshとFlipbookの併用はquad/mesh命令へfallbackしない" );
		// Pathの各形状もmeshと併用せず、専用命令へfallbackしない。
		const FEffectPathDefinition::EKind ConflictingPaths[] = { FEffectPathDefinition::EKind::Ribbon, FEffectPathDefinition::EKind::Lightning, FEffectPathDefinition::EKind::TreeLightning };
		// 各PathケースをCheck結果で識別する名前。
		const char* ConflictNames[] = { "ribbon", "lightning", "tree" };
		for ( int32 PathIndex = 0; PathIndex < 3; ++PathIndex )
		{
			// 現在のpath kindだけを変更する競合emitter。
			FParticleEmitterDefinition PathConflictEmitter = Emitter;
			PathConflictEmitter.Name = FString( "mesh-path-conflict-" ) + ConflictNames[PathIndex];
			PathConflictEmitter.Path.Kind = ConflictingPaths[PathIndex];
			PathConflictEmitter.Path.End = FVector3( 0.0f, 2.0f, 0.0f );
			PathConflictEmitter.Path.Segments = 8;
			PathConflictEmitter.Path.Branches = 1;
			PathConflictEmitter.Path.Tree.StrokeSeconds = 0.5f;
			PathConflictEmitter.Path.Tree.StrokeInterval = 0.6f;
			const TSharedPtr<FParticleEffectDefinition> PathConflictDefinition = NEffectTest::MakeEffect( { PathConflictEmitter } );
			FParticlePlayback PathConflictPlayback( PathConflictDefinition, Textures, FEffectPlayParams(), FTransform::Identity );
			PathConflictPlayback.Update( 0.01f );
			FDrawBuffer PathConflictBuffer;
			FDrawContext PathConflictContext( PathConflictBuffer );
			PathConflictPlayback.Record( PathConflictContext );
			C.Check( PathConflictPlayback.GetNumParticles() == 1 && PathConflictBuffer.Words.empty(), "MeshとPathの併用は各path命令へfallbackしない" );
		}
	}

	// 独立ImGui contextから本物のMesh panelへkeyboard入力する。
	class FMeshInputContext
	{
	public:
		// 入力試験用contextとfont atlasを作る。
		FMeshInputContext() : Previous( ImGui::GetCurrentContext() ), Context( ImGui::CreateContext() )
		{
			ImGui::SetCurrentContext( Context );
			ImGuiIO& Io = ImGui::GetIO();
			Io.DisplaySize = ImVec2( 1000.0f, 700.0f );
			Io.DeltaTime = 1.0f / 60.0f;
			Io.IniFilename = nullptr;
			Io.LogFilename = nullptr;
			Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			Io.ConfigInputTrickleEventQueue = false;
			Io.Fonts->AddFontDefaultBitmap();
			bReady = Io.Fonts->Build();
		}

		// 独立contextを捨てて呼出し元を戻す。
		~FMeshInputContext()
		{
			ImGui::DestroyContext( Context );
			ImGui::SetCurrentContext( Previous );
		}
		// ImGui contextの複製を禁止する。
		FMeshInputContext( const FMeshInputContext& ) = delete;
		// ImGui contextの代入を禁止する。
		FMeshInputContext& operator=( const FMeshInputContext& ) = delete;

		// production同様にemitter copyを編集してdocumentへ一度記録する。
		bool Frame( FEffectEditorState& State )
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos( ImVec2( 0.0f, 0.0f ), ImGuiCond_Always );
			ImGui::SetNextWindowSize( ImVec2( 900.0f, 650.0f ), ImGuiCond_Always );
			if ( ImGui::Begin( "EffectMeshInputSelfTest", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize ) )
			{
				Window = ImGui::GetCurrentWindow();
				if ( State.Document.Get().Emitters.IsValidIndex( 0 ) )
				{
					// 実UIと同じ一時emitter上でpanelを操作する。
					FParticleEmitterDefinition Emitter = State.Document.Get().Emitters[0];
					if ( NEffectEditorUI::DrawMeshPanel( State, Emitter ) ) NParticleEdit::SetEmitter( State.Document, 0, Emitter, "emitter-settings" );
				}
				if ( !ImGui::IsAnyItemActive() ) State.Document.EndEditSession();
			}
			ImGui::End();
			ImGui::Render();
			bBalanced = bBalanced && !Context->WithinFrameScope && Context->CurrentWindowStack.Size == 0;
			return bBalanced;
		}

		// 指定keyboard keyを押して離し、panelを各frameで更新する。
		void Tap( FEffectEditorState& State, ImGuiKey Key )
		{
			ImGui::GetIO().AddKeyEvent( Key, true );
			Frame( State );
			ImGui::GetIO().AddKeyEvent( Key, false );
			Frame( State );
		}

		// tab巡回で指定widgetにkeyboard focusを移す。
		bool TabTo( FEffectEditorState& State, ImGuiID Id )
		{
			for ( int32 Attempt = 0; Attempt < 128; ++Attempt )
			{
				if ( Context->NavId == Id ) return true;
				Tap( State, ImGuiKey_Tab );
			}
			return Context->NavId == Id;
		}

		// 最後のpanel frameからitem識別子を得る。
		ImGuiID ItemId( const char* Label ) const { return Window->GetID( Label ); }
		// font atlasを使えるか返す。
		bool IsReady() const { return bReady; }
		// 各frameのBegin/EndとNewFrame/Renderが揃ったか返す。
		bool IsBalanced() const { return bBalanced; }

	private:
		// この試験を始める前のImGui context。
		ImGuiContext* Previous = nullptr;
		// 独立入力試験が所有するcontext。
		ImGuiContext* Context = nullptr;
		// 最後に描いたpanel window。
		ImGuiWindow* Window = nullptr;
		// 試験用font atlasを構築できたか。
		bool bReady = false;
		// 全frameでstackが釣り合ったか。
		bool bBalanced = true;
	};

	// kind変更をUndo/Redoし、不正JSONをMesh専用Resetで修復する。
	void TestMeshPanelHistory( NAssetTest::FChecker& C, FMeshInputContext& Input )
	{
		C.Group = "effect-mesh-panel-history";
		// None時にも保存する値を持つが、表示では隠すemitter。
		FParticleEmitterDefinition Original;
		Original.Texture = "Effects/material_sigil.tga";
		Original.Material.UVScroll = FVector2( 0.25f, -0.5f );
		Original.Mesh.Segments = 17;
		Original.Mesh.InnerRadius = 0.42f;
		Original.Mesh.RotationDegrees = FVector3( 12.0f, -8.0f, 25.0f );
		// 入力欄とUndo履歴を持つEditor state。
		FParticleEffectDefinition Definition;
		Definition.Emitters.Add( Original );
		FEffectEditorState State;
		State.Document.Reset( Definition );
		State.Document.MarkSaved();
		Input.Frame( State );
		C.Check( Input.TabTo( State, Input.ItemId( "Mesh kind" ) ), "Mesh kind comboへkeyboard focusする" );
		Input.Tap( State, ImGuiKey_Enter );
		Input.Tap( State, ImGuiKey_DownArrow );
		Input.Tap( State, ImGuiKey_Enter );
		const FParticleEmitterDefinition& Changed = State.Document.Get().Emitters[0];
		C.Check( Changed.Mesh.Kind == FEffectMeshDefinition::EKind::Plane && Changed.Mesh.Segments == 17 && Changed.Mesh.InnerRadius == 0.42f && Changed.Mesh.RotationDegrees == Original.Mesh.RotationDegrees && State.Document.IsDirty() && State.Document.GetNumUndo() == 1, "Kind comboは他の隠れmesh値を保ったまま一回の履歴を作る" );
		C.Check( State.Document.Undo() && State.Document.Get().Emitters[0].Mesh.Kind == FEffectMeshDefinition::EKind::None && State.Document.Get().Emitters[0].Mesh.Segments == 17 && State.Document.Redo() && State.Document.Get().Emitters[0].Mesh.Kind == FEffectMeshDefinition::EKind::Plane, "Kind切替だけをUndo/Redoする" );
		// 保存値を壊すJSONを読み、Reset操作でmeshだけを既定化する。
		const NlohmannJson InvalidJson = NlohmannJson::parse( R"({"Emitters":[{"Name":"Invalid mesh","Texture":"Effects/material_sigil.tga","Mesh":{"Kind":"Unknown"}}]})" );
		FParticleEffectDefinition InvalidDefinition;
		const bool bInvalidParsed = FParticleEffectDefinition::FromJson( InvalidJson, InvalidDefinition ) && FParticleEffectDefinition::HasErrors( InvalidDefinition.Validate() );
		FEffectEditorState InvalidState;
		InvalidState.Document.Reset( InvalidDefinition );
		InvalidState.Document.MarkSaved();
		// 先行するUndo/Redo操作のNavIdを持ち込まずResetだけを入力する。
		FMeshInputContext ResetInput;
		ResetInput.Frame( InvalidState );
		const bool bResetFocused = ResetInput.IsReady() && ResetInput.TabTo( InvalidState, ResetInput.ItemId( "Reset mesh" ) );
		C.Check( bInvalidParsed && bResetFocused, "不正Kind JSONを安全に表示しResetへ移動する" );
		if ( bResetFocused ) ResetInput.Tap( InvalidState, ImGuiKey_Enter );
		// Reset結果のemitter数を確認してから参照する。
		const FParticleEmitterDefinition* Reset = InvalidState.Document.Get().Emitters.IsValidIndex( 0 ) ? &InvalidState.Document.Get().Emitters[0] : nullptr;
		C.Check( Reset != nullptr && Reset->Mesh.IsDefault() && Reset->Mesh.Validate().IsEmpty() && Reset->Texture == "Effects/material_sigil.tga" && InvalidState.Document.IsDirty(), "Mesh Resetは型違いを直してImageを保ち文書をdirtyにする" );
		C.Check( ResetInput.IsBalanced(), "mesh Reset専用ImGui frameを正しく閉じる" );
	}

	// test専用に新規保存した二つのeffect fixtureを、早期returnでも消す。
	struct FMeshFixtureCleanup
	{
		// fixtureが作られたasset root。
		CAssetManager& Assets;
		// test中に新規作成するpresetの相対path。
		FString PresetPath;
		// test中に新規作成するSaveAs copyの相対path。
		FString CopyPath;

		// 所有fixtureを消し、存在確認できない場合はfalseを返す。
		bool Clean()
		{
			bool bClean = true;
			const TOptional<FString> PresetFullPath = Assets.ResolvePath( PresetPath );
			const TOptional<FString> CopyFullPath = Assets.ResolvePath( CopyPath );
			if ( PresetFullPath.IsSet() ) bClean = NFileSystem::RemoveFile( PresetFullPath.GetValue() ) && bClean;
			if ( CopyFullPath.IsSet() ) bClean = NFileSystem::RemoveFile( CopyFullPath.GetValue() ) && bClean;
			return bClean;
		}

		// early returnでも作成済みfixtureを残さない。
		~FMeshFixtureCleanup() { Clean(); }
	};

	// 固定mesh boundsのcamera fit、入力検証、pendingの一回性を調べる。
	void TestMeshCameraFocus( NAssetTest::FChecker& C )
	{
		C.Group = "effect-mesh-camera-focus";
		// 実際に投影する横長viewport。
		FEditorViewport Landscape;
		Landscape.Width = 640;
		Landscape.Height = 360;
		// 実際に投影する縦長viewport。
		FEditorViewport Portrait;
		Portrait.Width = 360;
		Portrait.Height = 640;
		// fit結果のカメラ状態を比較する共通確認。
		const auto CameraMatches = []( const FEditorCamera& A, const FEditorCamera& B )
		{
			return A.Target.x == B.Target.x && A.Target.y == B.Target.y && A.Target.z == B.Target.z && A.Yaw == B.Yaw && A.Pitch == B.Pitch && A.Distance == B.Distance;
		};
		// geometry外周の全点が画面内に投影されたか調べる。
		const auto ProjectsInside = []( const FEditorCamera& Camera, const FEditorViewport& View, const TArray<FVector3>& Points )
		{
			for ( const FVector3& Point : Points )
			{
				// 外周点の投影位置と奥行き。
				FVector2 Pixel;
				float32 Depth = 0.0f;
				if ( !Camera.WorldToScreen( View, Point, Pixel, &Depth ) || !NMath::IsFinite( Pixel.x ) || !NMath::IsFinite( Pixel.y ) || !NMath::IsFinite( Depth ) || Pixel.x < 1.0f || Pixel.y < 1.0f || Pixel.x > View.Width - 1.0f || Pixel.y > View.Height - 1.0f || Depth <= View.Near || Depth >= View.Far ) return false;
			}
			return true;
		};
		// 既定emitterをmesh試験用に整える。
		FParticleEmitterDefinition BaseEmitter = NEffectTest::QuietEmitter();
		BaseEmitter.MaxParticles = 1;
		BaseEmitter.StartSize = { 1.0f, 1.0f };
		BaseEmitter.EndSize = { 1.0f, 1.0f };
		BaseEmitter.Lifetime = { 1.0f, 1.0f };
		BaseEmitter.Speed = { 0.0f, 0.0f };
		BaseEmitter.ShapeRadius = 0.0f;
		BaseEmitter.Gravity = FVector3::Zero;
		BaseEmitter.Drag = 0.0f;
		BaseEmitter.Mesh.Kind = FEffectMeshDefinition::EKind::Plane;
		// PlaneとRingの外周を両aspectで投影して切れないことを確認する。
		for ( int32 AspectIndex = 0; AspectIndex < 2; ++AspectIndex )
		{
			// 現在のiterationに対応するviewport。
			const FEditorViewport& View = AspectIndex == 0 ? Landscape : Portrait;
			for ( int32 MeshIndex = 0; MeshIndex < 2; ++MeshIndex )
			{
				// PlaneまたはRingの一時定義。
				FParticleEmitterDefinition Emitter = BaseEmitter;
				Emitter.Mesh.Kind = MeshIndex == 0 ? FEffectMeshDefinition::EKind::Plane : FEffectMeshDefinition::EKind::Ring;
				Emitter.Mesh.Segments = 48;
				Emitter.Mesh.InnerRadius = 0.6f;
				Emitter.Mesh.RotationDegrees = FVector3( 15.0f, -25.0f, 30.0f );
				Emitter.EndSize = { 1.5f, 1.5f };
				Emitter.Appearance.Size.Keys.Add( { 0.0f, 1.5f } );
				Emitter.Appearance.Size.Keys.Add( { 1.0f, 2.0f } );
				// 今回の形状だけを持つ一時定義。
				FParticleEffectDefinition Definition;
				Definition.Emitters.Add( Emitter );
				// focusを適用するeditor state。
				FEffectEditorState State;
				State.Document.Reset( Definition );
				// 形状boundsへ一度focusできたか。
				const bool bFocused = NEffectEditorUI::FocusCameraOnEffectGeometry( State, View );
				C.Check( bFocused && State.Camera.Target.x == 0.0f && State.Camera.Target.y == 0.0f && State.Camera.Target.z == 0.0f, "PlaneとRingをlandscape・portraitで原点へfocusする" );
				// Appearance最大値を反映した外周半径。
				const float32 OuterRadius = ( MeshIndex == 0 ? std::sqrt( 0.5f ) : 0.5f ) * 3.0f;
				// 外周半径を直接fitした期待camera距離。
				FEditorCamera ExpectedShapeCamera;
				ExpectedShapeCamera.Focus( FVector3::Zero, OuterRadius, View );
				// 再利用する固定mesh geometryと回転後の投影点。
				FEffectMeshGeometry Geometry;
				const bool bGeometryBuilt = NEffectMesh::BuildGeometry( Emitter.Mesh, Geometry );
				TArray<FVector3> ProjectedVertices;
				const FQuaternion MeshRotation = FQuaternion::EulerToQuat( Emitter.Mesh.RotationDegrees * NMath::DEG2RAD );
				if ( bGeometryBuilt )
				{
					for ( const FMeshVertex& Vertex : Geometry.Vertices )
					{
						const FVector3 LocalPoint( Vertex.Position[0] * 3.0f, Vertex.Position[1] * 3.0f, Vertex.Position[2] * 3.0f );
						ProjectedVertices.Add( MeshRotation.Rotate( LocalPoint ) );
					}
				}
				C.Check( bGeometryBuilt && bFocused && ProjectsInside( State.Camera, View, ProjectedVertices ) && NMath::Abs( State.Camera.Distance - ExpectedShapeCamera.Distance ) < 1.0e-4f, "実mesh geometryの回転後頂点がAppearance最大倍率のbounds内へ投影される" );
			}
		}
		// Sphere spawn、速さ、重力、Appearance倍率の保守半径。
		FParticleEmitterDefinition MotionEmitter = BaseEmitter;
		MotionEmitter.Shape = EParticleShape::Sphere;
		MotionEmitter.ShapeRadius = 2.0f;
		MotionEmitter.Speed = { 2.0f, 2.0f };
		MotionEmitter.Gravity = FVector3( 0.0f, -3.0f, 0.0f );
		MotionEmitter.Appearance.Size.Keys.Add( { 0.0f, 3.0f } );
		// 移動範囲を持つ一時effect定義。
		FParticleEffectDefinition MotionDefinition;
		MotionDefinition.Emitters.Add( MotionEmitter );
		// 移動範囲をfitするcamera state。
		FEffectEditorState MotionState;
		MotionState.Document.Reset( MotionDefinition );
		// sphere spawnと移動を含むfocus結果。
		const bool bMotionFocused = NEffectEditorUI::FocusCameraOnEffectGeometry( MotionState, Landscape );
		// mesh外周、spawn2、速度2、重力3を加えたexpected半径。
		const float32 ExpectedMotionRadius = std::sqrt( 0.5f ) * 3.0f + 2.0f + 2.0f + 3.0f;
		// 期待半径から直接計算したcamera。
		FEditorCamera ExpectedMotionCamera;
		ExpectedMotionCamera.Focus( FVector3::Zero, ExpectedMotionRadius, Landscape );
		C.Check( bMotionFocused && NMath::Abs( MotionState.Camera.Distance - ExpectedMotionCamera.Distance ) < 1.0e-4f, "Sphere spawn・速さ・重力・Appearance倍率をfocus余裕へ加える" );
		// Boxの半対角をspawn半径へ使う。
		MotionEmitter.Shape = EParticleShape::Box;
		MotionEmitter.BoxSize = FVector3( 4.0f, 0.0f, 0.0f );
		MotionEmitter.Speed = { 0.0f, 0.0f };
		MotionEmitter.Gravity = FVector3::Zero;
		MotionDefinition.Emitters[0] = MotionEmitter;
		MotionState.Document.Reset( MotionDefinition );
		const bool bBoxFocused = NEffectEditorUI::FocusCameraOnEffectGeometry( MotionState, Landscape );
		ExpectedMotionCamera.Focus( FVector3::Zero, std::sqrt( 0.5f ) * 3.0f + 2.0f, Landscape );
		C.Check( bBoxFocused && NMath::Abs( MotionState.Camera.Distance - ExpectedMotionCamera.Distance ) < 1.0e-4f, "Box spawnの半対角をfocus余裕へ加える" );
		// 実simulationの粒子を不等stepで進め、Recordと同じmesh変換後全頂点を投影する。
		const auto SimulateAndProject = [&]( FParticleEmitterDefinition Emitter, const TArray<float32>& Steps, uint32 Seed, const char* CheckName )
		{
			// mesh粒子を一個だけ出す検査用effect。
			FParticleEffectDefinition SimulationDefinition;
			SimulationDefinition.Duration = 2.0f;
			SimulationDefinition.bLoop = false;
			Emitter.MaxParticles = 1;
			Emitter.Rate = 0.0f;
			Emitter.Bursts.Empty();
			Emitter.Bursts.Add( NEffectTest::Burst( 0.0f, 1 ) );
			SimulationDefinition.Emitters.Add( Emitter );
			// simulationが所有する定義snapshot。
			const TSharedPtr<const FParticleEffectDefinition> Snapshot = MakeShared<FParticleEffectDefinition>( SimulationDefinition );
			// 今回のshapeと移動上限を含めたfocus結果。
			FEffectEditorState SimulationState;
			SimulationState.Document.Reset( SimulationDefinition );
			const bool bSimulationFocused = NEffectEditorUI::FocusCameraOnEffectGeometry( SimulationState, Landscape );
			// 固定seedで開始した実particle simulation。
			FParticleSimulation Simulation;
			NParticle::Start( Simulation, Snapshot, Seed, 1.0f, 1.0f, 0 );
			// productionと同じmesh定義から作るimmutable geometry。
			FEffectMeshGeometry SimulatedGeometry;
			// snapshot geometryを構築できたか。
			const bool bGeometryBuilt = NEffectMesh::BuildGeometry( Emitter.Mesh, SimulatedGeometry );
			// 複数stepの全時点でcamera内へ収まった粒子を見たか。
			bool bEveryStepProjected = true;
			// 寿命途中の正のageを持つparticleを確認したか。
			bool bSawAliveParticle = false;
			for ( const float32 Step : Steps )
			{
				NParticle::Advance( Simulation, Step );
				// simulationから現在時点のdraw particleを取得する。
				TArray<NParticle::FDrawParticle> DrawParticles;
				NParticle::CollectDrawParticles( Simulation, DrawParticles );
				if ( DrawParticles.Num() != 1 ) { bEveryStepProjected = false; continue; }
				// 寿命途中で実際に動いたparticleを確認する。
				bSawAliveParticle |= DrawParticles[0].Age > 0.0f && DrawParticles[0].Age < Emitter.Lifetime.Max;
				// この時点の粒子変換後mesh全頂点がcamera内にあることを確認する。
				TArray<FVector3> WorldVertices;
				// 実粒子へ適用する固定形状のEuler回転。
				const FVector3 MeshRadians = Emitter.Mesh.RotationDegrees * NMath::DEG2RAD;
				// mesh定義に保存した固定回転。
				const FQuaternion MeshRotation = FQuaternion::EulerToQuat( MeshRadians );
				// 粒子のspin・mesh固定回転・effect回転をproduction順で合成する。
				const FQuaternion ParticleRotation = Simulation.Transform.Rotation * MeshRotation * FQuaternion::FromAxisAngle( FVector3::Forward, DrawParticles[0].Angle );
				// geometryの各頂点を粒子の位置・寸法・回転へ変換する。
				for ( const FMeshVertex& Vertex : SimulatedGeometry.Vertices )
				{
					// 実DrawParticle.Sizeを掛けたmesh位置。
					const FVector3 LocalPosition( Vertex.Position[0] * DrawParticles[0].Size, Vertex.Position[1] * DrawParticles[0].Size, Vertex.Position[2] * DrawParticles[0].Size );
					WorldVertices.Add( DrawParticles[0].Position + ParticleRotation.Rotate( LocalPosition ) );
				}
				bEveryStepProjected &= ProjectsInside( SimulationState.Camera, Landscape, WorldVertices );
			}
			C.Check( bSimulationFocused && bGeometryBuilt && SimulatedGeometry.Vertices.Num() == 4 && bSawAliveParticle && bEveryStepProjected, CheckName );
		};
		// Sphereの実random spawn、最大speed、重力、Appearance sizeを進める不等step列。
		TArray<float32> SphereSteps;
		SphereSteps.Add( 0.07f );
		SphereSteps.Add( 0.23f );
		SphereSteps.Add( 0.31f );
		SphereSteps.Add( 0.40f );
		MotionEmitter.Shape = EParticleShape::Sphere;
		MotionEmitter.ShapeRadius = 2.0f;
		MotionEmitter.Speed = { 2.0f, 2.0f };
		MotionEmitter.Gravity = FVector3( 0.0f, -3.0f, 0.0f );
		MotionEmitter.Lifetime = { 1.25f, 1.25f };
		MotionEmitter.Mesh.RotationDegrees = FVector3( 13.0f, -9.0f, 31.0f );
		MotionEmitter.StartAngle = { 22.0f, 22.0f };
		MotionEmitter.Spin = { 43.0f, 43.0f };
		SimulateAndProject( MotionEmitter, SphereSteps, 7331u, "seed固定Sphere粒子を不等stepで進めた全頂点が最大sizeと移動余裕内" );
		// Boxの実random spawnに重力支配の移動を加える不等step列。
		TArray<float32> GravitySteps;
		GravitySteps.Add( 0.03f );
		GravitySteps.Add( 0.11f );
		GravitySteps.Add( 0.19f );
		GravitySteps.Add( 0.27f );
		GravitySteps.Add( 0.30f );
		FParticleEmitterDefinition GravityEmitter = MotionEmitter;
		GravityEmitter.Shape = EParticleShape::Box;
		GravityEmitter.BoxSize = FVector3( 4.0f, 2.0f, 1.0f );
		GravityEmitter.Speed = { 0.0f, 0.0f };
		GravityEmitter.Gravity = FVector3( 0.0f, -40.0f, 0.0f );
		GravityEmitter.Lifetime = { 1.0f, 1.0f };
		GravityEmitter.ShapeRadius = 0.0f;
		MotionDefinition.Emitters[0] = GravityEmitter;
		MotionState.Document.Reset( MotionDefinition );
		// gravity支配caseの全移動範囲へcameraを合わせたか。
		const bool bGravityFocused = NEffectEditorUI::FocusCameraOnEffectGeometry( MotionState, Landscape );
		C.Check( bGravityFocused, "gravity支配caseのfocusを適用する" );
		SimulateAndProject( GravityEmitter, GravitySteps, 7332u, "seed固定Box粒子の重力支配移動と全頂点がcamera bounds内" );
		// Treeの完成形とmesh geometryを、それぞれ独立に含むboundsへfocusする。
		FParticleEmitterDefinition TreeEmitter = NEffectTest::QuietEmitter();
		TreeEmitter.Path.Kind = FEffectPathDefinition::EKind::TreeLightning;
		TreeEmitter.Path.End = FVector3( 0.0f, 12.0f, 0.0f );
		TreeEmitter.Path.Jitter = 0.1f;
		TreeEmitter.Path.Branches = 0;
		TreeEmitter.Path.Tree.Depth = 0;
		TreeEmitter.Path.Tree.GrowthSeconds = 0.0f;
		TreeEmitter.Path.Tree.BranchDelaySeconds = 0.0f;
		TreeEmitter.Path.Tree.StrokeCount = 1;
		TreeEmitter.Path.Tree.StrokeInterval = 0.8f;
		TreeEmitter.Path.Tree.StrokeSeconds = 0.5f;
		TreeEmitter.Path.RefreshHz = 0.0f;
		TreeEmitter.Path.Segments = 12;
		TreeEmitter.StartSize = { 0.1f, 0.1f };
		TreeEmitter.EndSize = { 0.1f, 0.1f };
		TreeEmitter.Appearance.Size.Keys.Add( { 0.0f, 1.0f } );
		// mesh planeを十分大きくし、treeとmeshのbounds球を互いに包含させない。
		FParticleEmitterDefinition MixedMeshEmitter = BaseEmitter;
		MixedMeshEmitter.StartSize = { 16.0f, 16.0f };
		MixedMeshEmitter.EndSize = { 16.0f, 16.0f };
		// mesh外半径とTree bounds球をそれぞれ求める。
		FVector3 TreeBoundsCenter = FVector3::Zero;
		// Tree geometryのhalo込み保守半径。
		float32 TreeBoundsRadius = 0.0f;
		// Tree point周囲へ投影するhalo幅。
		const float32 TreeHalo = 0.15f;
		// Tree definitionからsphere boundsを計算できたか。
		const bool bTreeBoundsBuilt = NEffectPath::GetTreeLightningBounds( TreeEmitter.Path, FVector3::Zero, TreeEmitter.Path.End, TreeHalo, TreeBoundsCenter, TreeBoundsRadius );
		// Plane cornerから得るmesh bounds半径。
		const float32 MeshBoundsRadius = std::sqrt( 0.5f ) * 16.0f;
		// mesh原点とTree bounds中心間の距離。
		const float32 BoundsCenterDistance = ( TreeBoundsCenter - FVector3::Zero ).Length();
		// どちらのbounds sphereも他方を完全には含まないこと。
		const bool bBoundsDoNotContain = bTreeBoundsBuilt && BoundsCenterDistance > NMath::Abs( TreeBoundsRadius - MeshBoundsRadius ) && BoundsCenterDistance < TreeBoundsRadius + MeshBoundsRadius;
		// Treeとmeshを同時に持つ検査用文書。
		FParticleEffectDefinition MixedDefinition;
		MixedDefinition.Emitters.Add( TreeEmitter );
		MixedDefinition.Emitters.Add( MixedMeshEmitter );
		// 混在boundsを投影するcamera state。
		FEffectEditorState MixedState;
		MixedState.Document.Reset( MixedDefinition );
		MixedState.Camera.Yaw = 0.0f;
		MixedState.Camera.Pitch = 0.0f;
		// 両種別を含む全体へfocusできたか。
		const bool bMixedFocused = NEffectEditorUI::FocusCameraOnEffectGeometry( MixedState, Landscape );
		// 実plane geometryの頂点を含む点列。
		FEffectMeshGeometry MixedMeshGeometry;
		// mixed fixtureの実Plane geometryができたか。
		const bool bMixedMeshBuilt = NEffectMesh::BuildGeometry( MixedMeshEmitter.Mesh, MixedMeshGeometry );
		// mesh camera fitへ渡す全plane corner。
		TArray<FVector3> MixedGeometryPoints;
		if ( bMixedMeshBuilt )
		{
			for ( const FMeshVertex& Vertex : MixedMeshGeometry.Vertices ) MixedGeometryPoints.Add( FVector3( Vertex.Position[0] * 16.0f, Vertex.Position[1] * 16.0f, Vertex.Position[2] * 16.0f ) );
		}
		// 完成Tree geometryとhalo外縁を含む点列。
		FEffectPathGeometry MixedTreeGeometry;
		// 実Treeのboundsを再取得できたか。
		const bool bMixedTreeBuilt = NEffectPath::GetTreeLightningBounds( TreeEmitter.Path, FVector3::Zero, TreeEmitter.Path.End, TreeHalo, TreeBoundsCenter, TreeBoundsRadius );
		if ( bMixedTreeBuilt ) NEffectPath::BuildTreeLightning( TreeEmitter.Path, 31337u, 0.0f, FVector3::Zero, TreeEmitter.Path.End, TreeEmitter.StartSize.Max, FColor32( 255, 255, 255, 255 ), MixedTreeGeometry );
		// Tree中心線とhalo外周の投影確認点。
		TArray<FVector3> MixedTreePoints;
		for ( const FEffectPathGeometry::FStrip& Strip : MixedTreeGeometry.Strips )
		{
			for ( const FEffectPathGeometry::FPoint& Point : Strip.Points )
			{
				MixedTreePoints.Add( Point.Position );
				MixedTreePoints.Add( Point.Position + FVector3( TreeHalo, 0.0f, 0.0f ) );
				MixedTreePoints.Add( Point.Position - FVector3( TreeHalo, 0.0f, 0.0f ) );
				MixedTreePoints.Add( Point.Position + FVector3( 0.0f, TreeHalo, 0.0f ) );
				MixedTreePoints.Add( Point.Position - FVector3( 0.0f, TreeHalo, 0.0f ) );
				MixedTreePoints.Add( Point.Position + FVector3( 0.0f, 0.0f, TreeHalo ) );
				MixedTreePoints.Add( Point.Position - FVector3( 0.0f, 0.0f, TreeHalo ) );
			}
		}
		C.Check( bBoundsDoNotContain && bMixedMeshBuilt && MixedMeshGeometry.Vertices.Num() == 4 && bMixedTreeBuilt && MixedTreeGeometry.Strips.Num() == 1 && MixedTreeGeometry.Strips[0].Points.Num() == 13 && MixedGeometryPoints.Num() == 4 && MixedTreePoints.Num() == 91, "混在camera fixtureに包含関係のないboundsと全geometry点を用意する" );
		C.Check( bMixedFocused && ProjectsInside( MixedState.Camera, Landscape, MixedGeometryPoints ) && ProjectsInside( MixedState.Camera, Landscape, MixedTreePoints ), "混在Tree全点とPlane全cornerおよびhalo外縁をcameraへ収める" );
		// 不正・空定義では既存camera全fieldを保つ。
		FEffectEditorState InvalidState;
		InvalidState.Camera.Target = FVector3( 4.0f, 5.0f, 6.0f );
		InvalidState.Camera.Yaw = 1.2f;
		InvalidState.Camera.Pitch = 0.7f;
		InvalidState.Camera.Distance = 8.0f;
		const FEditorCamera PreviousCamera = InvalidState.Camera;
		// 失敗条件を入れ替えて使う一時定義。
		FParticleEffectDefinition InvalidDefinition;
		// 定義を壊さず、一つずつ不正値を差し込むemitter。
		FParticleEmitterDefinition InvalidEmitter = BaseEmitter;
		InvalidEmitter.Mesh.Segments = 129;
		InvalidDefinition.Emitters.Add( InvalidEmitter );
		InvalidState.Document.Reset( InvalidDefinition );
		C.Check( !NEffectEditorUI::FocusCameraOnEffectGeometry( InvalidState, Landscape ) && CameraMatches( InvalidState.Camera, PreviousCamera ), "不正mesh設定ではcamera全fieldを変更しない" );
		InvalidEmitter = BaseEmitter;
		InvalidEmitter.Path.Kind = FEffectPathDefinition::EKind::TreeLightning;
		InvalidDefinition.Emitters[0] = InvalidEmitter;
		InvalidState.Document.Reset( InvalidDefinition );
		C.Check( !NEffectEditorUI::FocusCameraOnEffectGeometry( InvalidState, Landscape ) && CameraMatches( InvalidState.Camera, PreviousCamera ), "Tree Path競合meshはTreeへfallbackせずcameraを保つ" );
		InvalidEmitter = BaseEmitter;
		InvalidEmitter.Gravity.x = TNumericLimits<float32>::Infinity();
		InvalidDefinition.Emitters[0] = InvalidEmitter;
		InvalidState.Document.Reset( InvalidDefinition );
		C.Check( !NEffectEditorUI::FocusCameraOnEffectGeometry( InvalidState, Landscape ) && CameraMatches( InvalidState.Camera, PreviousCamera ), "非有限移動値ではcamera全fieldを変更しない" );
		InvalidEmitter = BaseEmitter;
		InvalidEmitter.StartSize.Max = TNumericLimits<float32>::Infinity();
		InvalidDefinition.Emitters[0] = InvalidEmitter;
		InvalidState.Document.Reset( InvalidDefinition );
		C.Check( !NEffectEditorUI::FocusCameraOnEffectGeometry( InvalidState, Landscape ) && CameraMatches( InvalidState.Camera, PreviousCamera ), "非有限sizeではcamera全fieldを変更しない" );
		InvalidEmitter = BaseEmitter;
		InvalidEmitter.Appearance.Size.Keys.Add( { 0.0f, TNumericLimits<float32>::Infinity() } );
		InvalidDefinition.Emitters[0] = InvalidEmitter;
		InvalidState.Document.Reset( InvalidDefinition );
		C.Check( !NEffectEditorUI::FocusCameraOnEffectGeometry( InvalidState, Landscape ) && CameraMatches( InvalidState.Camera, PreviousCamera ), "非有限Appearance倍率ではcamera全fieldを変更しない" );
		InvalidEmitter = BaseEmitter;
		InvalidEmitter.Shape = static_cast<EParticleShape>( 255 );
		InvalidDefinition.Emitters[0] = InvalidEmitter;
		InvalidState.Document.Reset( InvalidDefinition );
		C.Check( !NEffectEditorUI::FocusCameraOnEffectGeometry( InvalidState, Landscape ) && CameraMatches( InvalidState.Camera, PreviousCamera ), "未知shapeではcamera全fieldを変更しない" );
		InvalidEmitter = BaseEmitter;
		InvalidEmitter.Lifetime.Max = 0.0f;
		InvalidDefinition.Emitters[0] = InvalidEmitter;
		InvalidState.Document.Reset( InvalidDefinition );
		C.Check( !NEffectEditorUI::FocusCameraOnEffectGeometry( InvalidState, Landscape ) && CameraMatches( InvalidState.Camera, PreviousCamera ), "不正lifetimeではcamera全fieldを変更しない" );
		InvalidEmitter = BaseEmitter;
		InvalidEmitter.Speed.Min = -1.0f;
		InvalidDefinition.Emitters[0] = InvalidEmitter;
		InvalidState.Document.Reset( InvalidDefinition );
		C.Check( !NEffectEditorUI::FocusCameraOnEffectGeometry( InvalidState, Landscape ) && CameraMatches( InvalidState.Camera, PreviousCamera ), "負のspeedではcamera全fieldを変更しない" );
		InvalidEmitter = BaseEmitter;
		InvalidEmitter.Drag = -1.0f;
		InvalidDefinition.Emitters[0] = InvalidEmitter;
		InvalidState.Document.Reset( InvalidDefinition );
		C.Check( !NEffectEditorUI::FocusCameraOnEffectGeometry( InvalidState, Landscape ) && CameraMatches( InvalidState.Camera, PreviousCamera ), "負のdragではcamera全fieldを変更しない" );
		InvalidEmitter = BaseEmitter;
		InvalidEmitter.Flipbook.bEnabled = true;
		InvalidDefinition.Emitters[0] = InvalidEmitter;
		InvalidState.Document.Reset( InvalidDefinition );
		C.Check( !NEffectEditorUI::FocusCameraOnEffectGeometry( InvalidState, Landscape ) && CameraMatches( InvalidState.Camera, PreviousCamera ), "Flipbook競合meshではcamera全fieldを変更しない" );
		InvalidEmitter = BaseEmitter;
		InvalidEmitter.StartSize = { 1.0e20f, 1.0e20f };
		InvalidDefinition.Emitters[0] = InvalidEmitter;
		InvalidState.Document.Reset( InvalidDefinition );
		C.Check( !NEffectEditorUI::FocusCameraOnEffectGeometry( InvalidState, Landscape ) && CameraMatches( InvalidState.Camera, PreviousCamera ), "camera上限を超える有限boundsではcamera全fieldを変更しない" );
		InvalidDefinition.Emitters.Empty();
		InvalidState.Document.Reset( InvalidDefinition );
		C.Check( !NEffectEditorUI::FocusCameraOnEffectGeometry( InvalidState, Landscape ) && CameraMatches( InvalidState.Camera, PreviousCamera ), "meshなしではcameraを変更しない" );
		// 入力検証の失敗を起こすviewport。
		FEditorViewport InvalidViewport;
		InvalidViewport.Width = 1;
		InvalidState.Document.Reset( MotionDefinition );
		C.Check( !NEffectEditorUI::FocusCameraOnEffectGeometry( InvalidState, InvalidViewport ) && CameraMatches( InvalidState.Camera, PreviousCamera ), "invalid viewportではcamera全fieldを変更しない" );
		InvalidViewport.Width = Landscape.Width;
		InvalidViewport.Height = Landscape.Height;
		InvalidViewport.Far = InvalidViewport.Near;
		C.Check( !NEffectEditorUI::FocusCameraOnEffectGeometry( InvalidState, InvalidViewport ) && CameraMatches( InvalidState.Camera, PreviousCamera ), "near/far不整合ではcamera全fieldを変更しない" );
		InvalidViewport.Far = Landscape.Far;
		InvalidViewport.FovY = 0.0f;
		C.Check( !NEffectEditorUI::FocusCameraOnEffectGeometry( InvalidState, InvalidViewport ) && CameraMatches( InvalidState.Camera, PreviousCamera ), "不正FOVではcamera全fieldを変更しない" );
		// Reset共通処理はTreeとmeshのpendingを設定し、空文書なら両方を消す。
		// Resetと共有するpending設定対象。
		FEffectEditorState ResetState;
		NEffectEditorUI::SetPendingEffectCameraFocus( ResetState, MixedDefinition );
		// Treeとmeshの両flagが立ったこと。
		const bool bResetPendingSet = ResetState.bTreeCameraFocusPending && ResetState.bEffectGeometryCameraFocusPending;
		// pendingを全て落とす空文書。
		FParticleEffectDefinition EmptyDefinition;
		NEffectEditorUI::SetPendingEffectCameraFocus( ResetState, EmptyDefinition );
		C.Check( bResetPendingSet && !ResetState.bTreeCameraFocusPending && !ResetState.bEffectGeometryCameraFocusPending, "camera reset共通設定がTree・meshを拾い空文書で消す" );
		// pendingは一度だけ消費し、後続の手動panを保つ。
		FEffectEditorState PendingState;
		PendingState.Document.Reset( MotionDefinition );
		PendingState.bEffectGeometryCameraFocusPending = true;
		C.Check( NEffectEditorUI::ApplyPendingEffectCameraFocus( PendingState, Landscape ) && !PendingState.bEffectGeometryCameraFocusPending, "mesh focus pendingを一度だけ適用して消費する" );
		PendingState.Camera.Pan( Landscape, 9.0f, -5.0f );
		// pending消費後にユーザーが動かしたcamera状態。
		const FEditorCamera ManuallyMoved = PendingState.Camera;
		C.Check( !NEffectEditorUI::ApplyPendingEffectCameraFocus( PendingState, Landscape ) && CameraMatches( PendingState.Camera, ManuallyMoved ), "pending消費後は手動panを次frameで上書きしない" );
		// Tree専用APIはmesh-only定義をfocusしない。
		// Tree専用APIへmesh-onlyを渡す別state。
		FEffectEditorState TreeApiState;
		TreeApiState.Document.Reset( MotionDefinition );
		TreeApiState.Camera = PreviousCamera;
		C.Check( !NEffectEditorUI::FocusCameraOnTree( TreeApiState, Landscape ) && CameraMatches( TreeApiState.Camera, PreviousCamera ), "Tree専用focus APIはmeshだけを処理しない" );
		// Appearanceが全時刻で0なら旧Tree focusとgeneric focusの双方がcameraを保つ。
		FParticleEmitterDefinition ZeroWidthTree = TreeEmitter;
		ZeroWidthTree.Appearance.Size.Keys.Empty();
		ZeroWidthTree.Appearance.Size.Keys.Add( { 0.0f, 0.0f } );
		ZeroWidthTree.Appearance.Size.Keys.Add( { 1.0f, 0.0f } );
		FParticleEffectDefinition ZeroWidthDefinition;
		ZeroWidthDefinition.Emitters.Add( ZeroWidthTree );
		FEffectEditorState ZeroWidthState;
		ZeroWidthState.Document.Reset( ZeroWidthDefinition );
		ZeroWidthState.Camera = PreviousCamera;
		C.Check( !NEffectEditorUI::FocusCameraOnTree( ZeroWidthState, Landscape ) && CameraMatches( ZeroWidthState.Camera, PreviousCamera ), "Tree専用focusはzero幅Treeで旧どおりcameraを保つ" );
		C.Check( !NEffectEditorUI::FocusCameraOnEffectGeometry( ZeroWidthState, Landscape ) && CameraMatches( ZeroWidthState.Camera, PreviousCamera ), "generic focusもzero幅Treeではcameraを保つ" );
	}

	// preset copyを別名保存して、配置用Effect componentから再参照できることを確認する。
	void TestMeshPresetSaveAndPlacement( NAssetTest::FChecker& C, CAssetManager& Assets )
	{
		C.Group = "effect-mesh-preset-save-placement";
		// sandbox内で原本とcopyに使う相対effect path。
		const FString PresetPath = "Effects/selftest_mesh_preset.effect.json";
		const FString CopyPath = "Effects/selftest_mesh_copy.effect.json";
		const bool bPresetUnused = !Assets.ResolvePath( PresetPath ).IsSet();
		const bool bCopyUnused = !Assets.ResolvePath( CopyPath ).IsSet();
		C.Check( bPresetUnused && bCopyUnused, "sandbox内のpreset/copy保存先が未使用である" );
		if ( !bPresetUnused || !bCopyUnused ) return;
		// この関数が新規作成するfixtureだけをcleanup対象にする。
		FMeshFixtureCleanup Cleanup{ Assets, PresetPath, CopyPath };
		// 本物のpreset loaderに渡すmesh定義。
		FParticleEffectDefinition Preset;
		Preset.Name = "Mesh selftest preset";
		FParticleEmitterDefinition Emitter;
		Emitter.Name = "Energy ring";
		Emitter.Rate = 0.0f;
		Emitter.Bursts.Add( NEffectTest::Burst( 0.0f, 1 ) );
		Emitter.Lifetime = { 2.0f, 2.0f };
		Emitter.Mesh.Kind = FEffectMeshDefinition::EKind::Ring;
		Emitter.Mesh.Segments = 24;
		Emitter.Mesh.InnerRadius = 0.6f;
		Preset.Emitters.Add( Emitter );
		C.Check( !FParticleEffectDefinition::HasErrors( Preset.Validate() ) && Assets.SaveEffect( PresetPath, Preset ), "sandbox内に有効なmesh preset fixtureを書く" );
		const TOptional<FString> PresetFullPath = Assets.ResolvePath( PresetPath );
		// 原本JSONを保存前比較用に読み取る。
		NJson::CJsonObject BeforeJson;
		const bool bReadBefore = PresetFullPath.IsSet() && BeforeJson.LoadFromFile( PresetFullPath.GetValue() );
		const NlohmannJson Before = bReadBefore ? BeforeJson.GetRoot() : NlohmannJson();
		C.Check( bReadBefore, "mesh preset fixtureを読める" );
		if ( !bReadBefore ) return;
		// production preset APIから未保存copyを作る。
		FEffectEditorState State;
		const bool bCreated = NDebugUI::NewEffectFromPreset( State, PresetPath );
		C.Check( bCreated && State.FilePath.IsEmpty() && State.Document.IsDirty() && State.Document.Get().ComputeHash() == Preset.ComputeHash() && State.bEffectGeometryCameraFocusPending && !State.bTreeCameraFocusPending, "Openを通るmesh preset copyがfocus pendingを持つ" );
		if ( !bCreated ) { NDebugUI::ReleaseEffectEditor( State ); return; }
		// 実viewportでpendingを一度消費した後のcamera操作を保つ。
		FEditorViewport PresetViewport;
		// presetの一回限りfocusが成功したか。
		const bool bPresetFocused = NEffectEditorUI::ApplyPendingEffectCameraFocus( State, PresetViewport );
		State.Camera.Pan( PresetViewport, 4.0f, 3.0f );
		// ユーザー操作後に保持すべき全camera値。
		const FEditorCamera PresetManuallyMoved = State.Camera;
		// 再適用されずpan後のcameraを保てるか。
		const bool bPresetManualCameraPreserved = !NEffectEditorUI::ApplyPendingEffectCameraFocus( State, PresetViewport ) && State.Camera.Target.x == PresetManuallyMoved.Target.x && State.Camera.Target.y == PresetManuallyMoved.Target.y && State.Camera.Target.z == PresetManuallyMoved.Target.z && State.Camera.Yaw == PresetManuallyMoved.Yaw && State.Camera.Pitch == PresetManuallyMoved.Pitch && State.Camera.Distance == PresetManuallyMoved.Distance;
		C.Check( bPresetFocused && !State.bEffectGeometryCameraFocusPending && bPresetManualCameraPreserved, "presetのfocusは一回だけで後続のmanual panを保つ" );
		const bool bSaved = NDebugUI::SaveEffectFile( State, CopyPath );
		C.Check( bSaved && State.FilePath == CopyPath && !State.Document.IsDirty() && State.SaveSerial == 1, "mesh copyをSaveAsしFilePath・dirty・SaveSerialを更新する" );
		if ( !bSaved ) { NDebugUI::ReleaseEffectEditor( State ); return; }
		// editor previewが保持する資源をasset検証より先に解放する。
		NDebugUI::ReleaseEffectEditor( State );
		// 保存したassetをfresh JSON parserで読み直す。
		const TOptional<FString> CopyFullPath = Assets.ResolvePath( CopyPath );
		NJson::CJsonObject CopyJson;
		const bool bReadCopy = CopyFullPath.IsSet() && CopyJson.LoadFromFile( CopyFullPath.GetValue() );
		FParticleEffectDefinition Fresh;
		const bool bParsedCopy = bReadCopy && FParticleEffectDefinition::FromJson( CopyJson.GetRoot(), Fresh );
		// emitter参照前にfresh parserの要素数を確かめる。
		const bool bFreshMeshMatches = bParsedCopy && Fresh.Emitters.IsValidIndex( 0 ) && Fresh.ComputeHash() == Preset.ComputeHash() && Fresh.Emitters[0].Mesh.ComputeHash() == Preset.Emitters[0].Mesh.ComputeHash();
		C.Check( bFreshMeshMatches, "SaveAs JSONをfresh definitionへ読むとmesh値とhashが一致する" );
		// 保存済みmesh effectを配置部品のPathへ結び付ける。
		FWorldDefinition World;
		World.Name = "Mesh effect placement test";
		FWorldObject Object;
		Object.Id = World.AllocateId();
		Object.Name = "Placed mesh effect";
		FWorldEffect Component;
		Component.Path = CopyPath;
		Object.Effect = Component;
		World.Objects.Add( Object );
		FWorldDefinition WorldRoundTrip;
		const bool bWorldParsed = FWorldDefinition::FromJson( World.ToJson(), WorldRoundTrip );
		// 配置先に保存したassetを読み込み、実際の再生からmesh commandを得る。
		const TSharedPtr<CEffectAsset> CopyAsset = Assets.LoadEffect( CopyPath );
		const bool bWorldPlacementMatches = bWorldParsed && WorldRoundTrip.Objects.Num() == 1 && WorldRoundTrip.Objects[0].Effect.IsSet() && WorldRoundTrip.Objects[0].Effect->Path == CopyPath;
		const TSharedPtr<IEffectPlayback> CopyPlayback = CopyAsset.IsValid() ? NEffect::CreateParticlePlayback( CopyAsset->GetDefinitionShared(), &Assets, FEffectPlayParams(), FTransform::Identity, CopyPath ) : TSharedPtr<IEffectPlayback>();
		if ( CopyPlayback.IsValid() ) CopyPlayback->Update( 0.01f );
		FDrawBuffer CopyBuffer;
		FDrawContext CopyContext( CopyBuffer );
		if ( CopyPlayback.IsValid() ) CopyPlayback->Record( CopyContext );
		const TArray<FRecordedMesh> CopyMeshes = ReadMaterialMeshes( CopyBuffer );
		C.Check( bWorldPlacementMatches && CopyAsset.IsValid() && CopyPlayback.IsValid() && CopyMeshes.Num() == 1, "World配置先のSaveAs assetを実再生してmesh命令を記録する" );
		// SaveAs操作は元preset JSONを書き換えない。
		NJson::CJsonObject AfterJson;
		C.Check( AfterJson.LoadFromFile( PresetFullPath.GetValue() ) && AfterJson.GetRoot() == Before, "mesh copy保存後もpreset原本JSONが不変" );
		// 通常終了時もfixtureを消せたことを結果に含める。
		C.Check( Cleanup.Clean(), "mesh presetとSaveAs fixtureを削除する" );
		// New操作では過去のmesh focus要求を残さない。
		FEffectEditorState NewState;
		NDebugUI::NewEffect( NewState );
		C.Check( !NewState.bTreeCameraFocusPending && !NewState.bEffectGeometryCameraFocusPending, "NewはTreeとmeshのpending focusを消す" );
		NDebugUI::ReleaseEffectEditor( NewState );
	}
}

// 定義・geometry・Playback・Editor履歴・保存後配置を一つのmesh sliceとして確認する。
void NWorldTest::RunEffectMeshSelfTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir )
{
	(void)Dir;
	TestMeshDefinitionAndGeometry( C );
	TestMeshPlayback( C, Assets );
	TestMeshCameraFocus( C );
	// 試験前のImGui contextを復帰確認に使う。
	ImGuiContext* Previous = ImGui::GetCurrentContext();
	{
		// editor panel入力だけに使う独立context。
		FMeshInputContext Input;
		C.Group = "effect-mesh-panel-context";
		C.Check( Input.IsReady(), "Mesh input test font atlasを構築する" );
		if ( Input.IsReady() )
		{
			TestMeshPanelHistory( C, Input );
			C.Check( Input.IsBalanced(), "Mesh panelのImGui frame stackを閉じる" );
		}
	}
	C.Check( ImGui::GetCurrentContext() == Previous, "mesh入力後に元ImGui contextへ戻す" );
	TestMeshPresetSaveAndPlacement( C, Assets );
}
