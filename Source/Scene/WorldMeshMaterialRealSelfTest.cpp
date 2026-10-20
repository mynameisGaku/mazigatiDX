// SPDX-License-Identifier: NOASSERTION
#include "WorldMeshMaterialSelfTestSupport.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <vector>

#include "Library/Core/Interfaces/Render/DrawMaterialMesh.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Render/MeshRender.h"
#include "Library/Framework/World/Mesh/EditMeshBuild.h"
#include "Library/Framework/World/WorldRender.h"
#include "Library/Platform/FileSystem.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

namespace
{
	// 実機試験前のDxLib画面・camera・viewport・色・合成設定を退避する。
	struct FMeshMaterialCallerState
	{
		int32 Screen = GetDrawScreen();
		int32 ScreenReset = GetUseSetDrawScreenSettingReset();
		int32 Culling = GetUseBackCulling();
		float32 Near = GetCameraNear();
		float32 Far = GetCameraFar();
		float32 Aspect = GetCameraDotAspect();
		float32 Center[2] = {};
		MATRIX CameraView = GetCameraViewMatrix();
		MATRIX CameraProjection = GetCameraProjectionMatrix();
		MATRIX World{};
		MATRIX View{};
		MATRIX Projection{};
		MATRIX Viewport{};
		RECT Area{};
		int32 Blend[2] = {};
		int32 Bright[3] = {};
		int32 Background[4] = {};

		FMeshMaterialCallerState()
		{
			GetCameraScreenCenter( &Center[0], &Center[1] );
			GetTransformToWorldMatrix( &World ); GetTransformToViewMatrix( &View ); GetTransformToProjectionMatrix( &Projection ); GetTransformToViewportMatrix( &Viewport ); GetDrawArea( &Area );
			GetDrawBlendMode( &Blend[0], &Blend[1] ); GetDrawBright( &Bright[0], &Bright[1], &Bright[2] ); GetBackgroundColor( &Background[0], &Background[1], &Background[2], &Background[3] );
			SetUseSetDrawScreenSettingReset( FALSE );
		}

		~FMeshMaterialCallerState()
		{
			RenderVertex(); SetDrawScreen( Screen );
			SetCameraNearFar( Near, Far ); SetCameraDotAspect( Aspect ); SetCameraScreenCenter( Center[0], Center[1] ); SetCameraViewMatrix( CameraView ); SetupCamera_ProjectionMatrix( CameraProjection );
			SetTransformToWorld( &World ); SetTransformToView( &View ); SetTransformToProjection( &Projection ); SetTransformToViewport( &Viewport ); SetDrawArea( Area.left, Area.top, Area.right, Area.bottom );
			SetDrawBlendMode( Blend[0], Blend[1] ); SetDrawBright( Bright[0], Bright[1], Bright[2] ); SetBackgroundColor( Background[0], Background[1], Background[2], Background[3] ); SetUseBackCulling( Culling ); SetUseSetDrawScreenSettingReset( ScreenReset );
		}
	};

