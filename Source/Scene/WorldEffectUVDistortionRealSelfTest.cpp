// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"

#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <limits>
#include <vector>

#include "Library/Core/Interfaces/Render/DrawDistortedMaterialQuad.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/Effect/ParticleEffectBackend.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/DxLib/EffectMaterialRenderer.h"
#include "Library/Platform/FileSystem.h"

namespace
{
	// DxLibが同期読込できる無圧縮TGAのheaderを返す。
	std::vector<uint8> MakeTgaHeader( int32 Width, int32 Height )
	{
		return { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, static_cast<uint8>( Width ), 0, static_cast<uint8>( Height ), 0, 32, 0x28 };
	}

	// 四象限の色を分け、横・縦方向の変位を独立に判定できるTGAを作る。
	std::vector<uint8> MakeRedGreenTga()
	{
		// 32x16画像のTGA byte列。
		std::vector<uint8> Bytes = MakeTgaHeader( 32, 16 );
		for ( int32 Y = 0; Y < 16; ++Y ) for ( int32 X = 0; X < 32; ++X ) { const bool bLeft = X < 16; const bool bTop = Y < 8; const uint8 R = bTop ? ( bLeft ? 240 : 16 ) : ( bLeft ? 16 : 232 ); const uint8 G = bTop ? ( bLeft ? 16 : 232 ) : ( bLeft ? 32 : 220 ); const uint8 B = bTop ? 16 : ( bLeft ? 240 : 16 ); Bytes.push_back( B ); Bytes.push_back( G ); Bytes.push_back( R ); Bytes.push_back( 255 ); }
		return Bytes;
	}

	// 右上のRGBを残したままalphaだけ0にし、画像alphaの穴を確認する。
	std::vector<uint8> MakeAlphaHoleTga()
	{
		// 元の四象限色を保ち、右上だけ透明にしたTGA byte列。
		std::vector<uint8> Bytes = MakeTgaHeader( 32, 16 );
		for ( int32 Y = 0; Y < 16; ++Y ) for ( int32 X = 0; X < 32; ++X ) { const bool bLeft = X < 16; const bool bTop = Y < 8; const uint8 R = bTop ? ( bLeft ? 240 : 16 ) : ( bLeft ? 16 : 232 ); const uint8 G = bTop ? ( bLeft ? 16 : 232 ) : ( bLeft ? 32 : 220 ); const uint8 B = bTop ? 16 : ( bLeft ? 240 : 16 ); const uint8 A = bLeft ? 255 : ( bTop ? 0 : 128 ); Bytes.push_back( B ); Bytes.push_back( G ); Bytes.push_back( R ); Bytes.push_back( A ); }
		return Bytes;
	}

	// 指定したRG方向を全域へ書き込む8x8の変位画像を作る。
	std::vector<uint8> MakeDirectionTga( uint8 R, uint8 G )
	{
		// TGA headerに続ける16bitのRG方向と不透明画素。
		std::vector<uint8> Bytes = MakeTgaHeader( 8, 8 );
		for ( int32 Index = 0; Index < 64; ++Index ) { Bytes.push_back( 0 ); Bytes.push_back( G ); Bytes.push_back( R ); Bytes.push_back( 255 ); }
		return Bytes;
	}

	// 左右で正負Rの方向を切り替え、UVscrollの画素変化を出す画像。
	std::vector<uint8> MakeTimeFlowTga()
	{
		// 8x8のTGA byte列。
		std::vector<uint8> Bytes = MakeTgaHeader( 8, 8 );
		for ( int32 Y = 0; Y < 8; ++Y ) for ( int32 X = 0; X < 8; ++X ) { Bytes.push_back( 0 ); Bytes.push_back( 128 ); Bytes.push_back( X < 4 ? 255 : 0 ); Bytes.push_back( 255 ); }
		return Bytes;
	}

	// 左半分は+X、右半分はUNORM中立値のnested-region fixture。
	std::vector<uint8> MakeNestedNoiseTga()
	{
		// NPOT 10x7画像の左半分へ+X、右半分へ中立値を入れるbyte列。
		std::vector<uint8> Bytes = MakeTgaHeader( 10, 7 );
		for ( int32 Y = 0; Y < 7; ++Y ) for ( int32 X = 0; X < 10; ++X ) { Bytes.push_back( 0 ); Bytes.push_back( 128 ); Bytes.push_back( X < 5 ? 255 : 127 ); Bytes.push_back( 255 ); }
		return Bytes;
	}

	// atlas先頭frame内に赤緑模様、隣frameに禁止色の青を置く。
	std::vector<uint8> MakeFlipbookAtlasTga()
	{
		// 16x8 atlasのTGA byte列。
		std::vector<uint8> Bytes = MakeTgaHeader( 16, 8 );
		for ( int32 Y = 0; Y < 8; ++Y ) for ( int32 X = 0; X < 16; ++X ) { const bool bForbiddenFrame = X >= 8; const bool bRightHalf = X % 8 >= 4; const uint8 R = bForbiddenFrame ? 16 : ( bRightHalf ? 16 : 240 ); const uint8 G = bForbiddenFrame ? 24 : ( bRightHalf ? 232 : 16 ); const uint8 B = bForbiddenFrame ? 240 : 16; Bytes.push_back( B ); Bytes.push_back( G ); Bytes.push_back( R ); Bytes.push_back( 255 ); }
		return Bytes;
	}

	// 左半分を消失側、右半分を生存側にする赤チャンネルmaskを作る。
	std::vector<uint8> MakeSplitMaskTga()
	{
		// 32x16 maskのTGA byte列。
		std::vector<uint8> Bytes = MakeTgaHeader( 32, 16 );
		for ( int32 Y = 0; Y < 16; ++Y ) for ( int32 X = 0; X < 32; ++X ) { const uint8 Value = X < 16 ? 0 : 255; Bytes.push_back( 0 ); Bytes.push_back( Value ); Bytes.push_back( Value ); Bytes.push_back( 255 ); }
		return Bytes;
	}

