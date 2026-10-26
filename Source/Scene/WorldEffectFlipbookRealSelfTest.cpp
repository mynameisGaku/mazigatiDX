// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Effect/ParticleEffectBackend.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/FileSystem.h"

namespace
{
	// real testで読み取った画面中心と、指定色のpixel数を保持する。
	struct FFlipbookPixelResult
	{
		// quad中心に出た合成後の色。
		FColor32 Center{};
		// 背景から変わった画像pixel数。
		int32 LitPixels = 0;
		// 選んだコマ以外の主色が混ざったpixel数。
		int32 UnexpectedPixels = 0;
		// dissolve穴、境界、表示側の色を読む独立pixel。
		FColor32 MaskHole{};
		FColor32 MaskEdge{};
		FColor32 MaskAlive{};
		// 元画像alphaが0の位置で、材質が色を復活させないか調べるpixel。
		FColor32 TransparentSource{};
	};

	// 3色を横に並べた無圧縮TGA atlasを作る。
	std::vector<uint8> MakeFlipbookRealAtlas()
	{
		// TGAヘッダーと12x4x32bitの格納byte列。
		std::vector<uint8> Bytes = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 0, 4, 0, 32, 0x28 };
		for ( int32 Y = 0; Y < 4; ++Y ) for ( int32 X = 0; X < 12; ++X )
		{
			// 赤・緑・青をコマ境界で切り替え、隣コマ混入を識別する。
			const uint8 Colors[3][3] = { { 240, 24, 16 }, { 16, 232, 32 }, { 24, 48, 240 } };
			const int32 Frame = X / 4;
			// green frame左上の2x2だけ画像alphaを0にする。
			const bool bTransparentCorner = Frame == 1 && X % 4 < 2 && Y < 2;
			Bytes.push_back( Colors[Frame][2] ); Bytes.push_back( Colors[Frame][1] ); Bytes.push_back( Colors[Frame][0] ); Bytes.push_back( bTransparentCorner ? 0 : 255 );
		}
		return Bytes;
	}

	// 左右へ連続変化する赤成分を持つ8x8消失マスクを作る。
	std::vector<uint8> MakeFlipbookMaskTga()
	{
		// TGAヘッダーと8x8x32bitのpixelを組み立てるbyte列。
		std::vector<uint8> Bytes = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 8, 0, 32, 0x28 };
		for ( int32 Y = 0; Y < 8; ++Y ) for ( int32 X = 0; X < 8; ++X )
		{
			// mask shaderはRを読むため、横方向だけ赤成分を増やす。
			const uint8 Mask = static_cast<uint8>( X * 255 / 7 );
			Bytes.push_back( 0 ); Bytes.push_back( 0 ); Bytes.push_back( Mask ); Bytes.push_back( 255 );
		}
		return Bytes;
	}

	// 呼び出し元の画面と論理カメラ設定を保存して復元する。
	struct FFlipbookCallerState
	{
		// 描画前の画面とscreen切替時の自動設定。
		int32 Screen = GetDrawScreen();
		int32 ScreenReset = GetUseSetDrawScreenSettingReset();
		// 呼び出し前のcameraと四つの変換行列。
		MATRIX CameraView = GetCameraViewMatrix();
		MATRIX CameraProjection = GetCameraProjectionMatrix();
		MATRIX World{};
		MATRIX View{};
		MATRIX Projection{};
		MATRIX Viewport{};
		// 呼び出し前のnear/far、画面中心、領域、blend、明るさ、背景。
		float32 Near = GetCameraNear();
		float32 Far = GetCameraFar();
		float32 Aspect = GetCameraDotAspect();
		float32 Center[2] = {};
		RECT Area{};
		int32 Blend[2] = {};
		int32 Bright[3] = {};
		int32 Background[4] = {};

		// 呼び出し前の全ての変更対象を保存する。
		FFlipbookCallerState()
		{
			GetTransformToWorldMatrix( &World ); GetTransformToViewMatrix( &View ); GetTransformToProjectionMatrix( &Projection ); GetTransformToViewportMatrix( &Viewport );
			GetCameraScreenCenter( &Center[0], &Center[1] ); GetDrawArea( &Area ); GetDrawBlendMode( &Blend[0], &Blend[1] ); GetDrawBright( &Bright[0], &Bright[1], &Bright[2] ); GetBackgroundColor( &Background[0], &Background[1], &Background[2], &Background[3] );
			SetUseSetDrawScreenSettingReset( FALSE );
		}

		// GPU testが変更した画面と論理camera状態を元へ戻す。
		~FFlipbookCallerState()
		{
			RenderVertex(); SetDrawScreen( Screen ); SetCameraNearFar( Near, Far ); SetCameraDotAspect( Aspect ); SetCameraScreenCenter( Center[0], Center[1] ); SetCameraViewMatrix( CameraView ); SetupCamera_ProjectionMatrix( CameraProjection );
			SetTransformToWorld( &World ); SetTransformToView( &View ); SetTransformToProjection( &Projection ); SetTransformToViewport( &Viewport ); SetDrawArea( Area.left, Area.top, Area.right, Area.bottom ); SetDrawBlendMode( Blend[0], Blend[1] ); SetDrawBright( Bright[0], Bright[1], Bright[2] ); SetBackgroundColor( Background[0], Background[1], Background[2], Background[3] ); SetUseSetDrawScreenSettingReset( ScreenReset );
		}
	};

	// キャッシュ済みpolygon UVを使い、通常Quadで派生画像中央の色を読む。
	FColor32 RenderRegionCenter( const TSharedPtr<CTexture>& Region, IDrawExecutor& Executor, int32 Screen, int32 Soft )
	{
		if ( !Region.IsValid() || !Region->IsReady() ) return FColor32( 0, 0, 0, 0 );
		SetDrawScreen( Screen ); SetBackgroundColor( 20, 20, 20 ); ClearDrawScreen(); SetCameraNearFar( 0.1f, 100.0f ); SetupCamera_Ortho( 8.0f ); SetCameraPositionAndTarget_UpVecY( VGet( 0.0f, 0.0f, -10.0f ), VGet( 0.0f, 0.0f, 0.0f ) ); SetCameraDotAspect( 1.0f ); SetCameraScreenCenter( 64.0f, 64.0f ); SetDrawAreaFull();
		// 128x128画面の中心へ単位worldのquadを置く論理viewport。
		MATRIX Viewport{};
		CreateViewportMatrix( &Viewport, 64.0f, 64.0f, 128.0f, 128.0f ); SetTransformToViewport( &Viewport );
		// 呼出し元worldと明るさを除き、既知の材質なしQuad条件を作る。
		const MATRIX IdentityWorld = MGetIdent();
		SetTransformToWorld( &IdentityWorld ); SetDrawBright( 255, 255, 255 );
		// 通常Quadへ渡す派生元画像上のUV境界。
		const float32 ( &Uv )[4] = Region->GetPolygonUvBounds();
		// 一pixel領域でも通常経路で描画できる四角形。
		FDrawQuad3DData Quad;
		Quad.Pos[0] = 0.0f; Quad.Pos[1] = 0.0f; Quad.Pos[2] = 0.0f; Quad.Width = 3.0f; Quad.Height = 3.0f; Quad.Handle = Region->GetHandle(); Quad.U0 = Uv[0]; Quad.V0 = Uv[1]; Quad.U1 = Uv[2]; Quad.V1 = Uv[3];
		// Quad命令を記録するdraw context。
		FDrawBuffer Buffer;
		// 通常のalpha合成を指定する描画窓口。
		FDrawContext Draw( Buffer );
		Draw.Blend( EDrawBlend::Alpha ); Draw.Quad3D( Quad );
		Executor.Execute( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		// screen中央に合成されたpixel。
		int32 R = 0, G = 0, B = 0, A = 0;
		GetPixelSoftImage( Soft, 64, 64, &R, &G, &B, &A );
		return FColor32( static_cast<uint8>( R ), static_cast<uint8>( G ), static_cast<uint8>( B ), static_cast<uint8>( A ) );
	}

	// 指定時刻の粒子を実simulation・record・executorへ通し画素を読む。
	FFlipbookPixelResult RenderFlipbookAge( const FParticleEmitterDefinition& Emitter, float32 Age, CAssetManager& Assets, IDrawExecutor& Executor, int32 Screen, int32 Soft, bool bMaterial )
	{
		// 比較用の実粒子を再現可能な定義から作る。
		FParticleEmitterDefinition Case = Emitter;
		Case.Material.bEnabled = bMaterial;
		TSharedPtr<IEffectPlayback> Playback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { Case } ), &Assets, FEffectPlayParams(), FTransform::Identity, bMaterial ? "flipbook-real-material" : "flipbook-real-legacy" );
		if ( !Playback.IsValid() ) return FFlipbookPixelResult();
		Playback->Update( 0.01f );
		Playback->Update( Age );
		SetDrawScreen( Screen ); SetBackgroundColor( 20, 20, 20 ); ClearDrawScreen(); SetCameraNearFar( 0.1f, 100.0f ); SetupCamera_Ortho( 8.0f ); SetCameraPositionAndTarget_UpVecY( VGet( 0.0f, 0.0f, -10.0f ), VGet( 0.0f, 0.0f, 0.0f ) ); SetCameraDotAspect( 1.0f ); SetCameraScreenCenter( 64.0f, 64.0f ); SetDrawAreaFull();
		// 粒子の座標を128x128screen中央へ写す論理viewport。
		MATRIX Viewport{};
		CreateViewportMatrix( &Viewport, 64.0f, 64.0f, 128.0f, 128.0f ); SetTransformToViewport( &Viewport );
		// テストで呼出側worldの影響を取り除く単位行列。
		const MATRIX IdentityWorld = MGetIdent();
		SetTransformToWorld( &IdentityWorld ); SetDrawBright( 255, 255, 255 );
		// simulationが選んだframeを保持した命令buffer。
		FDrawBuffer Buffer;
		// 命令記録を行うdraw context。
		FDrawContext Draw( Buffer );
		Draw.Blend( EDrawBlend::Alpha );
		Playback->Record( Draw );
		if ( !Buffer.Words.empty() ) Executor.Execute( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
		RenderVertex();
		GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		// 指定soft image中心の合成色。
		FFlipbookPixelResult Result;
		int32 R = 0, G = 0, B = 0, A = 0;
		GetPixelSoftImage( Soft, 64, 64, &R, &G, &B, &A );
		Result.Center = FColor32( static_cast<uint8>( R ), static_cast<uint8>( G ), static_cast<uint8>( B ), static_cast<uint8>( A ) );
		GetPixelSoftImage( Soft, 50, 64, &R, &G, &B, &A );
		Result.MaskHole = FColor32( static_cast<uint8>( R ), static_cast<uint8>( G ), static_cast<uint8>( B ), static_cast<uint8>( A ) );
		GetPixelSoftImage( Soft, 66, 64, &R, &G, &B, &A );
		Result.MaskEdge = FColor32( static_cast<uint8>( R ), static_cast<uint8>( G ), static_cast<uint8>( B ), static_cast<uint8>( A ) );
		GetPixelSoftImage( Soft, 78, 64, &R, &G, &B, &A );
		Result.MaskAlive = FColor32( static_cast<uint8>( R ), static_cast<uint8>( G ), static_cast<uint8>( B ), static_cast<uint8>( A ) );
		GetPixelSoftImage( Soft, 43, 43, &R, &G, &B, &A );
		Result.TransparentSource = FColor32( static_cast<uint8>( R ), static_cast<uint8>( G ), static_cast<uint8>( B ), static_cast<uint8>( A ) );
		for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X )
		{
			// 背景色から十分変わったquad pixelを数える。
			GetPixelSoftImage( Soft, X, Y, &R, &G, &B, &A );
			if ( NMath::Abs( R - 20 ) + NMath::Abs( G - 20 ) + NMath::Abs( B - 20 ) > 20 )
			{
				++Result.LitPixels;
				// atlas各コマの非主色上限を越えたpixelを隣コマ混入として数える。
				if ( Age < 0.5f ? ( R > 35 || B > 48 ) : ( R > 40 || G > 70 ) ) ++Result.UnexpectedPixels;
			}
		}
		Playback.Reset();
		return Result;
	}
}