	// 既知一色、非対称模様、二値mask、細かいnoiseを持つ16x16 TGAを作る。
	std::vector<uint8> MakeMeshMaterialTga( int32 Kind )
	{
		constexpr int32 Width = 16;
		constexpr int32 Height = 16;
		std::vector<uint8> Bytes = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, Width, 0, Height, 0, 32, 0x28 };
		for ( int32 Y = 0; Y < Height; ++Y )
		{
			for ( int32 X = 0; X < Width; ++X )
			{
				uint8 R = 80, G = 40, B = 20;
				if ( Kind == 1 ) { R = static_cast<uint8>( 35 + ( X * 31 + Y * 17 ) % 180 ); G = static_cast<uint8>( 25 + ( X * 13 + Y * 41 ) % 190 ); B = static_cast<uint8>( 30 + ( X * 47 + Y * 7 ) % 170 ); }
				if ( Kind == 2 ) R = G = B = X < Width / 2 ? 0 : 255;
				if ( Kind == 3 ) R = G = B = ( ( X / 2 + Y / 2 ) % 2 == 0 ) ? 16 : 240;
				Bytes.push_back( B ); Bytes.push_back( G ); Bytes.push_back( R ); Bytes.push_back( 255 );
			}
		}
		return Bytes;
	}

	// 指定された材質設定を持つ、表がカメラへ向く単一面を作る。
	FEditMesh MakeMeshMaterialFixture( const FString& Source, const FString& Mask, const FString& Noise, float32 Opacity, float32 Emission, bool bDissolve, float32 Dissolve, float32 Distortion, const FVector2& UVScroll, const FVector2& NoiseScroll, EDrawBlend Blend )
	{
		FEditMesh Mesh;
		Mesh.Vertices.Add( FVector3( -2.0f, -2.0f, 0.0f ) );
		Mesh.Vertices.Add( FVector3( 2.0f, -2.0f, 0.0f ) );
		Mesh.Vertices.Add( FVector3( 2.0f, 2.0f, 0.0f ) );
		Mesh.Vertices.Add( FVector3( -2.0f, 2.0f, 0.0f ) );
		FEditMeshFace Face;
		Face.Indices.Add( 0 ); Face.Indices.Add( 3 ); Face.Indices.Add( 2 ); Face.Indices.Add( 1 );
		Mesh.Faces.Add( Face );
		FEditMeshMaterial& Material = Mesh.Materials[Mesh.Materials.Add( FEditMeshMaterial() )];
		Material.Texture = Source;
		Material.Color = FColor32( 255, 255, 255, 255 );
		Material.Tile = 4.0f;
		Material.bTwoSided = true;
		Material.Effect.Material.bEnabled = true;
		Material.Effect.Material.bDissolve = bDissolve;
		Material.Effect.Material.MaskTexture = Mask;
		Material.Effect.Material.Dissolve.Interpolation = FEffectScalarCurve::EInterpolation::Linear;
		Material.Effect.Material.Dissolve.Keys.Add( { 0.0f, Dissolve } );
		Material.Effect.Material.Dissolve.Keys.Add( { 1.0f, Dissolve } );
		Material.Effect.Material.Emission.Interpolation = FEffectScalarCurve::EInterpolation::Linear;
		Material.Effect.Material.Emission.Keys.Add( { 0.0f, Emission } );
		Material.Effect.Material.Emission.Keys.Add( { 1.0f, Emission } );
		Material.Effect.Material.UVScroll = UVScroll;
		Material.Effect.Material.Distortion.bEnabled = Distortion > 0.0f;
		Material.Effect.Material.Distortion.Texture = Noise;
		Material.Effect.Material.Distortion.UVScroll = NoiseScroll;
		Material.Effect.Material.Distortion.Strength.Keys.Add( { 0.0f, Distortion } );
		Material.Effect.Material.Distortion.Strength.Keys.Add( { 1.0f, Distortion } );
		Material.Effect.Duration = 1.0f;
		Material.Effect.Opacity = Opacity;
		Material.Effect.Blend = Blend;
		return Mesh;
	}

	// SoftImageの一画素をRGBAへ読む。
	FColor32 MeshMaterialPixel( int32 Soft, int32 X, int32 Y )
	{
		int32 R = 0, G = 0, B = 0, A = 0;
		GetPixelSoftImage( Soft, X, Y, &R, &G, &B, &A );
		return FColor32( static_cast<uint8>( R ), static_cast<uint8>( G ), static_cast<uint8>( B ), static_cast<uint8>( A ) );
	}

	// 許容誤差内で画素のRGBが既知色に一致するか調べる。
	bool IsMeshMaterialColorNear( const FColor32& Actual, const FColor32& Expected, int32 Tolerance )
	{
		return std::abs( static_cast<int32>( Actual.r ) - Expected.r ) <= Tolerance && std::abs( static_cast<int32>( Actual.g ) - Expected.g ) <= Tolerance && std::abs( static_cast<int32>( Actual.b ) - Expected.b ) <= Tolerance;
	}

	// 背景色と比べ、既知のmesh pixelが描かれた強さを返す。
	int32 MeshMaterialColorDistance( const FColor32& Actual, const FColor32& Background )
	{
		return std::abs( static_cast<int32>( Actual.r ) - Background.r ) + std::abs( static_cast<int32>( Actual.g ) - Background.g ) + std::abs( static_cast<int32>( Actual.b ) - Background.b );
	}

	// 画面内の中央64x64領域から安定した比較用画素を取得する。
	TArray<FColor32> CaptureMeshMaterialPixels( int32 Soft )
	{
		TArray<FColor32> Pixels;
		Pixels.Reserve( 64 * 64 );
		for ( int32 Y = 32; Y < 96; ++Y ) for ( int32 X = 32; X < 96; ++X ) Pixels.Add( MeshMaterialPixel( Soft, X, Y ) );
		return Pixels;
	}
	// 固定時刻でmeshを描き、画素を読み取る。必要ならnative rasterのbindと復帰も記録する。
	// RasterBeforeDraw: 実行直前にbindする状態。各bool出力: bind前後の一致結果。
	bool RenderMeshMaterial( FMeshRenderer& Renderer, IDrawExecutor& Executor, int32 Screen, int32 Soft, EDrawBlend InitialBlend = EDrawBlend::Alpha, const FTransform& World = FTransform::Identity, ID3D11RasterizerState* RasterBeforeDraw = nullptr, bool* bRasterBoundBeforeDraw = nullptr, bool* bRasterRestoredAfterDraw = nullptr )
	{
		SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		Draw.Blend( InitialBlend, 255 );
		Renderer.Record( Draw, World );
		if ( Buffer.Words.empty() ) return false;
		// 実行前後のnative rasterを調べるcontext。
		ID3D11DeviceContext* NativeContext = const_cast<ID3D11DeviceContext*>( static_cast<const ID3D11DeviceContext*>( GetUseDirect3D11DeviceContext() ) );
		if ( RasterBeforeDraw != nullptr )
		{
			if ( NativeContext == nullptr ) return false;
			RenderVertex(); NativeContext->RSSetState( RasterBeforeDraw );
			// 実行直前に現在bindされているraster。
			ID3D11RasterizerState* BoundRaster = nullptr; NativeContext->RSGetState( &BoundRaster );
			if ( bRasterBoundBeforeDraw != nullptr ) *bRasterBoundBeforeDraw = BoundRaster == RasterBeforeDraw;
			if ( BoundRaster != nullptr ) BoundRaster->Release();
		}
		Executor.Execute( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
		if ( RasterBeforeDraw != nullptr && NativeContext != nullptr )
		{
			// draw後に復帰したraster。
			ID3D11RasterizerState* RestoredRaster = nullptr; NativeContext->RSGetState( &RestoredRaster );
			if ( bRasterRestoredAfterDraw != nullptr ) *bRasterRestoredAfterDraw = RestoredRaster == RasterBeforeDraw;
			if ( RestoredRaster != nullptr ) RestoredRaster->Release();
		}
		RenderVertex();
		return GetDrawScreenSoftImage( 0, 0, 128, 128, Soft ) == 0;
	}

	// 同一64x64領域のうちRGBが変わった画素数を数える。
	int32 CountPixelDifferences( int32 Soft, const TArray<FColor32>& Previous )
	{
		int32 Different = 0;
		for ( int32 Y = 32; Y < 96; ++Y ) for ( int32 X = 32; X < 96; ++X )
		{
			const FColor32 Current = MeshMaterialPixel( Soft, X, Y );
			const FColor32& Before = Previous[( Y - 32 ) * 64 + X - 32];
			if ( Current.r != Before.r || Current.g != Before.g || Current.b != Before.b ) ++Different;
		}
		return Different;
	}
	// 記録列からheader、先頭頂点を読み、指定時は通常culling flagも返す。
	// bUseRegularCulling: flagの出力先。nullなら読み捨てる。
	bool ReadFirstMaterialMesh( const FDrawBuffer& Buffer, FDrawMaterialMeshData& OutHeader, FMeshVertex& OutVertex, bool* bUseRegularCulling = nullptr )
	{
		bool bFound = false;
		if ( !Buffer.Words.empty() ) ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type != EDrawCommand::MaterialMesh3D || Command.NumDataWords < static_cast<int32>( sizeof( FDrawMaterialMeshData ) / 4 + sizeof( FMeshVertex ) / 4 ) ) return;
			OutHeader = Command.Read<FDrawMaterialMeshData>();
			if ( bUseRegularCulling != nullptr ) *bUseRegularCulling = Command.Has( FDrawFlag::UseRegularCulling );
			std::memcpy( &OutVertex, Command.Data + sizeof( FDrawMaterialMeshData ) / 4, sizeof( OutVertex ) );
			bFound = true;
		} );
		return bFound;
	}
}

