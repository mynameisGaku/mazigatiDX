// SPDX-License-Identifier: NOASSERTION
#include "WorldMeshMaterialSelfTestSupport.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "Library/Core/Interfaces/Render/DrawMaterialMesh.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Render/MeshRender.h"
#include "Library/Framework/World/Mesh/EditMeshPrimitives.h"
#include "Library/Framework/World/WorldRender.h"
#include "Library/Platform/FileSystem.h"

namespace
{
	// 左右上下で色が違う、繰返し可能な16x16 TGAを作る。
	std::vector<uint8> MakeMeshMaterialColorTga( bool bNoise = false, bool bMask = false )
	{
		constexpr int32 Width = 16;
		constexpr int32 Height = 16;
		std::vector<uint8> Bytes = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, Width, 0, Height, 0, 32, 0x28 };
		for ( int32 Y = 0; Y < Height; ++Y )
		{
			for ( int32 X = 0; X < Width; ++X )
			{
				uint8 R = 0, G = 0, B = 0;
				if ( bMask ) R = G = B = X < Width / 2 ? 0 : 255;
				else if ( bNoise ) R = G = B = ( ( X / 2 + Y / 2 ) % 2 == 0 ) ? 32 : 224;
				else if ( Y < Height / 2 ) { R = X < Width / 2 ? 120 : 30; G = X < Width / 2 ? 35 : 65; B = X < Width / 2 ? 25 : 120; }
				else { R = X < Width / 2 ? 25 : 110; G = X < Width / 2 ? 105 : 85; B = X < Width / 2 ? 95 : 30; }
				Bytes.push_back( B ); Bytes.push_back( G ); Bytes.push_back( R ); Bytes.push_back( 255 );
			}
		}
		return Bytes;
	}

	// 再生側と同じEditMesh経路で、手前向きのUV面を作る。
	FEditMesh MakeRealMeshMaterialFixture( const FString& SourcePath, const FString& MaskPath, const FString& NoisePath )
	{
		FEditMesh Mesh;
		Mesh.Vertices.Add( FVector3( -2.0f, -2.0f, 0.0f ) );
		Mesh.Vertices.Add( FVector3( 2.0f, -2.0f, 0.0f ) );
		Mesh.Vertices.Add( FVector3( 2.0f, 2.0f, 0.0f ) );
		Mesh.Vertices.Add( FVector3( -2.0f, 2.0f, 0.0f ) );
		FEditMeshFace Face;
		Face.Indices.Add( 0 ); Face.Indices.Add( 1 ); Face.Indices.Add( 2 ); Face.Indices.Add( 3 );
		Mesh.Faces.Add( Face );
		FEditMeshMaterial& Material = Mesh.Materials.Add();
		Material.Texture = SourcePath;
		Material.Tile = 4.0f;
		Material.bTwoSided = true;
		Material.Effect.Material.bEnabled = true;
		Material.Effect.Material.bDissolve = true;
		Material.Effect.Material.MaskTexture = MaskPath;
		Material.Effect.Material.Dissolve.Interpolation = FEffectScalarCurve::EInterpolation::Linear;
		Material.Effect.Material.Dissolve.Keys.Add( { 0.0f, 0.0f } );
		Material.Effect.Material.Dissolve.Keys.Add( { 1.0f, 1.0f } );
		Material.Effect.Material.Emission.Interpolation = FEffectScalarCurve::EInterpolation::Linear;
		Material.Effect.Material.Emission.Keys.Add( { 0.0f, 1.0f } );
		Material.Effect.Material.Emission.Keys.Add( { 1.0f, 3.0f } );
		Material.Effect.Material.Distortion.bEnabled = true;
		Material.Effect.Material.Distortion.Texture = NoisePath;
		Material.Effect.Material.Distortion.Strength.Keys.Add( { 0.0f, 1.0f } );
		Material.Effect.Material.Distortion.UVScroll = FVector2( 0.7f, -0.4f );
		Material.Effect.Duration = 1.0f;
		Material.Effect.Opacity = 1.0f;
		return Mesh;
	}

	// 現在のSoftImageから指定画素をRGBA値で読む。
	FColor32 ReadMeshMaterialPixel( int32 Soft, int32 X, int32 Y )
	{
		int32 R = 0, G = 0, B = 0, A = 0;
		GetPixelSoftImage( Soft, X, Y, &R, &G, &B, &A );
		return FColor32( static_cast<uint8>( R ), static_cast<uint8>( G ), static_cast<uint8>( B ), static_cast<uint8>( A ) );
	}

	// rendererの状態を実機へ記録し、画面全体をSoftImageへ読む。
	bool RenderMeshMaterialRenderer( FMeshRenderer& Renderer, IDrawExecutor& Executor, int32 Screen, int32 Soft, EDrawBlend InitialBlend = EDrawBlend::Alpha, int32 BlendParam = 255 )
	{
		SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		Draw.Blend( InitialBlend, BlendParam );
		Renderer.Record( Draw, FTransform::Identity );
		if ( Buffer.Words.empty() ) return false;
		Executor.Execute( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
		RenderVertex();
		return GetDrawScreenSoftImage( 0, 0, 128, 128, Soft ) == 0;
	}

	// 二画像の領域でRGB差がある画素数を数える。
	int32 CountMeshMaterialPixelDifferences( int32 Soft, const TArray<FColor32>& Previous )
	{
		int32 Different = 0;
		for ( int32 Y = 32; Y < 96; ++Y )
		{
			for ( int32 X = 32; X < 96; ++X )
			{
				const FColor32 Current = ReadMeshMaterialPixel( Soft, X, Y );
				const FColor32& Before = Previous[( Y - 32 ) * 64 + X - 32];
				if ( Current.r != Before.r || Current.g != Before.g || Current.b != Before.b ) ++Different;
			}
		}
		return Different;
	}

	// 固定領域のRGB画素を比較可能な配列へ複写する。
	TArray<FColor32> CaptureMeshMaterialPixels( int32 Soft )
	{
		TArray<FColor32> Pixels;
		Pixels.Reserve( 64 * 64 );
		for ( int32 Y = 32; Y < 96; ++Y ) for ( int32 X = 32; X < 96; ++X ) Pixels.Add( ReadMeshMaterialPixel( Soft, X, Y ) );
		return Pixels;
	}
}