// 実アトラスのコマ選択を、材質ON/OFFの両方でGPU readbackから確認する。
void NWorldTest::RunEffectFlipbookRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Dir )
{
	using namespace NEffectTest;
	C.Group = "effect-flipbook-real-gpu";
	// 呼出し元が使うアセットrootを保存してからsandboxへ切り替える。
	const FString PreviousRoot = Assets.GetRootDirectory();
	Assets.SetRootDirectory( Dir );
	// camera、screen、blend、背景などを関数終了時に戻す。
	FFlipbookCallerState CallerState;
	C.Check( Executor.SupportsEffectMaterials(), "通常のD3D11実機で材質付きflipbookを描ける" );
	if ( !Executor.SupportsEffectMaterials() ) { Assets.SetRootDirectory( PreviousRoot ); return; }
	// 画像とsheet metadataをtest sandboxへ作る。
	const FString AtlasPath = "flipbook-real/atlas.tga";
	// 材質の消失境界をGPUで確認するmask画像。
	const FString MaskPath = "flipbook-real/mask-gradient.tga";
	const FString SheetPath = "flipbook-real/atlas.sheet.json";
	// atlas用folderを作成できたか。
	const bool bDirectoryReady = NFileSystem::EnsureDirectory( Dir / "flipbook-real" );
	// コマ境界が赤・緑・青になる入力画像。
	const std::vector<uint8> AtlasBytes = MakeFlipbookRealAtlas();
	C.Check( bDirectoryReady && NAssetTest::WriteFileBytes( Dir / AtlasPath, AtlasBytes ), "GPU flipbook atlasをsandboxへ保存する" );
	// 境界位置を独立に決められる横方向mask画像。
	const std::vector<uint8> MaskBytes = MakeFlipbookMaskTga();
	C.Check( bDirectoryReady && NAssetTest::WriteFileBytes( Dir / MaskPath, MaskBytes ), "dissolve用gradient maskをsandboxへ保存する" );
	FSpriteSheetDefinition SheetDefinition;
	SheetDefinition.TexturePath = AtlasPath;
	SheetDefinition.Frames.Add( { 0, 0, 4, 4, 2.0f, 2.0f } );
	SheetDefinition.Frames.Add( { 4, 0, 4, 4, 2.0f, 2.0f } );
	SheetDefinition.Frames.Add( { 8, 0, 4, 4, 2.0f, 2.0f } );
	FSpriteClip Clip = FSpriteClip::FromFrames( "Cycle", { 0, 1, 2 }, 10.0f, EAnimLoopMode::Loop );
	Clip.Durations = { 0.1f, 0.2f, 0.3f };
	SheetDefinition.Clips.Add( Clip );
	C.Check( Assets.SaveSpriteSheetDefinition( SheetPath, SheetDefinition ), "GPU flipbook sheet定義を保存する" );
	// DxLib screenと全pixel取得に使うsoft image。
	const int32 Screen = MakeScreen( 128, 128, TRUE );
	const int32 Soft = MakeARGB8ColorSoftImage( 128, 128 );
	C.Check( Screen >= 0 && Soft >= 0, "flipbook実機readback用のscreenとsoft imageを作る" );
	if ( Screen < 0 || Soft < 0 ) { RenderVertex(); SetDrawScreen( CallerState.Screen ); if ( Screen >= 0 ) DeleteGraph( Screen ); if ( Soft >= 0 ) DeleteSoftImage( Soft ); Assets.SetRootDirectory( PreviousRoot ); return; }
	// 既存の画像欄を別pathにしてもflipbook atlasが描画元となる粒子。
	FParticleEmitterDefinition Emitter = QuietEmitter();
	Emitter.Texture = "flipbook-real/ignored.png";
	Emitter.Lifetime = { 1.0f, 1.0f };
	Emitter.StartSize = { 3.0f, 3.0f };
	Emitter.EndSize = { 3.0f, 3.0f };
	Emitter.Blend = EParticleBlend::Alpha;
	Emitter.StartColor = Emitter.EndColor = FColor32( 255, 255, 255, 255 );
	Emitter.Bursts.Add( Burst( 0.0f, 1 ) );
	Emitter.Flipbook.bEnabled = true;
	Emitter.Flipbook.Sheet = SheetPath;
	Emitter.Flipbook.Clip = "Cycle";
	Emitter.Flipbook.TimeMode = FEffectFlipbookDefinition::ETimeMode::Lifetime;
	Emitter.Material.UVScale = FVector2( 2.25f, 1.75f );
	Emitter.Material.UVScroll = FVector2( -1.25f, 0.75f );
	Emitter.Material.bDissolve = false;
	Emitter.Material.EdgeWidth = 0.0f;
	// 寿命の四分の一は緑、四分の三は青となり、隣コマの色を混ぜない。
	for ( int32 MaterialIndex = 0; MaterialIndex < 2; ++MaterialIndex ) for ( int32 Phase = 0; Phase < 2; ++Phase )
	{
		// 粒子時計・素材設定以外を同じにした独立readback。
		const FFlipbookPixelResult Pixel = RenderFlipbookAge( Emitter, Phase == 0 ? 0.25f : 0.75f, Assets, Executor, Screen, Soft, MaterialIndex != 0 );
		C.Check( Pixel.LitPixels > 100, "simulationからGPUへ選ばれた色のpixelが出る (material %d phase %d, count %d)", MaterialIndex, Phase, Pixel.LitPixels );
		C.Check( Pixel.UnexpectedPixels == 0, "非整数UVscale・負scroll・repeatでも隣のコマ色が混ざらない (material %d phase %d, unexpected %d)", MaterialIndex, Phase, Pixel.UnexpectedPixels );
		if ( Phase == 0 ) C.Check( Pixel.Center.g > 150 && Pixel.Center.r < 80 && Pixel.Center.b < 80, "寿命四分の一で緑frameだけを表示する (got %u,%u,%u)", Pixel.Center.r, Pixel.Center.g, Pixel.Center.b );
		else C.Check( Pixel.Center.b > 150 && Pixel.Center.r < 80 && Pixel.Center.g < 100, "寿命四分の三で青frameだけを表示する (got %u,%u,%u)", Pixel.Center.r, Pixel.Center.g, Pixel.Center.b );
		// selftest用sandboxの保存領域に実機の比較画像を残す。
		const FString ProofDirectory = FString( "Saved" ) / "EffectFlipbookProof";
		const bool bProofDirectoryReady = NFileSystem::EnsureDirectory( ProofDirectory );
		const FString ProofName = FString::Printf( "flipbook-%s-%s.png", MaterialIndex != 0 ? "material" : "legacy", Phase == 0 ? "green" : "blue" );
		const bool bProofSaved = bProofDirectoryReady && SaveDrawScreenToPNG( 0, 0, 128, 128, *( ProofDirectory / ProofName ) ) == 0;
		C.Check( bProofSaved, "flipbook比較画面をsandboxへ保存する (%s)", *ProofName );
	}
	// green frameのalpha0部分は、強いEmissionを指定しても不透明色へ戻らない。
	FParticleEmitterDefinition TransparentEmitter = Emitter;
	TransparentEmitter.Material.bEnabled = true;
	TransparentEmitter.Material.bDissolve = false;
	TransparentEmitter.Material.UVScale = FVector2( 1.0f, 1.0f );
	TransparentEmitter.Material.UVScroll = FVector2( 0.0f, 0.0f );
	TransparentEmitter.Material.EdgeWidth = 0.0f;
	TransparentEmitter.Material.Emission.Keys = { { 0.0f, 32.0f } };
	const FFlipbookPixelResult TransparentPixel = RenderFlipbookAge( TransparentEmitter, 0.25f, Assets, Executor, Screen, Soft, true );
	C.Check( TransparentPixel.Center.g > 80 && TransparentPixel.TransparentSource.r <= 24 && TransparentPixel.TransparentSource.g <= 24 && TransparentPixel.TransparentSource.b <= 24, "alpha0の元画像pixelは強いEmissionでも背景のまま残る (center %u,%u,%u, alpha0 %u,%u,%u)", TransparentPixel.Center.r, TransparentPixel.Center.g, TransparentPixel.Center.b, TransparentPixel.TransparentSource.r, TransparentPixel.TransparentSource.g, TransparentPixel.TransparentSource.b );
	// 寿命25%時にmaskの左側を消し、境界だけ緑へ発光させる。
	FParticleEmitterDefinition DissolveEmitter = Emitter;
	DissolveEmitter.Material.bEnabled = true;
	DissolveEmitter.Material.bDissolve = true;
	DissolveEmitter.Material.MaskTexture = MaskPath;
	DissolveEmitter.Material.UVScale = FVector2( 1.0f, 1.0f );
	DissolveEmitter.Material.UVScroll = FVector2( 0.0f, 0.0f );
	DissolveEmitter.Material.Dissolve.Keys = { { 0.0f, 0.5f } };
	DissolveEmitter.Material.Emission.Keys = { { 0.0f, 0.25f } };
	DissolveEmitter.Material.EdgeWidth = 0.25f;
	DissolveEmitter.Material.EdgeEmission = 2.0f;
	DissolveEmitter.Material.EdgeColor = FColor32( 32, 255, 32, 255 );
	// 境界と両側の画素を同じ実材質描画から読む。
	const FFlipbookPixelResult DissolvePixel = RenderFlipbookAge( DissolveEmitter, 0.25f, Assets, Executor, Screen, Soft, true );
	C.Check( DissolvePixel.LitPixels > 100, "実材質で消失境界の両側に画素が残る (count %d)", DissolvePixel.LitPixels );
	C.Check( DissolvePixel.MaskHole.r <= 24 && DissolvePixel.MaskHole.g <= 24 && DissolvePixel.MaskHole.b <= 24, "既知mask値がしきい値未満の側を実際に消す (got %u,%u,%u)", DissolvePixel.MaskHole.r, DissolvePixel.MaskHole.g, DissolvePixel.MaskHole.b );
	C.Check( DissolvePixel.MaskAlive.g > 35 && DissolvePixel.MaskEdge.g > DissolvePixel.MaskAlive.g + 40, "消失境界は生存画素より明るい緑で発光する (edge %u,%u,%u alive %u,%u,%u)", DissolvePixel.MaskEdge.r, DissolvePixel.MaskEdge.g, DissolvePixel.MaskEdge.b, DissolvePixel.MaskAlive.r, DissolvePixel.MaskAlive.g, DissolvePixel.MaskAlive.b );
	// GPU上の穴と境界の実画面を専用sandboxへ残す。
	const FString DissolveProofDirectory = FString( "Saved" ) / "EffectFlipbookProof";
	// 穴と境界を比較する画像path。
	const FString DissolveProofPath = DissolveProofDirectory / "flipbook-dissolve-edge.png";
	const bool bDissolveProofSaved = NFileSystem::EnsureDirectory( DissolveProofDirectory ) && SaveDrawScreenToPNG( 0, 0, 128, 128, *DissolveProofPath ) == 0;
	C.Check( bDissolveProofSaved, "穴と発光境界の実画面をsandboxへ保存する" );
	// NPOT atlas上の緑一pixelと、その子regionを通常Quadで連続確認する。
	// 12x4元atlasを共有するtextureアセット。
	TSharedPtr<CTexture> AtlasTexture = Assets.LoadTexture( AtlasPath, false );
	// atlas上の既知緑pixelを含む一pixel領域。
	TSharedPtr<CTexture> OnePixelRegion = AtlasTexture.IsValid() ? Assets.CreateTextureRegion( AtlasTexture, 6, 2, 1, 1 ) : TSharedPtr<CTexture>();
	// 一pixel領域から作った入れ子の派生画像。
	TSharedPtr<CTexture> NestedOnePixelRegion = OnePixelRegion.IsValid() ? Assets.CreateTextureRegion( OnePixelRegion, 0, 0, 1, 1 ) : TSharedPtr<CTexture>();
	// 通常Quad経路で取得した元領域の中央色。
	const FColor32 OnePixelColor = RenderRegionCenter( OnePixelRegion, Executor, Screen, Soft );
	// 通常Quad経路で取得したnested領域の中央色。
	const FColor32 NestedOnePixelColor = RenderRegionCenter( NestedOnePixelRegion, Executor, Screen, Soft );
	C.Check( OnePixelRegion.IsValid() && NestedOnePixelRegion.IsValid() && OnePixelColor.g > 180 && OnePixelColor.r < 40 && OnePixelColor.b < 50 && NestedOnePixelColor.g > 180 && NestedOnePixelColor.r < 40 && NestedOnePixelColor.b < 50, "NPOT画像の1pixel regionとnested regionは通常Quadで同じ緑pixelを描く (first %u,%u,%u nested %u,%u,%u)", OnePixelColor.r, OnePixelColor.g, OnePixelColor.b, NestedOnePixelColor.r, NestedOnePixelColor.g, NestedOnePixelColor.b );
	// readbackに使ったDxLib資源を解放し、AssetManager rootを戻す。
	NestedOnePixelRegion.Reset(); OnePixelRegion.Reset(); AtlasTexture.Reset();
	RenderVertex(); SetDrawScreen( CallerState.Screen ); DeleteSoftImage( Soft ); DeleteGraph( Screen ); Assets.SetRootDirectory( PreviousRoot );
}