// 実DxLib rendererで一つずつ条件を変え、固定時刻の画素と状態復元を確認する。
void NWorldTest::RunMeshMaterialRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Dir )
{
	C.Group = "world-mesh-material-effect-real";
	C.Check( Executor.SupportsEffectMaterialMesh(), "実描画先が材質付きmeshをサポートする" );
	if ( !Executor.SupportsEffectMaterialMesh() ) { C.Skip( "現在の描画先はmesh material effectに非対応" ); return; }
	FMeshMaterialCallerState Saved;
	const FString PreviousRoot = Assets.GetRootDirectory();
	Assets.SetRootDirectory( Dir );
	const FString FixtureDirectory = "mesh-material-real";
	const FString UniformPath = FixtureDirectory / "uniform.tga";
	const FString PatternPath = FixtureDirectory / "pattern.tga";
	const FString MaskPath = FixtureDirectory / "mask.tga";
	const FString NoisePath = FixtureDirectory / "noise.tga";
	const bool bFixturesSaved = NFileSystem::EnsureDirectory( Dir / FixtureDirectory ) && NAssetTest::WriteFileBytes( Dir / UniformPath, MakeMeshMaterialTga( 0 ) ) && NAssetTest::WriteFileBytes( Dir / PatternPath, MakeMeshMaterialTga( 1 ) ) && NAssetTest::WriteFileBytes( Dir / MaskPath, MakeMeshMaterialTga( 2 ) ) && NAssetTest::WriteFileBytes( Dir / NoisePath, MakeMeshMaterialTga( 3 ) );
	C.Check( bFixturesSaved, "既知色・非対称source・二値mask・noise画像をtest sandboxへ保存する" );
	if ( !bFixturesSaved ) { Assets.SetRootDirectory( PreviousRoot ); return; }
	const int32 Screen = MakeScreen( 128, 128, TRUE );
	const int32 Soft = MakeARGB8ColorSoftImage( 128, 128 );
	C.Check( Screen >= 0 && Soft >= 0, "画素比較用screenとSoftImageを作る" );
	if ( Screen < 0 || Soft < 0 ) { if ( Screen >= 0 ) DeleteGraph( Screen ); if ( Soft >= 0 ) DeleteSoftImage( Soft ); Assets.SetRootDirectory( PreviousRoot ); return; }
	SetDrawScreen( Screen ); SetDrawAreaFull(); SetDrawBright( 255, 255, 255 ); SetBackgroundColor( 20, 30, 40, 255 ); SetCameraNearFar( 0.1f, 100.0f ); SetCameraDotAspect( 1.0f ); SetCameraScreenCenter( 64.0f, 64.0f ); SetupCamera_Ortho( 8.0f ); SetCameraPositionAndTarget_UpVecY( VGet( 0.0f, 0.0f, -10.0f ), VGet( 0.0f, 0.0f, 0.0f ) );
	MATRIX Viewport{}; CreateViewportMatrix( &Viewport, 64.0f, 64.0f, 128.0f, 128.0f ); SetTransformToViewport( &Viewport ); const MATRIX Identity = MGetIdent(); SetTransformToWorld( &Identity ); SetUseZBuffer3D( TRUE ); SetWriteZBuffer3D( FALSE );
	const auto LoadTexture = [&Assets]( const FString& Path ) { return Assets.LoadTexture( Path ); };

	// cleanBaseは白tint・Alpha・Opacity 1・Emission 1・Dissolve off・Distortion off・UV scroll 0。
	FMeshRenderer CleanBase = NWorld::MakeMeshRenderer( MakeMeshMaterialFixture( UniformPath, FString(), FString(), 1.0f, 1.0f, false, 0.0f, 0.0f, FVector2( 0.0f, 0.0f ), FVector2( 0.0f, 0.0f ), EDrawBlend::Alpha ), LoadTexture );
	CleanBase.Update( 0.5f );
	const bool bCleanDrawn = CleanBase.IsValid() && RenderMeshMaterial( CleanBase, Executor, Screen, Soft );
	const FColor32 CleanPixel = MeshMaterialPixel( Soft, 48, 48 );
	C.Check( bCleanDrawn && CleanPixel.r >= 78 && CleanPixel.r <= 82 && CleanPixel.g >= 38 && CleanPixel.g <= 42 && CleanPixel.b >= 18 && CleanPixel.b <= 22, "cleanBaseの既知RGBが時刻0.5でも変わらない: got %d,%d,%d expected 80,40,20", CleanPixel.r, CleanPixel.g, CleanPixel.b );

	// 同時刻のEmission 1と2だけを比べ、8bitの既知RGB倍率を確認する。
	FMeshRenderer EmissionTwo = NWorld::MakeMeshRenderer( MakeMeshMaterialFixture( UniformPath, FString(), FString(), 1.0f, 2.0f, false, 0.0f, 0.0f, FVector2( 0.0f, 0.0f ), FVector2( 0.0f, 0.0f ), EDrawBlend::Alpha ), LoadTexture );
	EmissionTwo.Update( 0.5f );
	const bool bEmissionDrawn = RenderMeshMaterial( EmissionTwo, Executor, Screen, Soft );
	const FColor32 EmissionPixel = MeshMaterialPixel( Soft, 48, 48 );
	C.Check( bEmissionDrawn && EmissionPixel.r >= 158 && EmissionPixel.r <= 162 && EmissionPixel.g >= 78 && EmissionPixel.g <= 82 && EmissionPixel.b >= 38 && EmissionPixel.b <= 42, "Emission 2は既知8bit RGBの2倍: got %d,%d,%d expected 160,80,40", EmissionPixel.r, EmissionPixel.g, EmissionPixel.b );

	// 同時刻のUV scrollだけを変えた非対称画像を比べる。
	FMeshRenderer UvBase = NWorld::MakeMeshRenderer( MakeMeshMaterialFixture( PatternPath, FString(), FString(), 1.0f, 1.0f, false, 0.0f, 0.0f, FVector2( 0.0f, 0.0f ), FVector2( 0.0f, 0.0f ), EDrawBlend::Alpha ), LoadTexture );
	FMeshRenderer UvMoved = NWorld::MakeMeshRenderer( MakeMeshMaterialFixture( PatternPath, FString(), FString(), 1.0f, 1.0f, false, 0.0f, 0.0f, FVector2( 0.75f, 0.0f ), FVector2( 0.0f, 0.0f ), EDrawBlend::Alpha ), LoadTexture );
	UvBase.Update( 0.5f ); UvMoved.Update( 0.5f );
	const bool bUvBaseDrawn = RenderMeshMaterial( UvBase, Executor, Screen, Soft );
	const TArray<FColor32> UvBasePixels = CaptureMeshMaterialPixels( Soft );
	const bool bUvMovedDrawn = RenderMeshMaterial( UvMoved, Executor, Screen, Soft );
	const int32 UvDifferentPixels = CountPixelDifferences( Soft, UvBasePixels );
	C.Check( bUvBaseDrawn && bUvMovedDrawn && UvDifferentPixels > 700, "同時刻のsource UV scrollだけで模様が移動する: different=%d/4096 expected>700", UvDifferentPixels );

	// 強度だけを0から1へ変え、source UV・発光・消失・noise UVを同じにして比べる。
	FMeshRenderer DistortionZero = NWorld::MakeMeshRenderer( MakeMeshMaterialFixture( PatternPath, FString(), NoisePath, 1.0f, 1.0f, false, 0.0f, 0.0f, FVector2( 0.0f, 0.0f ), FVector2( 0.0f, 0.0f ), EDrawBlend::Alpha ), LoadTexture );
	FMeshRenderer DistortionOne = NWorld::MakeMeshRenderer( MakeMeshMaterialFixture( PatternPath, FString(), NoisePath, 1.0f, 1.0f, false, 0.0f, 1.0f, FVector2( 0.0f, 0.0f ), FVector2( 0.0f, 0.0f ), EDrawBlend::Alpha ), LoadTexture );
	DistortionZero.Update( 0.5f ); DistortionOne.Update( 0.5f );
	const bool bDistortionZeroDrawn = RenderMeshMaterial( DistortionZero, Executor, Screen, Soft );
	const TArray<FColor32> DistortionZeroPixels = CaptureMeshMaterialPixels( Soft );
	const bool bDistortionOneDrawn = RenderMeshMaterial( DistortionOne, Executor, Screen, Soft );
	const int32 DistortionDifferentPixels = CountPixelDifferences( Soft, DistortionZeroPixels );
	C.Check( bDistortionZeroDrawn && bDistortionOneDrawn && DistortionDifferentPixels > 100, "同時刻に歪み強度だけを変えるとnoiseで画素が変わる: different=%d/4096 expected>100", DistortionDifferentPixels );

	// 強度を1に固定し、noise UV scrollだけの違いを独立して測る。
	FMeshRenderer NoiseBase = NWorld::MakeMeshRenderer( MakeMeshMaterialFixture( PatternPath, FString(), NoisePath, 1.0f, 1.0f, false, 0.0f, 1.0f, FVector2( 0.0f, 0.0f ), FVector2( 0.0f, 0.0f ), EDrawBlend::Alpha ), LoadTexture );
	FMeshRenderer NoiseMoved = NWorld::MakeMeshRenderer( MakeMeshMaterialFixture( PatternPath, FString(), NoisePath, 1.0f, 1.0f, false, 0.0f, 1.0f, FVector2( 0.0f, 0.0f ), FVector2( 0.75f, -0.5f ), EDrawBlend::Alpha ), LoadTexture );
	NoiseBase.Update( 0.5f ); NoiseMoved.Update( 0.5f );
	const bool bNoiseBaseDrawn = RenderMeshMaterial( NoiseBase, Executor, Screen, Soft );
	const TArray<FColor32> NoiseBasePixels = CaptureMeshMaterialPixels( Soft );
	const bool bNoiseMovedDrawn = RenderMeshMaterial( NoiseMoved, Executor, Screen, Soft );
	const int32 NoiseDifferentPixels = CountPixelDifferences( Soft, NoiseBasePixels );
	C.Check( bNoiseBaseDrawn && bNoiseMovedDrawn && NoiseDifferentPixels > 100, "同時刻にnoise scrollだけを変えると参照模様が移動する: different=%d/4096 expected>100", NoiseDifferentPixels );

	// opacity 1と.5を既知背景・既知色で比較し、出力RGBをalpha式と照合する。
	FMeshRenderer OpacityOne = NWorld::MakeMeshRenderer( MakeMeshMaterialFixture( UniformPath, FString(), FString(), 1.0f, 1.0f, false, 0.0f, 0.0f, FVector2( 0.0f, 0.0f ), FVector2( 0.0f, 0.0f ), EDrawBlend::Alpha ), LoadTexture );
	FMeshRenderer OpacityHalf = NWorld::MakeMeshRenderer( MakeMeshMaterialFixture( UniformPath, FString(), FString(), 0.5f, 1.0f, false, 0.0f, 0.0f, FVector2( 0.0f, 0.0f ), FVector2( 0.0f, 0.0f ), EDrawBlend::Alpha ), LoadTexture );
	OpacityOne.Update( 0.5f ); OpacityHalf.Update( 0.5f );
	const bool bOpacityOneDrawn = RenderMeshMaterial( OpacityOne, Executor, Screen, Soft );
	const FColor32 OpacityOnePixel = MeshMaterialPixel( Soft, 48, 48 );
	const bool bOpacityHalfDrawn = RenderMeshMaterial( OpacityHalf, Executor, Screen, Soft );
	const FColor32 OpacityHalfPixel = MeshMaterialPixel( Soft, 48, 48 );
	C.Check( bOpacityOneDrawn && bOpacityHalfDrawn && OpacityOnePixel.r >= 78 && OpacityOnePixel.r <= 82 && OpacityHalfPixel.r >= 49 && OpacityHalfPixel.r <= 51 && OpacityHalfPixel.g >= 34 && OpacityHalfPixel.g <= 36 && OpacityHalfPixel.b >= 29 && OpacityHalfPixel.b <= 31, "Opacity .5は既知背景との中間RGBを作る: full=%d,%d,%d half=%d,%d,%d expected half=50,35,30", OpacityOnePixel.r, OpacityOnePixel.g, OpacityOnePixel.b, OpacityHalfPixel.r, OpacityHalfPixel.g, OpacityHalfPixel.b );

	// 同じ既知source・背景でAlphaとAddの合成結果を照合する。
	FMeshRenderer AddRenderer = NWorld::MakeMeshRenderer( MakeMeshMaterialFixture( UniformPath, FString(), FString(), 1.0f, 1.0f, false, 0.0f, 0.0f, FVector2( 0.0f, 0.0f ), FVector2( 0.0f, 0.0f ), EDrawBlend::Add ), LoadTexture );
	AddRenderer.Update( 0.5f );
	const bool bAddDrawn = RenderMeshMaterial( AddRenderer, Executor, Screen, Soft );
	const FColor32 AddPixel = MeshMaterialPixel( Soft, 48, 48 );
	C.Check( bAddDrawn && AddPixel.r >= 98 && AddPixel.r <= 102 && AddPixel.g >= 68 && AddPixel.g <= 72 && AddPixel.b >= 58 && AddPixel.b <= 62, "Addは既知背景へsource RGBを加える: got %d,%d,%d expected 100,70,60", AddPixel.r, AddPixel.g, AddPixel.b );

	// 固定消失値とmaskを同時刻で評価し、実画像の片側へ穴を作る。
	FMeshRenderer MaskRenderer = NWorld::MakeMeshRenderer( MakeMeshMaterialFixture( UniformPath, MaskPath, FString(), 1.0f, 1.0f, true, 0.5f, 0.0f, FVector2( 0.0f, 0.0f ), FVector2( 0.0f, 0.0f ), EDrawBlend::Alpha ), LoadTexture );
	MaskRenderer.Update( 0.5f );
	const bool bMaskDrawn = RenderMeshMaterial( MaskRenderer, Executor, Screen, Soft );
	const FColor32 MaskPixelA = MeshMaterialPixel( Soft, 48, 64 );
	const FColor32 MaskPixelB = MeshMaterialPixel( Soft, 80, 64 );
	const FColor32 KnownBackground( 20, 30, 40, 255 );
	const FColor32 KnownSource( 80, 40, 20, 255 );
	const bool bMaskPixelAIsHole = IsMeshMaterialColorNear( MaskPixelA, KnownBackground, 1 );
	const bool bMaskPixelBIsHole = IsMeshMaterialColorNear( MaskPixelB, KnownBackground, 1 );
	const bool bMaskPixelAIsSource = IsMeshMaterialColorNear( MaskPixelA, KnownSource, 1 );
	const bool bMaskPixelBIsSource = IsMeshMaterialColorNear( MaskPixelB, KnownSource, 1 );
	C.Check( bMaskDrawn && ( bMaskPixelAIsHole && bMaskPixelBIsSource || bMaskPixelBIsHole && bMaskPixelAIsSource ), "二値maskの一方は背景、他方はsource色になる: A=%d,%d,%d B=%d,%d,%d", MaskPixelA.r, MaskPixelA.g, MaskPixelA.b, MaskPixelB.r, MaskPixelB.g, MaskPixelB.b );

	// 次のplain meshをeffect単独時と同じ位置で描き、合成state漏れを画素で比較する。
	FMeshRenderer EffectThenPlain = NWorld::MakeMeshRenderer( MakeMeshMaterialFixture( UniformPath, FString(), FString(), 1.0f, 1.0f, false, 0.0f, 0.0f, FVector2( 0.0f, 0.0f ), FVector2( 0.0f, 0.0f ), EDrawBlend::Add ), LoadTexture );
	FMeshRenderer PlainControl = EffectThenPlain;
	PlainControl.Batches[0].bEffectRequested = false;
	FDrawBuffer SequenceBuffer;
	FDrawContext SequenceDraw( SequenceBuffer );
	SequenceDraw.Blend( EDrawBlend::Alpha, 255 );
	EffectThenPlain.Record( SequenceDraw, FTransform( FVector3( -1.5f, 0.0f, 0.0f ) ) );
	PlainControl.Record( SequenceDraw, FTransform( FVector3( 1.5f, 0.0f, 0.0f ) ) );
	const EDrawBlend BlendAfterEffect = SequenceDraw.GetBlendMode();
	const int32 BlendParamAfterEffect = SequenceDraw.GetBlendParam();
	SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
	Executor.Execute( FDrawSpan{ SequenceBuffer.Words.data(), static_cast<int32>( SequenceBuffer.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
	const FColor32 PlainAfterEffect = MeshMaterialPixel( Soft, 80, 64 );
	SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
	FDrawBuffer PlainBuffer; FDrawContext PlainDraw( PlainBuffer ); PlainDraw.Blend( EDrawBlend::Alpha, 255 ); PlainControl.Record( PlainDraw, FTransform( FVector3( 1.5f, 0.0f, 0.0f ) ) ); Executor.Execute( FDrawSpan{ PlainBuffer.Words.data(), static_cast<int32>( PlainBuffer.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
	const FColor32 PlainAlone = MeshMaterialPixel( Soft, 80, 64 );
	const bool bPlainPositiveControl = MeshMaterialColorDistance( PlainAlone, KnownBackground ) > 20;
	C.Check( BlendAfterEffect == EDrawBlend::Alpha && BlendParamAfterEffect == 255 && bPlainPositiveControl && PlainAfterEffect.r == PlainAlone.r && PlainAfterEffect.g == PlainAlone.g && PlainAfterEffect.b == PlainAlone.b, "effect後の通常mesh画素は描画され単独時と一致: after=%d,%d,%d alone=%d,%d,%d background=%d,%d,%d", PlainAfterEffect.r, PlainAfterEffect.g, PlainAfterEffect.b, PlainAlone.r, PlainAlone.g, PlainAlone.b, KnownBackground.r, KnownBackground.g, KnownBackground.b );

	// 非一様・負・ゼロscaleを含むworld頂点と逆scale法線を独立に照合する。
	const FEditMesh TransformMesh = MakeMeshMaterialFixture( UniformPath, FString(), FString(), 1.0f, 1.0f, false, 0.0f, 0.0f, FVector2( 0.0f, 0.0f ), FVector2( 0.0f, 0.0f ), EDrawBlend::Alpha );
	FMeshRenderer TransformRenderer = NWorld::MakeMeshRenderer( TransformMesh, LoadTexture );
	const FTransform ExpectedTransform( FVector3( 3.0f, 2.0f, 1.0f ), FQuaternion::Identity, FVector3( -2.0f, 0.0f, 0.5f ) );
	FDrawBuffer TransformBuffer; FDrawContext TransformDraw( TransformBuffer ); TransformRenderer.Record( TransformDraw, ExpectedTransform );
	FDrawMaterialMeshData TransformHeader; FMeshVertex FirstVertex;
	// 3b rendererが通常culling flagを記録したか。
	bool bRecordedRegularCulling = false;
	const bool bReadTransform = ReadFirstMaterialMesh( TransformBuffer, TransformHeader, FirstVertex, &bRecordedRegularCulling );
	const FEditMeshBuild LocalBuild = NEditMesh::BuildRenderMesh( TransformMesh );
	const FVector3 ExpectedPosition = ExpectedTransform.TransformPoint( FVector3( LocalBuild.Vertices[0].Position[0], LocalBuild.Vertices[0].Position[1], LocalBuild.Vertices[0].Position[2] ) );
	const FVector3 ExpectedNormal( LocalBuild.Vertices[0].Normal[0], LocalBuild.Vertices[0].Normal[1], LocalBuild.Vertices[0].Normal[2] );
	const FVector3 ExpectedWorldNormal = FVector3( 0.0f, 0.0f, ExpectedNormal.z / ExpectedTransform.Scale.z ).Normalized();
	C.Check( bReadTransform && bRecordedRegularCulling && std::abs( FirstVertex.Position[0] - ExpectedPosition.x ) < 1.0e-5f && std::abs( FirstVertex.Position[1] - ExpectedPosition.y ) < 1.0e-5f && std::abs( FirstVertex.Position[2] - ExpectedPosition.z ) < 1.0e-5f && std::abs( FirstVertex.Normal[0] - ExpectedWorldNormal.x ) < 1.0e-5f && std::abs( FirstVertex.Normal[1] - ExpectedWorldNormal.y ) < 1.0e-5f && std::abs( FirstVertex.Normal[2] - ExpectedWorldNormal.z ) < 1.0e-5f, "全XYZ位置とinverse-scale法線が負・非一様・zero scaleで一致: got p=%.3f,%.3f,%.3f n=%.3f,%.3f,%.3f expected p=%.3f,%.3f,%.3f n=%.3f,%.3f,%.3f", FirstVertex.Position[0], FirstVertex.Position[1], FirstVertex.Position[2], FirstVertex.Normal[0], FirstVertex.Normal[1], FirstVertex.Normal[2], ExpectedPosition.x, ExpectedPosition.y, ExpectedPosition.z, ExpectedWorldNormal.x, ExpectedWorldNormal.y, ExpectedWorldNormal.z );

	// effect停止後のMesh3D flagも同じ材質slotの片面・両面指定を保つ。
	FEditMesh OneSidedMesh = TransformMesh; OneSidedMesh.Materials[0].bTwoSided = false;
	FMeshRenderer OneSidedRenderer = NWorld::MakeMeshRenderer( OneSidedMesh, LoadTexture );
	OneSidedRenderer.StopEffects();
	FDrawBuffer OneSidedBuffer; FDrawContext OneSidedDraw( OneSidedBuffer ); OneSidedRenderer.Record( OneSidedDraw, FTransform::Identity );
	FMeshRenderer TwoSidedRenderer = TransformRenderer; TwoSidedRenderer.StopEffects();
	FDrawBuffer TwoSidedBuffer; FDrawContext TwoSidedDraw( TwoSidedBuffer ); TwoSidedRenderer.Record( TwoSidedDraw, FTransform::Identity );
	bool bOneSidedFlag = false, bTwoSidedFlag = false;
	if ( !OneSidedBuffer.Words.empty() ) ForEachDrawCommand( FDrawSpan{ OneSidedBuffer.Words.data(), static_cast<int32>( OneSidedBuffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::Mesh3D ) bOneSidedFlag = !Command.Has( FDrawFlag::TwoSided ); } );
	if ( !TwoSidedBuffer.Words.empty() ) ForEachDrawCommand( FDrawSpan{ TwoSidedBuffer.Words.data(), static_cast<int32>( TwoSidedBuffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::Mesh3D ) bTwoSidedFlag = Command.Has( FDrawFlag::TwoSided ); } );
	C.Check( bOneSidedFlag && bTwoSidedFlag && TransformHeader.bTwoSided == 1, "effect meshとlegacy Mesh3Dが片面・両面契約を保つ" );
	TransformRenderer.StopEffects();
	FMeshRenderer DisabledEffectPath = TransformRenderer;
	DisabledEffectPath.Batches[0].bEffectRequested = false;
	// 既定引数falseでflagなし命令を確認する記録buffer。
	FDrawBuffer DefaultCullingBuffer;
	// 旧flags=0の確認命令を書き込むcontext。
	FDrawContext DefaultCullingDraw( DefaultCullingBuffer );
	DefaultCullingDraw.MaterialMesh3D( TransformHeader, LocalBuild.Vertices.GetData(), LocalBuild.Indices.GetData() );
	// 既定の3引数APIが旧flags=0を保つか。
	if ( !DefaultCullingBuffer.Words.empty() ) ForEachDrawCommand( FDrawSpan{ DefaultCullingBuffer.Words.data(), static_cast<int32>( DefaultCullingBuffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::MaterialMesh3D ) bDefaultCullingFlagClear = !Command.Has( FDrawFlag::UseRegularCulling ); } );
	C.Check( bRecordedRegularCulling && bDefaultCullingFlagClear, "3b meshは通常culling flagを付け、既定APIは旧flags=0を保つ" );
	FDrawBuffer StoppedEffectBuffer;
	FDrawContext StoppedEffectDraw( StoppedEffectBuffer );
	TransformRenderer.Record( StoppedEffectDraw, FTransform::Identity );
	FDrawBuffer DisabledEffectBuffer; FDrawContext DisabledEffectDraw( DisabledEffectBuffer ); DisabledEffectPath.Record( DisabledEffectDraw, FTransform::Identity );
	C.Check( !StoppedEffectBuffer.Words.empty() && StoppedEffectBuffer.Words == DisabledEffectBuffer.Words, "停止とEffect無効の両経路は同じlegacy Mesh3D命令byteを記録する" );

	// 正面向きと負scaleで表裏を反転し、material shaderとlegacy meshの実表示を比べる。
	FEditMesh OneSidedEffectMesh = TransformMesh;
	OneSidedEffectMesh.Materials[0].bTwoSided = false;
	FMeshRenderer OneSidedEffect = NWorld::MakeMeshRenderer( OneSidedEffectMesh, LoadTexture );
	FMeshRenderer TwoSidedEffect = NWorld::MakeMeshRenderer( TransformMesh, LoadTexture );
	FEditMesh LegacyOneSidedMesh = TransformMesh;
	LegacyOneSidedMesh.Materials[0].Effect.Material.bEnabled = false;
	LegacyOneSidedMesh.Materials[0].bTwoSided = false;
	FMeshRenderer LegacyOneSided = NWorld::MakeMeshRenderer( LegacyOneSidedMesh, LoadTexture );
	FEditMesh LegacyTwoSidedMesh = LegacyOneSidedMesh;
	LegacyTwoSidedMesh.Materials[0].bTwoSided = true;
	FMeshRenderer LegacyTwoSided = NWorld::MakeMeshRenderer( LegacyTwoSidedMesh, LoadTexture );
	const FTransform PositiveScale = FTransform::Identity;
	const FTransform NegativeScale( FVector3::Zero, FQuaternion::Identity, FVector3( -1.0f, 1.0f, 1.0f ) );
	SetUseBackCulling( DX_CULLING_LEFT );
	// test用raster stateを作るnative device。
	ID3D11Device* NativeDevice = const_cast<ID3D11Device*>( static_cast<const ID3D11Device*>( GetUseDirect3D11Device() ) );
	// 実行前後のnative rasterを調べるcontext。
	ID3D11DeviceContext* NativeContext = const_cast<ID3D11DeviceContext*>( static_cast<const ID3D11DeviceContext*>( GetUseDirect3D11DeviceContext() ) );
	// このtest区間より前のnative raster。
	ID3D11RasterizerState* PreviousRaster = nullptr;
	// cullingなしを強制bindするtest用raster。
	ID3D11RasterizerState* NoCullRaster = nullptr;
	// caller descriptorを保ったcullingなし設定。
	D3D11_RASTERIZER_DESC NoCullDesc{};
	if ( NativeContext != nullptr ) NativeContext->RSGetState( &PreviousRaster );
	if ( PreviousRaster != nullptr ) PreviousRaster->GetDesc( &NoCullDesc ); else { NoCullDesc.FillMode = D3D11_FILL_SOLID; NoCullDesc.DepthClipEnable = TRUE; }
	NoCullDesc.CullMode = D3D11_CULL_NONE;
	// test用rasterの作成結果。
	const HRESULT NoCullResult = NativeDevice != nullptr ? NativeDevice->CreateRasterizerState( &NoCullDesc, &NoCullRaster ) : E_FAIL;
	// device、context、sentinel rasterが揃ったか。
	const bool bNativeCullingReady = NativeDevice != nullptr && NativeContext != nullptr && SUCCEEDED( NoCullResult ) && NoCullRaster != nullptr;
	C.Check( bNativeCullingReady && NoCullDesc.CullMode == D3D11_CULL_NONE && GetUseBackCulling() == DX_CULLING_LEFT, "native raster NONEと論理LEFTを同時に用意する (HRESULT=0x%08X)", static_cast<unsigned int>( NoCullResult ) );
	// 最初の描画直前・直後にsentinelがbindされていたか。
	bool bNoCullBoundBeforeDraw = false;
	bool bNoCullRestoredAfterDraw = false;
	// LEFT設定で正scaleのone-sided shaderが描画まで成功したか。
	const bool bEffectFrontDrawn = RenderMeshMaterial( OneSidedEffect, Executor, Screen, Soft, EDrawBlend::Alpha, PositiveScale, NoCullRaster, &bNoCullBoundBeforeDraw, &bNoCullRestoredAfterDraw );
	// 描画後の中心画素と既知背景との差。
	const int32 EffectFrontDistance = MeshMaterialColorDistance( MeshMaterialPixel( Soft, 64, 64 ), KnownBackground );
	C.Check( bNativeCullingReady && bNoCullBoundBeforeDraw && bNoCullRestoredAfterDraw, "最初のone-sided mesh直前はnative NONEで、draw後に同じnative rasterへ復帰する: bound=%d restored=%d", bNoCullBoundBeforeDraw, bNoCullRestoredAfterDraw );
	// mirror描画の直前・直後にsentinelがbindされていたか。
	bool bMirrorNoCullBoundBeforeDraw = false;
	bool bMirrorNoCullRestoredAfterDraw = false;
	const bool bEffectMirrorDrawn = RenderMeshMaterial( OneSidedEffect, Executor, Screen, Soft, EDrawBlend::Alpha, NegativeScale, NoCullRaster, &bMirrorNoCullBoundBeforeDraw, &bMirrorNoCullRestoredAfterDraw );
	// 負scale描画後の中心画素と既知背景との差。
	const int32 EffectMirrorDistance = MeshMaterialColorDistance( MeshMaterialPixel( Soft, 64, 64 ), KnownBackground );
	C.Check( bMirrorNoCullBoundBeforeDraw && bMirrorNoCullRestoredAfterDraw, "mirror one-sided drawもnative NONEから始まり同じrasterへ復帰する: bound=%d restored=%d", bMirrorNoCullBoundBeforeDraw, bMirrorNoCullRestoredAfterDraw );
	// two-sided shaderの負scale描画に成功したか。
	const bool bEffectTwoSidedMirrorDrawn = RenderMeshMaterial( TwoSidedEffect, Executor, Screen, Soft, EDrawBlend::Alpha, NegativeScale );
	// 描画後の中心画素と既知背景との差。
	const int32 EffectTwoSidedMirrorDistance = MeshMaterialColorDistance( MeshMaterialPixel( Soft, 64, 64 ), KnownBackground );
	// legacy片面meshの正負scale描画に成功したか。
	const bool bLegacyFrontDrawn = RenderMeshMaterial( LegacyOneSided, Executor, Screen, Soft, EDrawBlend::Alpha, PositiveScale );
	const int32 LegacyFrontDistance = MeshMaterialColorDistance( MeshMaterialPixel( Soft, 64, 64 ), KnownBackground );
	const bool bLegacyMirrorDrawn = RenderMeshMaterial( LegacyOneSided, Executor, Screen, Soft, EDrawBlend::Alpha, NegativeScale );
	const int32 LegacyMirrorDistance = MeshMaterialColorDistance( MeshMaterialPixel( Soft, 64, 64 ), KnownBackground );
	// legacy両面meshの負scale描画に成功したか。
	const bool bLegacyTwoSidedMirrorDrawn = RenderMeshMaterial( LegacyTwoSided, Executor, Screen, Soft, EDrawBlend::Alpha, NegativeScale );
	const int32 LegacyTwoSidedMirrorDistance = MeshMaterialColorDistance( MeshMaterialPixel( Soft, 64, 64 ), KnownBackground );
	// 中心画素が背景から変化したかで片面表示を判定する。
	const bool bEffectPositiveVisible = EffectFrontDistance > 20;
	const bool bEffectNegativeVisible = EffectMirrorDistance > 20;
	const bool bLegacyPositiveVisible = LegacyFrontDistance > 20;
	const bool bLegacyNegativeVisible = LegacyMirrorDistance > 20;
	C.Check( bEffectFrontDrawn && bEffectMirrorDrawn && bEffectTwoSidedMirrorDrawn && bLegacyFrontDrawn && bLegacyMirrorDrawn && bLegacyTwoSidedMirrorDrawn && bEffectPositiveVisible == bLegacyPositiveVisible && bEffectNegativeVisible == bLegacyNegativeVisible && bEffectPositiveVisible != bEffectNegativeVisible && EffectTwoSidedMirrorDistance > 20 && LegacyTwoSidedMirrorDistance > 20, "初期 winding=0,3,2,1の正負scale可視反転と両面表示がlegacy Mesh3Dに一致: effect=%d,%d,%d legacy=%d,%d,%d", EffectFrontDistance, EffectMirrorDistance, EffectTwoSidedMirrorDistance, LegacyFrontDistance, LegacyMirrorDistance, LegacyTwoSidedMirrorDistance );
	SetUseBackCulling( DX_CULLING_RIGHT );
	// RIGHT設定の正負scale描画に成功したか。
	const bool bRightPositiveDrawn = RenderMeshMaterial( OneSidedEffect, Executor, Screen, Soft, EDrawBlend::Alpha, PositiveScale, NoCullRaster );
	// logical RIGHTの正scale中心画素と背景の距離。
	const int32 RightPositiveDistance = MeshMaterialColorDistance( MeshMaterialPixel( Soft, 64, 64 ), KnownBackground );
	const bool bRightNegativeDrawn = RenderMeshMaterial( OneSidedEffect, Executor, Screen, Soft, EDrawBlend::Alpha, NegativeScale, NoCullRaster );
	// logical RIGHTの負scale中心画素と背景の距離。
	const int32 RightNegativeDistance = MeshMaterialColorDistance( MeshMaterialPixel( Soft, 64, 64 ), KnownBackground );
	SetUseBackCulling( DX_CULLING_NONE );
	// NONE設定の正負scale描画に成功したか。
	const bool bNoCullPositiveDrawn = RenderMeshMaterial( OneSidedEffect, Executor, Screen, Soft, EDrawBlend::Alpha, PositiveScale, NoCullRaster );
	// logical NONEの正scale中心画素と背景の距離。
	const int32 NoCullPositiveDistance = MeshMaterialColorDistance( MeshMaterialPixel( Soft, 64, 64 ), KnownBackground );
	const bool bNoCullNegativeDrawn = RenderMeshMaterial( OneSidedEffect, Executor, Screen, Soft, EDrawBlend::Alpha, NegativeScale, NoCullRaster );
	// logical NONEの負scale中心画素と背景の距離。
	const int32 NoCullNegativeDistance = MeshMaterialColorDistance( MeshMaterialPixel( Soft, 64, 64 ), KnownBackground );
	C.Check( bRightPositiveDrawn && bRightNegativeDrawn && ( RightPositiveDistance > 20 ) == bEffectNegativeVisible && ( RightNegativeDistance > 20 ) == bEffectPositiveVisible && ( RightPositiveDistance > 20 ) != ( RightNegativeDistance > 20 ), "logical RIGHTはLEFTと逆向きの表裏をnative NONEから選ぶ: right=%d,%d left=%d,%d", RightPositiveDistance, RightNegativeDistance, EffectFrontDistance, EffectMirrorDistance );
	C.Check( bNoCullPositiveDrawn && bNoCullNegativeDrawn && NoCullPositiveDistance > 20 && NoCullNegativeDistance > 20, "logical NONEは正負scaleの両面をnative NONEから描く: distance=%d,%d", NoCullPositiveDistance, NoCullNegativeDistance );
	if ( NativeContext != nullptr ) NativeContext->RSSetState( PreviousRaster );
	if ( NoCullRaster != nullptr ) NoCullRaster->Release();
	if ( PreviousRaster != nullptr ) PreviousRaster->Release();

	SetDrawScreen( Saved.Screen ); DeleteSoftImage( Soft ); DeleteGraph( Screen );
	CleanBase = FMeshRenderer(); EmissionTwo = FMeshRenderer(); UvBase = FMeshRenderer(); UvMoved = FMeshRenderer(); DistortionZero = FMeshRenderer(); DistortionOne = FMeshRenderer(); NoiseBase = FMeshRenderer(); NoiseMoved = FMeshRenderer(); OpacityOne = FMeshRenderer(); OpacityHalf = FMeshRenderer(); AddRenderer = FMeshRenderer(); MaskRenderer = FMeshRenderer(); EffectThenPlain = FMeshRenderer(); PlainControl = FMeshRenderer(); TransformRenderer = FMeshRenderer(); OneSidedRenderer = FMeshRenderer(); OneSidedEffect = FMeshRenderer(); TwoSidedEffect = FMeshRenderer(); LegacyOneSided = FMeshRenderer(); LegacyTwoSided = FMeshRenderer();
	Assets.ReleaseUnused(); Assets.Tick( 0.0f ); Assets.SetRootDirectory( PreviousRoot );
}