// 実DxLib rendererの固定時刻でmesh材質の画素・transform・状態復元を確認する。
void NWorldTest::RunMeshMaterialRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Dir )
{
	C.Group = "world-mesh-material-effect-real";
	C.Check( Executor.SupportsEffectMaterialMesh(), "実描画先が材質付きmeshをサポートする" );
	if ( !Executor.SupportsEffectMaterialMesh() ) { C.Skip( "現在の描画先はmesh material effectに非対応" ); return; }
	const FString PreviousRoot = Assets.GetRootDirectory();
	Assets.SetRootDirectory( Dir );
	const FString FixtureDirectory = "mesh-material-real";
	const FString SourcePath = FixtureDirectory / "source.tga";
	const FString MaskPath = FixtureDirectory / "mask.tga";
	const FString NoisePath = FixtureDirectory / "noise.tga";
	const bool bFixturesSaved = NFileSystem::EnsureDirectory( Dir / FixtureDirectory ) && NAssetTest::WriteFileBytes( Dir / SourcePath, MakeMeshMaterialColorTga() ) && NAssetTest::WriteFileBytes( Dir / MaskPath, MakeMeshMaterialColorTga( false, true ) ) && NAssetTest::WriteFileBytes( Dir / NoisePath, MakeMeshMaterialColorTga( true ) );
	C.Check( bFixturesSaved, "非対称source・mask・noise画像をtest sandboxへ保存する" );
	if ( !bFixturesSaved ) { Assets.SetRootDirectory( PreviousRoot ); return; }
	const int32 PreviousScreen = GetDrawScreen();
	const int32 Screen = MakeScreen( 128, 128, TRUE );
	const int32 Soft = MakeARGB8ColorSoftImage( 128, 128 );
	C.Check( Screen >= 0 && Soft >= 0, "固定時刻のreadback用screenとSoftImageを作る" );
	if ( Screen < 0 || Soft < 0 )	{ if ( Screen >= 0 ) DeleteGraph( Screen ); if ( Soft >= 0 ) DeleteSoftImage( Soft ); Assets.SetRootDirectory( PreviousRoot ); return; }
	SetDrawScreen( Screen ); SetDrawAreaFull(); SetDrawBright( 255, 255, 255 ); SetBackgroundColor( 12, 20, 28, 255 ); SetCameraNearFar( 0.1f, 100.0f ); SetCameraDotAspect( 1.0f ); SetCameraScreenCenter( 64.0f, 64.0f ); SetupCamera_Ortho( 8.0f ); SetCameraPositionAndTarget_UpVecY( VGet( 0.0f, 0.0f, -10.0f ), VGet( 0.0f, 0.0f, 0.0f ) );
	MATRIX Viewport{}; CreateViewportMatrix( &Viewport, 64.0f, 64.0f, 128.0f, 128.0f ); SetTransformToViewport( &Viewport ); const MATRIX Identity = MGetIdent(); SetTransformToWorld( &Identity ); SetUseZBuffer3D( TRUE ); SetWriteZBuffer3D( FALSE );
	FMeshRenderer Renderer = NWorld::MakeMeshRenderer( MakeRealMeshMaterialFixture( SourcePath, MaskPath, NoisePath ), [&Assets]( const FString& Path ) { return Assets.LoadTexture( Path ); } );
	C.Check( Renderer.IsValid() && !Renderer.HasEffectPreparationFailures(), "本物のAssetManagerから3画像とmesh effect資源を準備する" );
	if ( !Renderer.IsValid() || Renderer.HasEffectPreparationFailures() )
	{
		SetDrawScreen( PreviousScreen ); DeleteSoftImage( Soft ); DeleteGraph( Screen ); Renderer = FMeshRenderer(); Assets.ReleaseUnused(); Assets.Tick( 0.0f ); Assets.SetRootDirectory( PreviousRoot ); return;
	}
	SetUseZBuffer3D( TRUE ); SetWriteZBuffer3D( FALSE );
	const bool bBaselineDrawn = RenderMeshMaterialRenderer( Renderer, Executor, Screen, Soft );
	const FColor32 BaselineTopLeft = ReadMeshMaterialPixel( Soft, 48, 48 );
	const FColor32 BaselineBottomRight = ReadMeshMaterialPixel( Soft, 80, 48 );
	const TArray<FColor32> BaselinePixels = CaptureMeshMaterialPixels( Soft );
	const int32 BaselineColorDifference = std::abs( static_cast<int32>( BaselineTopLeft.r ) - BaselineBottomRight.r ) + std::abs( static_cast<int32>( BaselineTopLeft.g ) - BaselineBottomRight.g ) + std::abs( static_cast<int32>( BaselineTopLeft.b ) - BaselineBottomRight.b );
	C.Check( bBaselineDrawn && BaselineColorDifference > 100, "時刻0の実meshへ非対称な元画像が描かれる" );

	// 0.5秒時点で曲線発光が元画素を明るくすることを確認する。
	Renderer.Update( 0.5f );
	const bool bEmissionDrawn = RenderMeshMaterialRenderer( Renderer, Executor, Screen, Soft );
	const FColor32 EmissionPixel = ReadMeshMaterialPixel( Soft, 48, 48 );
	C.Check( bEmissionDrawn && EmissionPixel.r > BaselineTopLeft.r + 35, "時刻0.5の発光曲線が実画素を明るくする" );
	Renderer.RestartEffects();

	// source画像のUV位相が固定時刻で左右上下の色配置を変えることを確認する。
	FEditMesh UvMesh = MakeRealMeshMaterialFixture( SourcePath, MaskPath, NoisePath );
	UvMesh.Materials[0].Effect.Material.UVScroll = FVector2( 0.75f, 0.0f );
	UvMesh.Materials[0].Effect.Material.bDissolve = false;
	UvMesh.Materials[0].Effect.Material.Distortion.bEnabled = false;
	UvMesh.Materials[0].Effect.Material.Emission.Keys.Reset();
	Renderer = NWorld::MakeMeshRenderer( UvMesh, [&Assets]( const FString& Path ) { return Assets.LoadTexture( Path ); } );
	Renderer.Update( 0.25f );
	const bool bUvDrawn = RenderMeshMaterialRenderer( Renderer, Executor, Screen, Soft );
	const int32 UvDifferences = CountMeshMaterialPixelDifferences( Soft, BaselinePixels );
	C.Check( bUvDrawn && UvDifferences > 700, "固定source UV位相が実画素の広い領域を変える" );
	Renderer = NWorld::MakeMeshRenderer( MakeRealMeshMaterialFixture( SourcePath, MaskPath, NoisePath ), [&Assets]( const FString& Path ) { return Assets.LoadTexture( Path ); } );
	Renderer.Update( 0.0f );

	// maskの左右値と消失曲線を組み合わせて実際に半面を消す。
	Renderer.Update( 0.5f );
	const bool bMaskDrawn = RenderMeshMaterialRenderer( Renderer, Executor, Screen, Soft );
	const FColor32 MaskLeft = ReadMeshMaterialPixel( Soft, 48, 64 );
	const FColor32 MaskRight = ReadMeshMaterialPixel( Soft, 80, 64 );
	const int32 MaskLeftBrightness = std::max( { MaskLeft.r, MaskLeft.g, MaskLeft.b } );
	const int32 MaskRightBrightness = std::max( { MaskRight.r, MaskRight.g, MaskRight.b } );
	C.Check( bMaskDrawn && ( MaskLeftBrightness < 30 ) != ( MaskRightBrightness < 30 ), "固定消失しきい値でmask片側に穴を作り、もう片側を残す" );

	// opacityは頂점alphaへ掛かり、RGBはAlpha合成で背景との中間になる。
	FEditMesh OpacityMesh = MakeRealMeshMaterialFixture( SourcePath, MaskPath, NoisePath );
	OpacityMesh.Materials[0].Effect.Opacity = 0.5f;
	OpacityMesh.Materials[0].Effect.Material.bDissolve = false;
	OpacityMesh.Materials[0].Effect.Material.Distortion.bEnabled = false;
	Renderer = NWorld::MakeMeshRenderer( OpacityMesh, [&Assets]( const FString& Path ) { return Assets.LoadTexture( Path ); } );
	Renderer.Update( 0.0f );
	const bool bOpacityDrawn = RenderMeshMaterialRenderer( Renderer, Executor, Screen, Soft, EDrawBlend::Alpha, 255 );
	const FColor32 OpacityPixel = ReadMeshMaterialPixel( Soft, 48, 48 );
	C.Check( bOpacityDrawn && OpacityPixel.r > 40 && OpacityPixel.r < BaselineTopLeft.r - 20 && OpacityPixel.b > 10, "opacityは既知の背景とのalpha合成へ作用しsource RGBを保つ" );

	// noise画像を使う歪みを0.5秒で適用し、同じ点列の実画素変化を確認する。
	FEditMesh NoiseMesh = MakeRealMeshMaterialFixture( SourcePath, MaskPath, NoisePath );
	NoiseMesh.Materials[0].Effect.Material.bDissolve = false;
	NoiseMesh.Materials[0].Effect.Material.Distortion.Strength.Keys.Reset();
	NoiseMesh.Materials[0].Effect.Material.Distortion.Strength.Keys.Add( { 0.0f, 0.0f } );
	NoiseMesh.Materials[0].Effect.Material.Distortion.Strength.Keys.Add( { 1.0f, 1.0f } );
	NoiseMesh.Materials[0].Effect.Material.Distortion.UVScale = FVector2( 8.0f, 8.0f );
	Renderer = NWorld::MakeMeshRenderer( NoiseMesh, [&Assets]( const FString& Path ) { return Assets.LoadTexture( Path ); } );
	Renderer.Update( 0.0f );
	const bool bNoiseBaseDrawn = RenderMeshMaterialRenderer( Renderer, Executor, Screen, Soft );
	const TArray<FColor32> NoiseBasePixels = CaptureMeshMaterialPixels( Soft );
	Renderer.Update( 0.5f );
	const bool bDistortionDrawn = RenderMeshMaterialRenderer( Renderer, Executor, Screen, Soft );
	const int32 DistortionDifferences = CountMeshMaterialPixelDifferences( Soft, NoiseBasePixels );
	C.Check( bNoiseBaseDrawn && bDistortionDrawn && DistortionDifferences > 100, "ready noiseを使う固定歪み位相が実画素を変える" );

	// effectの前後でcaller blendをDrawContext上へ戻し、後続通常meshも命令へ残す。
	FMeshRenderer Mixed = NWorld::MakeMeshRenderer( MakeRealMeshMaterialFixture( SourcePath, MaskPath, NoisePath ), [&Assets]( const FString& Path ) { return Assets.LoadTexture( Path ); } );
	FMeshRenderer Plain = Mixed;
	Plain.Batches[0].bEffectRequested = false;
	FDrawBuffer StateBuffer;
	FDrawContext StateDraw( StateBuffer );
	StateDraw.Blend( EDrawBlend::Add, 91 );
	Mixed.Record( StateDraw, FTransform::Identity );
	Plain.Record( StateDraw, FTransform::Identity );
	int32 MaterialCommands = 0, PlainCommands = 0;
	if ( !StateBuffer.Words.empty() ) ForEachDrawCommand( FDrawSpan{ StateBuffer.Words.data(), static_cast<int32>( StateBuffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::MaterialMesh3D ) ++MaterialCommands; if ( Command.Type == EDrawCommand::Mesh3D ) ++PlainCommands; } );
	C.Check( MaterialCommands > 0 && PlainCommands == 1 && StateDraw.GetBlendMode() == EDrawBlend::Add && StateDraw.GetBlendParam() == 91, "effect命令後も通常meshと呼出元Add 91状態を保つ" );

	// 変換を含む命令頂点が一度だけworld位置へ移ることを読む。
	FDrawBuffer TransformBuffer;
	FDrawContext TransformDraw( TransformBuffer );
	Renderer.StopEffects();
	Renderer.RestartEffects();
	Renderer.Record( TransformDraw, FTransform( FVector3( 3.0f, 2.0f, 1.0f ) ) );
	bool bTranslated = false;
	if ( !TransformBuffer.Words.empty() ) ForEachDrawCommand( FDrawSpan{ TransformBuffer.Words.data(), static_cast<int32>( TransformBuffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::MaterialMesh3D && Command.NumDataWords >= static_cast<int32>( sizeof( FDrawMaterialMeshData ) / 4 + sizeof( FMeshVertex ) / 4 ) ) { FMeshVertex Vertex{}; std::memcpy( &Vertex, Command.Data + sizeof( FDrawMaterialMeshData ) / 4, sizeof( Vertex ) ); bTranslated = std::abs( Vertex.Position[0] ) >= 1.0f && std::abs( Vertex.Position[1] ) >= 1.0f; } } );
	C.Check( bTranslated, "実mesh effect頂点へworld transformを一度適用する" );

	SetDrawScreen( PreviousScreen ); DeleteSoftImage( Soft ); DeleteGraph( Screen );
	Renderer = FMeshRenderer(); Mixed = FMeshRenderer(); Plain = FMeshRenderer();
	Assets.ReleaseUnused(); Assets.Tick( 0.0f );
	Assets.SetRootDirectory( PreviousRoot );
}