	// 呼び出し前の画面・camera・blend設定をGPU確認後に戻す。
	struct FDistortionCallerState
	{
		// 呼び出し元が描いていたscreenとcameraの距離範囲。
		int32 Screen = GetDrawScreen();
		// screen切替で自動適用するDxLib設定。
		int32 ScreenReset = GetUseSetDrawScreenSettingReset();
		float32 Near = GetCameraNear();
		float32 Far = GetCameraFar();
		float32 Aspect = GetCameraDotAspect();
		float32 Center[2] = {};
		// SetupCameraが更新するcamera本体の行列。
		MATRIX CameraView = GetCameraViewMatrix();
		MATRIX CameraProjection = GetCameraProjectionMatrix();
		// 描画と投影に使われていた論理行列。
		MATRIX World = MGetIdent();
		MATRIX View = MGetIdent();
		MATRIX Projection = MGetIdent();
		MATRIX Viewport = MGetIdent();
		// 描画領域、混合、明るさ、背景色。
		RECT DrawArea{};
		int32 Blend[2] = {};
		int32 Bright[3] = {};
		int32 Background[4] = {};

		// callerの現在値を保存する。
		FDistortionCallerState()
		{
			GetCameraScreenCenter( &Center[0], &Center[1] ); GetTransformToWorldMatrix( &World ); GetTransformToViewMatrix( &View ); GetTransformToProjectionMatrix( &Projection ); GetTransformToViewportMatrix( &Viewport ); GetDrawArea( &DrawArea ); GetDrawBlendMode( &Blend[0], &Blend[1] ); GetDrawBright( &Bright[0], &Bright[1], &Bright[2] ); GetBackgroundColor( &Background[0], &Background[1], &Background[2], &Background[3] );
			SetUseSetDrawScreenSettingReset( FALSE );
		}

		// このfixtureが変更したcaller描画値を戻す。
		~FDistortionCallerState()
		{
			RenderVertex(); SetDrawScreen( Screen ); SetCameraNearFar( Near, Far ); SetCameraDotAspect( Aspect ); SetCameraScreenCenter( Center[0], Center[1] ); SetCameraViewMatrix( CameraView ); SetupCamera_ProjectionMatrix( CameraProjection ); SetTransformToWorld( &World ); SetTransformToView( &View ); SetTransformToProjection( &Projection ); SetTransformToViewport( &Viewport ); SetDrawArea( DrawArea.left, DrawArea.top, DrawArea.right, DrawArea.bottom ); SetDrawBlendMode( Blend[0], Blend[1] ); SetDrawBright( Bright[0], Bright[1], Bright[2] ); SetBackgroundColor( Background[0], Background[1], Background[2], Background[3] ); SetUseSetDrawScreenSettingReset( ScreenReset );
		}
	};
}

// 実D3D11実行で変位方向、旧命令一致、壊れた直接payloadの拒否を確認する。
void NWorldTest::RunEffectUVDistortionRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Dir )
{
	C.Group = "effect-uv-distortion-real";
	// 呼び出し元のasset rootを実fixture後に復元する。
	const FString PreviousRoot = Assets.GetRootDirectory();
	Assets.SetRootDirectory( Dir );
	// 実Playbackのtexture loadingへ渡すpatternと変位画像のasset path。
	const FString SourcePath = "uv-distortion/source.tga";
	const FString AlphaHoleSourcePath = "uv-distortion/source-alpha-hole.tga";
		const FString NoisePath = "uv-distortion/noise.tga";
		const FString NoiseYPath = "uv-distortion/noise-y.tga";
	const FString NoiseNegativeXPath = "uv-distortion/noise-negative-x.tga";
	const FString NoiseNeutralPath = "uv-distortion/noise-neutral.tga";
	const FString MaskPath = "uv-distortion/mask.tga";
	const FString TimeNoisePath = "uv-distortion/noise-time.tga";
		// sandboxへ作る2枚の独立画像と読み込み先フォルダー。
	const bool bImagesSaved = NFileSystem::EnsureDirectory( Dir / "uv-distortion" ) && NAssetTest::WriteFileBytes( Dir / SourcePath, MakeRedGreenTga() ) && NAssetTest::WriteFileBytes( Dir / AlphaHoleSourcePath, MakeAlphaHoleTga() ) && NAssetTest::WriteFileBytes( Dir / NoisePath, MakeDirectionTga( 255, 128 ) ) && NAssetTest::WriteFileBytes( Dir / NoiseYPath, MakeDirectionTga( 128, 255 ) ) && NAssetTest::WriteFileBytes( Dir / NoiseNegativeXPath, MakeDirectionTga( 0, 128 ) ) && NAssetTest::WriteFileBytes( Dir / NoiseNeutralPath, MakeNestedNoiseTga() ) && NAssetTest::WriteFileBytes( Dir / MaskPath, MakeSplitMaskTga() ) && NAssetTest::WriteFileBytes( Dir / TimeNoisePath, MakeTimeFlowTga() );
	C.Check( bImagesSaved, "GPU fixtureのsourceと変位画像をsandboxへ保存する" );
	if ( !bImagesSaved ) { Assets.SetRootDirectory( PreviousRoot ); return; }
	// 実Playbackに渡すsource、noise、screen、readback image。
	const int32 Screen = MakeScreen( 128, 128, TRUE );
	const int32 Soft = MakeARGB8ColorSoftImage( 128, 128 );
	C.Check( Screen >= 0 && Soft >= 0, "実画素確認用描画先を作る" );
	if ( Screen < 0 || Soft < 0 ) { if ( Screen >= 0 ) DeleteGraph( Screen ); if ( Soft >= 0 ) DeleteSoftImage( Soft ); Assets.SetRootDirectory( PreviousRoot ); return; }
	{
		// DxLibの論理cameraを128x128画面中央へ固定する設定。
		FDistortionCallerState Caller;
		SetDrawScreen( Screen ); SetDrawAreaFull(); SetCameraNearFar( 0.1f, 100.0f ); SetCameraDotAspect( 1.0f ); SetCameraScreenCenter( 64.0f, 64.0f ); SetupCamera_Ortho( 8.0f ); SetCameraPositionAndTarget_UpVecY( VGet( 0.0f, 0.0f, -10.0f ), VGet( 0.0f, 0.0f, 0.0f ) );
		// world座標をscreen中央へ投影するviewportとidentity world。
		MATRIX Viewport{}; CreateViewportMatrix( &Viewport, 64.0f, 64.0f, 128.0f, 128.0f ); SetTransformToViewport( &Viewport ); const MATRIX Identity = MGetIdent(); SetTransformToWorld( &Identity );
		SetDrawBright( 255, 255, 255 ); SetBackgroundColor( 12, 12, 12, 255 );
		// 実シミュレーションとfactoryを通す四角形材質定義。
		FParticleEmitterDefinition Emitter = NEffectTest::QuietEmitter();
		Emitter.Texture = SourcePath; Emitter.StartSize = { 4.0f, 4.0f }; Emitter.EndSize = { 4.0f, 4.0f }; Emitter.Bursts.Add( NEffectTest::Burst( 0.0f, 1 ) ); Emitter.Material.bEnabled = true; Emitter.Material.Distortion.bEnabled = true; Emitter.Material.Distortion.Texture = NoisePath; Emitter.Material.Distortion.UVScale = FVector2( 1.0f, 1.0f ); Emitter.Material.Distortion.Strength.Keys.Add( { 0.0f, 0.35f } ); Emitter.Material.Distortion.Strength.Keys.Add( { 1.0f, 0.35f } ); Emitter.Material.Distortion.UVScroll = FVector2( 0.0f, 0.0f );
		TSharedPtr<IEffectPlayback> Playback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { Emitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-gpu" );
		C.Check( Playback.IsValid(), "実source・noise画像からPlaybackを作る" );
		if ( Playback.IsValid() ) Playback->Update( 0.01f );
		// 別方向を検証するnoise資源を再生外側で保持する。
		TSharedPtr<CTexture> NoiseY = Assets.LoadTexture( NoiseYPath );
		TSharedPtr<CTexture> NoiseNegativeX = Assets.LoadTexture( NoiseNegativeXPath );
		TSharedPtr<CTexture> NoiseParent = Assets.LoadTexture( NoiseNeutralPath );
		TSharedPtr<CTexture> NoiseRegion = NoiseParent.IsValid() ? Assets.CreateTextureRegion( NoiseParent, 5, 0, 5, 7 ) : TSharedPtr<CTexture>();
		TSharedPtr<CTexture> NestedNoise = NoiseRegion.IsValid() ? Assets.CreateTextureRegion( NoiseRegion, 1, 1, 3, 4 ) : TSharedPtr<CTexture>();
		C.Check( NoiseY.IsValid() && NoiseY->IsReady() && NoiseNegativeX.IsValid() && NoiseNegativeX->IsReady() && NestedNoise.IsValid(), "縦・負方向noiseとnested regionを準備する" );
		// 実Playbackの記録結果からnoiseと元画像を含む材質値を取り出す。
		FDrawDistortedMaterialQuadData Distorted{};
		if ( Playback.IsValid() ) { FDrawBuffer Recorded; FDrawContext RecordContext( Recorded ); Playback->Record( RecordContext ); ForEachDrawCommand( FDrawSpan{ Recorded.Words.data(), static_cast<int32>( Recorded.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::DistortedMaterialQuad3D ) Distorted = Command.Read<FDrawDistortedMaterialQuadData>(); } ); }
		// 取得したPlayback命令を描画し、比較側は同じsource材質値を使う。
		const auto Render = [&]( bool bDistorted ) { ClearDrawScreen(); FDrawBuffer Buffer; FDrawContext Draw( Buffer ); Draw.Blend( EDrawBlend::Alpha, 255 ); if ( bDistorted ) { if ( Playback.IsValid() ) Playback->Record( Draw ); } else Draw.MaterialQuad3D( Distorted.Material ); if ( !Buffer.Words.empty() ) Executor.Execute( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft ); };
		// 画素readbackから画面中央左寄りの既知点を返す。
		const auto Pixel = [&]( int32 X, int32 Y ) { int32 R = 0, G = 0, B = 0, A = 0; GetPixelSoftImage( Soft, X, Y, &R, &G, &B, &A ); return FColor32( static_cast<uint8>( R ), static_cast<uint8>( G ), static_cast<uint8>( B ), static_cast<uint8>( A ) ); };
		// 128x128の実readbackを順番どおり所有配列へ複写する。
		const auto CapturePixels = [&]() { TArray<FColor32> Pixels; Pixels.SetNum( 128 * 128 ); for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X ) Pixels[Y * 128 + X] = Pixel( X, Y ); return Pixels; };
		Render( false ); const FColor32 LegacyPixel = Pixel( 57, 57 ); const TArray<FColor32> LegacyPixels = CapturePixels();
		Render( true ); const FColor32 DistortedPixel = Pixel( 57, 57 );
		C.Check( Playback.IsValid() && Distorted.DistortionTexture >= 0 && Executor.SupportsEffectUVDistortion(), "通常D3D11 executorがPlayback経由のゆがみ材質をサポートする" );
		C.Check( LegacyPixel.r > LegacyPixel.g && DistortedPixel.g > DistortedPixel.r + 80, "正のR変位は左上赤source sampleを右上緑領域へ移す" );
		// 指定noiseでquadをnative実行する共通helper。
		const auto RenderData = [&]( const FDrawDistortedMaterialQuadData& Data ) { ClearDrawScreen(); FDrawBuffer Buffer; FDrawContext Draw( Buffer ); Draw.Blend( EDrawBlend::Alpha, 255 ); Draw.DistortedMaterialQuad3D( Data ); Executor.Execute( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft ); };
		// Playback bufferから最初の完全長歪み材質payloadを取り出す。
		const auto ReadDistortionData = [&]( const TSharedPtr<IEffectPlayback>& ValuePlayback, FDrawDistortedMaterialQuadData& OutData ) { if ( !ValuePlayback.IsValid() ) return false; FDrawBuffer Buffer; FDrawContext Draw( Buffer ); ValuePlayback->Record( Draw ); bool bFound = false; ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::DistortedMaterialQuad3D && Command.NumDataWords == static_cast<int32>( sizeof( FDrawDistortedMaterialQuadData ) / 4 ) ) { OutData = Command.Read<FDrawDistortedMaterialQuadData>(); bFound = true; } } ); return bFound; };
		// 元画像倍率・scrollとnoise方向を分けて、歪み倍率の二重適用を検出する。
		// source画像倍率とscrollを既知の非整数値にしたemitter。
		FParticleEmitterDefinition SourceScaleEmitter = Emitter;
		SourceScaleEmitter.Material.UVScale = FVector2( 2.25f, 1.75f ); SourceScaleEmitter.Material.UVScroll = FVector2( -0.2f, 0.3f ); SourceScaleEmitter.Material.Distortion.Strength.Keys[0].Value = 0.2f; SourceScaleEmitter.Material.Distortion.Strength.Keys[1].Value = 0.2f;
		// 秒齢1でsource scale/scrollを評価する再生。
		TSharedPtr<IEffectPlayback> SourceScalePlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { SourceScaleEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-source-scale" );
		if ( SourceScalePlayback.IsValid() ) { SourceScalePlayback->Update( 0.01f ); SourceScalePlayback->Update( 1.0f ); }
		// 再生が実際に記録したscale・offsetを保持するpayload。
		FDrawDistortedMaterialQuadData SourceScaleData{};
		// 完全長の材質命令が再生から得られたかを示すflag。
		const bool bSourceScaleData = ReadDistortionData( SourceScalePlayback, SourceScaleData );
		// 二重倍率誤りを色で見分ける既知の緑領域画素。
		RenderData( SourceScaleData ); const FColor32 SourceScalePixel = Pixel( 57, 64 );
		C.Check( bSourceScaleData && NMath::Abs( SourceScaleData.Material.UVOffset[0] - 0.8f ) < 1.0e-4f && NMath::Abs( SourceScaleData.Material.UVOffset[1] - 0.3f ) < 1.0e-4f && SourceScaleData.UVOffset[0] == 0.0f && SourceScaleData.UVOffset[1] == 0.0f && NMath::Abs( SourceScaleData.Material.UVScale[0] - 2.25f ) < 1.0e-4f && NMath::Abs( SourceScaleData.Material.UVScale[1] - 1.75f ) < 1.0e-4f && SourceScalePixel.g > SourceScalePixel.r + 80, "sourceOffset=(%.3f,%.3f), noiseOffset=(%.3f,%.3f), scale=(%.2f,%.2f), uv=(%.4f,%.4f), pixel(57,64)=(%d,%d,%d): source offsetとnoise offsetを分け倍率を二重適用しない", SourceScaleData.Material.UVOffset[0], SourceScaleData.Material.UVOffset[1], SourceScaleData.UVOffset[0], SourceScaleData.UVOffset[1], SourceScaleData.Material.UVScale[0], SourceScaleData.Material.UVScale[1], ( 57.5f - 32.0f ) / 64.0f, ( 64.5f - 32.0f ) / 64.0f, SourceScalePixel.r, SourceScalePixel.g, SourceScalePixel.b );
		// 上右alpha0・下右alpha128のsourceで画素alphaをnoise変位後に保つ。
		// alpha穴画像と通常発光を使うPlayback定義。
		FParticleEmitterDefinition AlphaEmitter = Emitter;
		AlphaEmitter.Texture = AlphaHoleSourcePath; AlphaEmitter.Material.Emission.Keys.Add( { 0.0f, 1.0f } ); AlphaEmitter.Material.Emission.Keys.Add( { 1.0f, 1.0f } );
		// alpha穴画像を持つ通常強度の再生。
		TSharedPtr<IEffectPlayback> AlphaPlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { AlphaEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-alpha-hole" );
		if ( AlphaPlayback.IsValid() ) AlphaPlayback->Update( 0.01f );
		// 通常強度の再生が記録した透明source payload。
		FDrawDistortedMaterialQuadData AlphaData{};
		// 通常強度のpayloadを取得できたかを示すflag。
		const bool bAlphaData = ReadDistortionData( AlphaPlayback, AlphaData );
		// 強度0で同じ場所が元画像の不透明赤として見える対照画素。
		AlphaData.Strength = 0.0f; RenderData( AlphaData ); const FColor32 OpaqueSourceControl = Pixel( 57, 57 );
		// 歪み後に透明になる画素と半透明の右下画素。
		AlphaData.Strength = 0.35f; RenderData( AlphaData ); const FColor32 AlphaHolePixel = Pixel( 57, 57 ); const FColor32 HalfAlphaPixel = Pixel( 57, 80 );
		// 発光倍率だけ32へ上げる複製emitter。
		FParticleEmitterDefinition AlphaBrightEmitter = AlphaEmitter; AlphaBrightEmitter.Material.Emission.Keys[0].Value = 32.0f; AlphaBrightEmitter.Material.Emission.Keys[1].Value = 32.0f;
		// 強発光時もalpha穴を保持する再生。
		TSharedPtr<IEffectPlayback> AlphaBrightPlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { AlphaBrightEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-alpha-hole-bright" );
		if ( AlphaBrightPlayback.IsValid() ) AlphaBrightPlayback->Update( 0.01f );
		// 強発光再生が記録した材質payload。
		FDrawDistortedMaterialQuadData AlphaBrightData{};
		// 強発光payloadの取得成否。
		const bool bAlphaBrightData = ReadDistortionData( AlphaBrightPlayback, AlphaBrightData );
		// 発光32で描いた透明穴とalpha128 survivor。
		RenderData( AlphaBrightData ); const FColor32 BrightAlphaHolePixel = Pixel( 57, 57 ); const FColor32 BrightHalfAlphaPixel = Pixel( 57, 80 );
		C.Check( bAlphaData && bAlphaBrightData && OpaqueSourceControl.r > 180 && OpaqueSourceControl.g < 50 && AlphaHolePixel.r < 20 && AlphaHolePixel.g < 20 && BrightAlphaHolePixel.r < 20 && BrightAlphaHolePixel.g < 20, "RGBを持つ透明source画素は強い自己発光でも背景へ消える" );
		C.Check( HalfAlphaPixel.r >= 112 && HalfAlphaPixel.r <= 132 && HalfAlphaPixel.g >= 106 && HalfAlphaPixel.g <= 126 && HalfAlphaPixel.b < 30 && BrightHalfAlphaPixel.r >= 124 && BrightHalfAlphaPixel.r <= 145 && BrightHalfAlphaPixel.g >= 124 && BrightHalfAlphaPixel.g <= 145 && BrightHalfAlphaPixel.b >= 124 && BrightHalfAlphaPixel.b <= 145, "alpha128のsourceは通常発光と32倍発光を背景へ半透明合成する" );
		FDrawDistortedMaterialQuadData Vertical = Distorted; Vertical.DistortionTexture = NoiseY.IsValid() ? NoiseY->GetHandle() : -1;
		RenderData( Vertical ); const FColor32 VerticalPixel = Pixel( 57, 57 );
		C.Check( LegacyPixel.r > LegacyPixel.b && VerticalPixel.b > VerticalPixel.r + 80, "正のG変位は左上赤source sampleを左下青領域へ移す" );
		FDrawDistortedMaterialQuadData Negative = Distorted; Negative.DistortionTexture = NoiseNegativeX.IsValid() ? NoiseNegativeX->GetHandle() : -1;
		RenderData( Negative ); const FColor32 NegativePixel = Pixel( 71, 57 );
		C.Check( NegativePixel.r > NegativePixel.g + 80, "負のR変位は右上緑source sampleを左上赤領域へ戻す" );
		FDrawDistortedMaterialQuadData Neutral = Distorted; Neutral.DistortionTexture = NestedNoise.IsValid() ? NestedNoise->GetHandle() : -1; Neutral.Strength = 0.35f;
		RenderData( Neutral ); const FColor32 NeutralPixel = Pixel( 57, 57 ); const FColor32 NeutralOppositePixel = Pixel( 71, 71 );
		C.Check( NeutralPixel.r > 200 && NeutralPixel.g < 50 && NeutralOppositePixel.r > 180 && NeutralOppositePixel.g > 170 && NeutralOppositePixel.b < 60, "NPOT nested regionの中立noiseを非整数強度で左右のsource色から判別する" );
		// particle ageが進み、正負scrollでnoiseの異なるR領域を読む再生を作る。
		FParticleEmitterDefinition FlowEmitter = Emitter;
		FlowEmitter.Material.Distortion.Texture = TimeNoisePath; FlowEmitter.Material.Distortion.UVScale = FVector2( 1.0f, 1.0f ); FlowEmitter.Material.Distortion.UVScroll = FVector2( 0.5f, 0.0f );
		TSharedPtr<IEffectPlayback> FlowPlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { FlowEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-positive-flow" );
		if ( FlowPlayback.IsValid() ) FlowPlayback->Update( 0.01f );
		// 実粒子Recordを実行し、CPU側で評価されたoffsetとsource pixelを同時に得る。
		const auto RenderFlow = [&]( const TSharedPtr<IEffectPlayback>& Flow, FDrawDistortedMaterialQuadData& OutData, int32 X, int32 Y ) { ClearDrawScreen(); FDrawBuffer Buffer; FDrawContext Draw( Buffer ); Draw.Blend( EDrawBlend::Alpha, 255 ); if ( Flow.IsValid() ) Flow->Record( Draw ); if ( !Buffer.Words.empty() ) { ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::DistortedMaterialQuad3D ) OutData = Command.Read<FDrawDistortedMaterialQuadData>(); } ); Executor.Execute( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } ); } RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft ); return Pixel( X, Y ); };
		// 非均一time noiseを非整数倍率で読む時と倍率1を同じ既知画素で比べる。
		// 同じ非均一noiseを非整数倍率で参照するemitter。
		FParticleEmitterDefinition NoiseScaleEmitter = Emitter;
		NoiseScaleEmitter.Material.Distortion.Texture = TimeNoisePath; NoiseScaleEmitter.Material.Distortion.UVScale = FVector2( 2.5f, 1.25f ); NoiseScaleEmitter.Material.Distortion.UVScroll = FVector2( 0.0f, 0.0f );
		// noise倍率を1へ戻した比較用emitter。
		TSharedPtr<IEffectPlayback> NoiseScalePlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { NoiseScaleEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-noise-scale" );
		FParticleEmitterDefinition NoiseScaleOneEmitter = NoiseScaleEmitter; NoiseScaleOneEmitter.Material.Distortion.UVScale = FVector2( 1.0f, 1.0f );
		// 非整数倍率と1倍のそれぞれのPlayback。
		TSharedPtr<IEffectPlayback> NoiseScaleOnePlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { NoiseScaleOneEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-noise-scale-one" );
		if ( NoiseScalePlayback.IsValid() ) NoiseScalePlayback->Update( 0.01f ); if ( NoiseScaleOnePlayback.IsValid() ) NoiseScaleOnePlayback->Update( 0.01f );
		// 両再生から記録するnoise倍率値。
		FDrawDistortedMaterialQuadData NoiseScaleData{}; FDrawDistortedMaterialQuadData NoiseScaleOneData{};
		// どちらの再生も材質命令を記録したかを示すflag。
		const bool bNoiseScaleData = ReadDistortionData( NoiseScalePlayback, NoiseScaleData ) && ReadDistortionData( NoiseScaleOnePlayback, NoiseScaleOneData );
		// 同一位置での非整数倍率と1倍のnoise読取結果。
		const FColor32 NoiseScalePixel = RenderFlow( NoiseScalePlayback, NoiseScaleData, 55, 57 );
		const FColor32 NoiseScaleOnePixel = RenderFlow( NoiseScaleOnePlayback, NoiseScaleOneData, 55, 57 );
		C.Check( bNoiseScaleData && NMath::Abs( NoiseScaleData.UVScale[0] - 2.5f ) < 1.0e-4f && NoiseScalePixel.r > NoiseScalePixel.g + 80 && NoiseScaleOnePixel.g > NoiseScaleOnePixel.r + 80, "非均一noiseの非整数倍率が既知pixelの方向を倍率1から変える" );
		FDrawDistortedMaterialQuadData FlowData{};
		const FColor32 FlowAtZero = RenderFlow( FlowPlayback, FlowData, 57, 57 );
		if ( FlowPlayback.IsValid() ) FlowPlayback->Update( 0.5f );
		const FColor32 FlowAtHalf = RenderFlow( FlowPlayback, FlowData, 57, 57 );
		C.Check( FlowPlayback.IsValid() && NMath::Abs( FlowData.UVOffset[0] - 0.25f ) < 1.0e-4f && FlowAtZero.g > FlowAtZero.r + 80 && FlowAtHalf.r > FlowAtHalf.g + 80, "Age 0から0.5秒の正UVscrollがnoise方向とsource pixelを変える" );
		FParticleEmitterDefinition ReverseFlowEmitter = FlowEmitter; ReverseFlowEmitter.Material.Distortion.UVScroll = FVector2( -0.5f, 0.0f );
		TSharedPtr<IEffectPlayback> ReverseFlowPlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { ReverseFlowEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-negative-flow" );
		if ( ReverseFlowPlayback.IsValid() ) { ReverseFlowPlayback->Update( 0.01f ); ReverseFlowPlayback->Update( 0.5f ); }
		FDrawDistortedMaterialQuadData ReverseFlowData{};
		const FColor32 ReverseFlowPixel = RenderFlow( ReverseFlowPlayback, ReverseFlowData, 57, 57 );
		C.Check( ReverseFlowPlayback.IsValid() && NMath::Abs( ReverseFlowData.UVOffset[0] - 0.75f ) < 1.0e-4f && ReverseFlowPixel.g > ReverseFlowPixel.r + 80, "負UVscrollは同時刻に反対側のnoiseへ戻る" );
		// mask消失・境界発光・自己発光をゆがみ付きの同じ実Playbackへ加える。
		FParticleEmitterDefinition MaskedEmitter = Emitter;
		MaskedEmitter.Material.bDissolve = true; MaskedEmitter.Material.MaskTexture = MaskPath; MaskedEmitter.Material.EdgeWidth = 0.12f; MaskedEmitter.Material.EdgeEmission = 2.0f; MaskedEmitter.Material.Emission.Keys.Add( { 0.0f, 2.0f } ); MaskedEmitter.Material.Emission.Keys.Add( { 1.0f, 2.0f } ); MaskedEmitter.Material.Dissolve.Keys.Add( { 0.0f, 0.5f } ); MaskedEmitter.Material.Dissolve.Keys.Add( { 1.0f, 0.5f } );
		TSharedPtr<IEffectPlayback> MaskedPlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { MaskedEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-mask-combined" );
		if ( MaskedPlayback.IsValid() ) MaskedPlayback->Update( 0.01f );
		ClearDrawScreen(); FDrawBuffer MaskedBuffer; FDrawContext MaskedDraw( MaskedBuffer ); MaskedDraw.Blend( EDrawBlend::Alpha, 255 ); if ( MaskedPlayback.IsValid() ) MaskedPlayback->Record( MaskedDraw );
		// mask・source・発光値と命令数をreadback前に所有して診断する。
		FDrawDistortedMaterialQuadData MaskedData{};
		int32 MaskedCommandCount = 0;
		if ( !MaskedBuffer.Words.empty() ) ForEachDrawCommand( FDrawSpan{ MaskedBuffer.Words.data(), static_cast<int32>( MaskedBuffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::DistortedMaterialQuad3D && Command.NumDataWords == static_cast<int32>( sizeof( FDrawDistortedMaterialQuadData ) / 4 ) ) { MaskedData = Command.Read<FDrawDistortedMaterialQuadData>(); ++MaskedCommandCount; } } );
		if ( !MaskedBuffer.Words.empty() ) Executor.Execute( FDrawSpan{ MaskedBuffer.Words.data(), static_cast<int32>( MaskedBuffer.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		const FColor32 DissolveHole = Pixel( 40, 64 ); const FColor32 EmissiveSurvivor = Pixel( 80, 64 );
		C.Check( MaskedPlayback.IsValid() && MaskedCommandCount == 1 && DissolveHole.r < 20 && DissolveHole.g < 20 && EmissiveSurvivor.b > 200, "maskUV=(%.3f,%.3f)で左の穴は背景(%.0f,%.0f,%.0f)、pixel(80,64)のsourceUVは約(%.3f,%.3f)で青(%.0f,%.0f,%.0f)、commands=%d dissolve=%.2f emission=%.2f", ( 40.5f - 32.0f ) / 64.0f, ( 64.5f - 32.0f ) / 64.0f, static_cast<float32>( DissolveHole.r ), static_cast<float32>( DissolveHole.g ), static_cast<float32>( DissolveHole.b ), std::fmod( ( 80.5f - 32.0f ) / 64.0f + 0.35f, 1.0f ), ( 64.5f - 32.0f ) / 64.0f, static_cast<float32>( EmissiveSurvivor.r ), static_cast<float32>( EmissiveSurvivor.g ), static_cast<float32>( EmissiveSurvivor.b ), MaskedCommandCount, MaskedData.Material.Dissolve, MaskedData.Material.Emission );
		// 変位量を大きくしたとき隣frameの青が混ざらないatlasを用意する。
		const FString AtlasPath = "uv-distortion/atlas.tga";
		const FString SheetPath = "uv-distortion/atlas.sheet.json";
		const bool bAtlasSaved = NAssetTest::WriteFileBytes( Dir / AtlasPath, MakeFlipbookAtlasTga() );
		FSpriteSheetDefinition SheetDefinition;
		SheetDefinition.TexturePath = AtlasPath;
		SheetDefinition.Frames.Add( { 0, 0, 8, 8, 4.0f, 4.0f } );
		SheetDefinition.Frames.Add( { 8, 0, 8, 8, 4.0f, 4.0f } );
		SheetDefinition.Clips.Add( FSpriteClip::FromFrames( "Atlas", { 0, 1 }, 4.0f, EAnimLoopMode::Once ) );
		C.Check( bAtlasSaved && Assets.SaveSpriteSheetDefinition( SheetPath, SheetDefinition ), "distortion隔離用atlasとclipを保存する" );
		// 材質noiseを有効にした先頭frameの実Playback。
		FParticleEmitterDefinition FlipbookEmitter = Emitter;
		FlipbookEmitter.Flipbook.bEnabled = true; FlipbookEmitter.Flipbook.Sheet = SheetPath; FlipbookEmitter.Flipbook.Clip = "Atlas"; FlipbookEmitter.Material.Distortion.UVScale = FVector2( 3.25f, 2.75f ); FlipbookEmitter.Material.Distortion.Strength.Keys[0].Value = 0.7f; FlipbookEmitter.Material.Distortion.Strength.Keys[1].Value = 0.7f;
		TSharedPtr<IEffectPlayback> FlipbookPlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { FlipbookEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-flipbook" );
		if ( FlipbookPlayback.IsValid() ) FlipbookPlayback->Update( 0.01f );
		// 実Playbackが選んだコマ命令を値として読み、無変位対照と比較する。
		FDrawDistortedMaterialQuadData FlipbookData{};
		int32 FlipbookCommandCount = 0;
		if ( FlipbookPlayback.IsValid() ) { FDrawBuffer Recorded; FDrawContext Draw( Recorded ); FlipbookPlayback->Record( Draw ); ForEachDrawCommand( FDrawSpan{ Recorded.Words.data(), static_cast<int32>( Recorded.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::DistortedMaterialQuad3D ) { ++FlipbookCommandCount; FlipbookData = Command.Read<FDrawDistortedMaterialQuadData>(); } } ); }
		// same-frame, same-materialの強度0全画素対照。
		FlipbookData.Strength = 0.0f; ClearDrawScreen(); FDrawBuffer FlipbookBaseline; FDrawContext BaselineDraw( FlipbookBaseline ); BaselineDraw.Blend( EDrawBlend::Alpha, 255 ); BaselineDraw.DistortedMaterialQuad3D( FlipbookData ); Executor.Execute( FDrawSpan{ FlipbookBaseline.Words.data(), static_cast<int32>( FlipbookBaseline.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft ); const TArray<FColor32> FlipbookBasePixels = CapturePixels();
		// 同じPlaybackが選んだframeを本来のゆがみ強度で描く。
		ClearDrawScreen(); FDrawBuffer FlipbookBuffer; FDrawContext FlipbookDraw( FlipbookBuffer ); FlipbookDraw.Blend( EDrawBlend::Alpha, 255 ); if ( FlipbookPlayback.IsValid() ) FlipbookPlayback->Record( FlipbookDraw ); Executor.Execute( FDrawSpan{ FlipbookBuffer.Words.data(), static_cast<int32>( FlipbookBuffer.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		// frame内の赤緑画素・隣frameの青漏れ・変位による変化を数える。
		int32 FrameRedPixels = 0; int32 FrameGreenPixels = 0; int32 NeighborBluePixels = 0; int32 FlipbookChangedPixels = 0;
		for ( int32 Y = 32; Y < 96; ++Y ) for ( int32 X = 32; X < 96; ++X ) { const FColor32 Value = Pixel( X, Y ); const FColor32 Baseline = FlipbookBasePixels[Y * 128 + X]; FrameRedPixels += Value.r > 100 && Value.r > Value.g + 80 ? 1 : 0; FrameGreenPixels += Value.g > 100 && Value.g > Value.r + 80 ? 1 : 0; NeighborBluePixels += Value.b > 80 ? 1 : 0; FlipbookChangedPixels += Value != Baseline ? 1 : 0; }
		C.Check( FlipbookCommandCount == 1 && FrameRedPixels > 100 && FrameGreenPixels > 100 && NeighborBluePixels == 0 && FlipbookChangedPixels > 100, "実frameが赤緑模様を描き、強い歪みで絵を変えても隣frameの青を混ぜない" );
		const FString FlipbookProof = FString( "Saved" ) / "EffectUVDistortionProof";
		if ( NFileSystem::EnsureDirectory( FlipbookProof ) ) SaveDrawScreenToPNG( 0, 0, 128, 128, *( FlipbookProof / "uv-distortion-flipbook-isolation.png" ) );
		// 変位命令の幅を0へ戻して既存材質描画と全画素を比較する。
		Distorted.Strength = 0.0f; ClearDrawScreen(); FDrawBuffer ZeroBuffer; FDrawContext ZeroDraw( ZeroBuffer ); ZeroDraw.Blend( EDrawBlend::Alpha, 255 ); ZeroDraw.DistortedMaterialQuad3D( Distorted ); Executor.Execute( FDrawSpan{ ZeroBuffer.Words.data(), static_cast<int32>( ZeroBuffer.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft ); const TArray<FColor32> ZeroPixels = CapturePixels();
		// 全画素で旧材質描画との差を数える。
		int32 ZeroPixelMismatch = 0;
		for ( int32 Index = 0; Index < LegacyPixels.Num(); ++Index ) if ( LegacyPixels[Index] != ZeroPixels[Index] ) ++ZeroPixelMismatch;
		C.Check( ZeroPixelMismatch == 0, "Strength 0の変位命令は既存材質と128x128全画素で一致する" );
		Distorted.Strength = 0.35f;
		// active strengthでproofを再描画して、比較画像へ0強度を残さない。
		Render( true );
		// 診断画像は実行sandboxのSavedへ保存する。
		const FString ProofDirectory = FString( "Saved" ) / "EffectUVDistortionProof";
		if ( NFileSystem::EnsureDirectory( ProofDirectory ) ) SaveDrawScreenToPNG( 0, 0, 128, 128, *( ProofDirectory / "uv-distortion-positive-x.png" ) );
		// 不正な形状を含むが命令長は正しい直接payload。
		FDrawDistortedMaterialQuadData Invalid = Distorted; Invalid.Material.Quad.Pos[0] = TNumericLimits<float32>::QuietNaN();
		FDrawBuffer Forged; Forged.Words.resize( 1 + sizeof( Invalid ) / 4 ); Forged.Words[0] = NDrawCommand::MakeHeader( EDrawCommand::DistortedMaterialQuad3D, 0, static_cast<int32>( Forged.Words.size() ) ); std::memcpy( Forged.Words.data() + 1, &Invalid, sizeof( Invalid ) );
		ClearDrawScreen(); Executor.Execute( FDrawSpan{ Forged.Words.data(), static_cast<int32>( Forged.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		const FColor32 RejectedPixel = Pixel( 57, 64 );
		C.Check( RejectedPixel.r < 20 && RejectedPixel.g < 20 && RejectedPixel.b < 20, "完全長の偽造命令も非有限quad座標ならGPU描画を拒否する" );
		// payloadを全く持たない偽造命令がRead前に長さ検査で止まる。
		FDrawBuffer ShortCommand; ShortCommand.Words.push_back( NDrawCommand::MakeHeader( EDrawCommand::DistortedMaterialQuad3D, 0, 1 ) );
		ClearDrawScreen(); Executor.Execute( FDrawSpan{ ShortCommand.Words.data(), static_cast<int32>( ShortCommand.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		const FColor32 ShortRejectedPixel = Pixel( 57, 64 );
		C.Check( ShortRejectedPixel.r < 20 && ShortRejectedPixel.g < 20 && ShortRejectedPixel.b < 20, "payload長が短い命令は材質dataを読まずに拒否する" );
		// rendererの戻り値で各異常条件がGPU処理へ入る前に止まることを直接検証する。
		FEffectMaterialRenderer GuardRenderer;
		// 頂点のx位置にNaNを入れ、位置検証を外した描画が成功扱いにならないか調べる。
		FDrawDistortedMaterialQuadData InvalidPosition = Distorted; InvalidPosition.Material.Quad.Pos[0] = TNumericLimits<float32>::QuietNaN();
		// pivotの許容範囲を外し、不正な基準点を拒否するか調べる。
		FDrawDistortedMaterialQuadData InvalidPivot = Distorted; InvalidPivot.Material.Quad.PivotX = -0.1f;
		// 定義外の向き番号を渡し、enum値を検証するか調べる。
		FDrawDistortedMaterialQuadData InvalidOrient = Distorted; InvalidOrient.Material.Quad.Orient = 900;
		// 画像handleを無効化し、必要な変位画像の欠落を拒否するか調べる。
		FDrawDistortedMaterialQuadData InvalidNoise = Distorted; InvalidNoise.DistortionTexture = -1;
		// 発光値を無限大にし、有限値検証で拒否するか調べる。
		FDrawDistortedMaterialQuadData InvalidEmission = Distorted; InvalidEmission.Material.Emission = TNumericLimits<float32>::Infinity();
		// 直接renderer呼出し前に、screen/viewportを反映し対応blendを選ぶ。
		// 試験後に戻すDxLib合成方式。
		int32 GuardOldBlend = DX_BLENDMODE_NOBLEND;
		// 試験後に戻す合成強度。
		int32 GuardOldBlendParam = 0;
		GetDrawBlendMode( &GuardOldBlend, &GuardOldBlendParam );
		SetDrawBlendMode( DX_BLENDMODE_ALPHA, 255 );
		RenderVertex();
		// 不正値試験が環境不備で常に失敗しないよう、有効dataの描画成功を先に確かめる。
		ClearDrawScreen();
		const bool bGuardPositiveControl = GuardRenderer.DrawDistorted( Distorted );
		RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		const FColor32 GuardPositivePixel = Pixel( 57, 57 );
		C.Check( bGuardPositiveControl && GuardPositivePixel.g > GuardPositivePixel.r + 80, "Alpha合成・同期済み128x128描画先で有効なrenderer呼出しが既知の緑画素を描く" );
		// 各不正値を独立して呼び、別caseの短絡で検証を省かない。
		ClearDrawScreen();
		// 非有限位置をrendererが拒否した結果。
		const bool bPositionRejected = !NDrawDistortedMaterialQuad::IsValid( InvalidPosition ) && !GuardRenderer.DrawDistorted( InvalidPosition );
		ClearDrawScreen();
		// 範囲外pivotをrendererが拒否した結果。
		const bool bPivotRejected = !NDrawDistortedMaterialQuad::IsValid( InvalidPivot ) && !GuardRenderer.DrawDistorted( InvalidPivot );
		ClearDrawScreen();
		// 不正orientをrendererが拒否した結果。
		const bool bOrientRejected = !NDrawDistortedMaterialQuad::IsValid( InvalidOrient ) && !GuardRenderer.DrawDistorted( InvalidOrient );
		ClearDrawScreen();
		// 欠落noise画像をrendererが拒否した結果。
		const bool bNoiseRejected = !NDrawDistortedMaterialQuad::IsValid( InvalidNoise ) && !GuardRenderer.DrawDistorted( InvalidNoise );
		ClearDrawScreen();
		// 非有限発光値をrendererが拒否した結果。
		const bool bEmissionRejected = !NDrawDistortedMaterialQuad::IsValid( InvalidEmission ) && !GuardRenderer.DrawDistorted( InvalidEmission );
		SetDrawBlendMode( GuardOldBlend, GuardOldBlendParam );
		RenderVertex();
		GuardRenderer.Deinitialize();
		C.Check( bPositionRejected, "rendererは非有限位置を拒否する" );
		C.Check( bPivotRejected, "rendererは範囲外pivotを拒否する" );
		C.Check( bOrientRejected, "rendererは不正orientを拒否する" );
		C.Check( bNoiseRejected, "rendererは欠落したnoise画像を拒否する" );
		C.Check( bEmissionRejected, "rendererは非有限発光値を拒否する" );
		// pixel shaderのt2に外部SRVを結び、native描画後も同じ参照へ戻るか調べる。
		ID3D11Device* Device = const_cast<ID3D11Device*>( static_cast<const ID3D11Device*>( GetUseDirect3D11Device() ) );
		ID3D11DeviceContext* NativeContext = const_cast<ID3D11DeviceContext*>( static_cast<const ID3D11DeviceContext*>( GetUseDirect3D11DeviceContext() ) );
		ID3D11ShaderResourceView* SentinelView = nullptr;
		ID3D11ShaderResourceView* OriginalView = nullptr;
		const void* SentinelTexture = NestedNoise.IsValid() ? GetGraphID3D11Texture2D( NestedNoise->GetHandle() ) : nullptr;
		const bool bSentinelCreated = Device != nullptr && NativeContext != nullptr && SentinelTexture != nullptr && SUCCEEDED( Device->CreateShaderResourceView( static_cast<ID3D11Resource*>( const_cast<void*>( SentinelTexture ) ), nullptr, &SentinelView ) );
		if ( bSentinelCreated )
		{
			NativeContext->PSGetShaderResources( 2, 1, &OriginalView );
			RenderVertex(); ClearDrawScreen(); RenderVertex(); NativeContext->PSSetShaderResources( 2, 1, &SentinelView );
			FDrawBuffer SentinelBuffer; FDrawContext SentinelDraw( SentinelBuffer ); SentinelDraw.Blend( EDrawBlend::Alpha, 255 ); if ( Playback.IsValid() ) Playback->Record( SentinelDraw ); Executor.Execute( FDrawSpan{ SentinelBuffer.Words.data(), static_cast<int32>( SentinelBuffer.Words.size() ) } );
		}
		ID3D11ShaderResourceView* RestoredView = nullptr;
		if ( bSentinelCreated ) NativeContext->PSGetShaderResources( 2, 1, &RestoredView );
		const bool bSentinelRestored = bSentinelCreated && RestoredView == SentinelView;
		if ( RestoredView != nullptr ) RestoredView->Release();
		if ( bSentinelCreated ) NativeContext->PSSetShaderResources( 2, 1, &OriginalView );
		if ( OriginalView != nullptr ) OriginalView->Release();
		if ( SentinelView != nullptr ) SentinelView->Release();
		C.Check( bSentinelRestored, "材質ゆがみ描画後もpixel shader t2のcaller SRVを復元する" );
		RenderVertex(); SetDrawScreen( Caller.Screen );
	}
	DeleteSoftImage( Soft ); DeleteGraph( Screen ); Assets.SetRootDirectory( PreviousRoot );
}
