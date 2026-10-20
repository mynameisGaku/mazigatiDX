// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <vector>

#include "Library/Core/Interfaces/Render/DrawDistortedMaterialRibbon.h"
#include "Library/Core/Interfaces/Render/DrawMaterialQuad.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/DxLib/EffectMaterialRenderer.h"
#include "Library/Platform/FileSystem.h"

namespace
{
	// DxLibが読み込める無圧縮32bit TGAのheaderを作る。
	std::vector<uint8> MakeTgaHeader( int32 Width, int32 Height )
	{
		return { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, static_cast<uint8>( Width ), 0, static_cast<uint8>( Height ), 0, 32, 0x28 };
	}

	// 四象限を異なる色にして横・縦変位の参照位置を識別できるsourceを作る。
	std::vector<uint8> MakeRibbonSource( bool bAlphaPattern )
	{
		// 四象限sourceの32x32 TGA byte列。
		std::vector<uint8> Bytes = MakeTgaHeader( 32, 32 );
		// 画素座標Y/Xから象限色とalphaを決める二重loop。
		for ( int32 Y = 0; Y < 32; ++Y ) for ( int32 X = 0; X < 32; ++X )
		{
			// 横方向の左右象限を示す判定。
			const bool bLeft = X < 16;
			// 縦方向の上下象限を示す判定。
			const bool bTop = Y < 16;
			// 象限ごとに値を変える赤channel。
			const uint8 R = bTop ? ( bLeft ? 240 : 16 ) : ( bLeft ? 16 : 232 );
			// 象限ごとに値を変える緑channel。
			const uint8 G = bTop ? ( bLeft ? 16 : 232 ) : ( bLeft ? 32 : 220 );
			// 象限ごとに値を変える青channel。
			const uint8 B = bTop ? 16 : ( bLeft ? 240 : 16 );
			// alpha試験時だけ右上を透明、右下を半透明にする値。
			const uint8 A = !bAlphaPattern || bLeft ? 255 : ( bTop ? 0 : 128 );
			Bytes.push_back( B ); Bytes.push_back( G ); Bytes.push_back( R ); Bytes.push_back( A );
		}
		return Bytes;
	}

	// 左半分を消し、右半分を残す消失maskを作る。
	std::vector<uint8> MakeRibbonMask()
	{
		// 32x32の赤チャンネルmask byte列。
		std::vector<uint8> Bytes = MakeTgaHeader( 32, 32 );
		// 各画素のX位置で左右のmask値を切り替える二重loop。
		for ( int32 Y = 0; Y < 32; ++Y ) for ( int32 X = 0; X < 32; ++X )
		{
			// 左半分を消失、右半分を生存させる赤channel値。
			const uint8 Value = X < 16 ? 0 : 255;
			Bytes.push_back( Value ); Bytes.push_back( Value ); Bytes.push_back( Value ); Bytes.push_back( 255 );
		}
		return Bytes;
	}

	// 指定RGを全面へ書くnoise画像を作る。
	std::vector<uint8> MakeConstantNoise( uint8 R, uint8 G )
	{
		// 8x8画像に使う固定方向noise byte列。
		std::vector<uint8> Bytes = MakeTgaHeader( 8, 8 );
		// 64画素すべてへ引数のnoise方向を複写する添字loop。
		for ( int32 Index = 0; Index < 64; ++Index )
		{
			Bytes.push_back( 0 ); Bytes.push_back( G ); Bytes.push_back( R ); Bytes.push_back( 255 );
		}
		return Bytes;
	}

	// 左右でnoiseのR符号を変え、scroll反復を検査する画像を作る。
	std::vector<uint8> MakeFlowNoise()
	{
		// 左が+X、右が-Xとなる8x8画像。
		std::vector<uint8> Bytes = MakeTgaHeader( 8, 8 );
		// 8x8の左右位置をnoiseの横向きへ対応させる二重loop。
		for ( int32 Y = 0; Y < 8; ++Y ) for ( int32 X = 0; X < 8; ++X )
		{
			Bytes.push_back( 0 ); Bytes.push_back( 127 ); Bytes.push_back( X < 4 ? 255 : 0 ); Bytes.push_back( 255 );
		}
		return Bytes;
	}

	// 10x7親画像の左半分だけ+Xにしてnested region外漏れを検出する。
	std::vector<uint8> MakeNestedNoise()
	{
		// NPOT parentへnoise符号を配置したbyte列。
		std::vector<uint8> Bytes = MakeTgaHeader( 10, 7 );
		// NPOT親画像の左右5列をnoise符号へ対応させる二重loop。
		for ( int32 Y = 0; Y < 7; ++Y ) for ( int32 X = 0; X < 10; ++X )
		{
			Bytes.push_back( 0 ); Bytes.push_back( 128 ); Bytes.push_back( X < 5 ? 255 : 127 ); Bytes.push_back( 255 );
		}
		return Bytes;
	}

	// 呼び出し元の描画画面とcamera設定を実GPU試験後に戻す。
	struct FRibbonDistortionCallerState
	{
		// 試験前に選択されていた描画先と画面設定。
		int32 Screen = GetDrawScreen();
		int32 ScreenReset = GetUseSetDrawScreenSettingReset();
		// 試験前に使われていたcamera範囲と投影中心。
		float32 Near = GetCameraNear();
		float32 Far = GetCameraFar();
		float32 Aspect = GetCameraDotAspect();
		float32 Center[2] = {};
		// SetupCameraが変更するcamera行列。
		MATRIX CameraView = GetCameraViewMatrix();
		MATRIX CameraProjection = GetCameraProjectionMatrix();
		// DxLibの論理描画行列。
		MATRIX World = MGetIdent();
		MATRIX View = MGetIdent();
		MATRIX Projection = MGetIdent();
		MATRIX Viewport = MGetIdent();
		// 呼び出し元の切取り、合成、明度、背景色。
		RECT Area{};
		int32 Blend[2] = {};
		int32 Bright[3] = {};
		int32 Background[4] = {};

		// callerの描画状態を保存して画面切替の自動変更を止める。
		FRibbonDistortionCallerState()
		{
			GetCameraScreenCenter( &Center[0], &Center[1] ); GetTransformToWorldMatrix( &World ); GetTransformToViewMatrix( &View ); GetTransformToProjectionMatrix( &Projection ); GetTransformToViewportMatrix( &Viewport ); GetDrawArea( &Area ); GetDrawBlendMode( &Blend[0], &Blend[1] ); GetDrawBright( &Bright[0], &Bright[1], &Bright[2] ); GetBackgroundColor( &Background[0], &Background[1], &Background[2], &Background[3] ); SetUseSetDrawScreenSettingReset( FALSE );
		}

		// 試験が変更したcallerの描画状態を復元する。
		~FRibbonDistortionCallerState()
		{
			RenderVertex(); SetDrawScreen( Screen ); SetCameraNearFar( Near, Far ); SetCameraDotAspect( Aspect ); SetCameraScreenCenter( Center[0], Center[1] ); SetCameraViewMatrix( CameraView ); SetupCamera_ProjectionMatrix( CameraProjection ); SetTransformToWorld( &World ); SetTransformToView( &View ); SetTransformToProjection( &Projection ); SetTransformToViewport( &Viewport ); SetDrawArea( Area.left, Area.top, Area.right, Area.bottom ); SetDrawBlendMode( Blend[0], Blend[1] ); SetDrawBright( Bright[0], Bright[1], Bright[2] ); SetBackgroundColor( Background[0], Background[1], Background[2], Background[3] ); SetUseSetDrawScreenSettingReset( ScreenReset );
		}
	};

	// sandbox asset rootを元へ戻す所有guard。
	struct FRibbonDistortionAssetRoot
	{
		// root変更前のasset path。
		FString Previous;
		// rootを変更する既存asset manager。
		CAssetManager& Assets;

		// 現在値を保存してfixture rootへ切り替える。
		FRibbonDistortionAssetRoot( CAssetManager& InAssets, const FString& Root ) : Previous( InAssets.GetRootDirectory() ), Assets( InAssets ) { Assets.SetRootDirectory( Root ); }
		// scope終了時にasset pathを元へ戻す。
		~FRibbonDistortionAssetRoot() { Assets.SetRootDirectory( Previous ); }
	};

	// 点の形状とnoise設定を一括で作り、他の値を初期化する。
	FDrawDistortedMaterialRibbonData::FPoint MakeDistortionPoint( float32 X, float32 Y, float32 U, float32 Strength, float32 NoiseU, float32 NoiseV, float32 SourceU = 0.0f, float32 SourceV = 0.0f, float32 Dissolve = 0.0f, float32 Emission = 1.0f, uint32 Color = 0xFFFFFFFFu )
	{
		// 新旧材質設定を初期値から持つ点。
		FDrawDistortedMaterialRibbonData::FPoint Point{};
		Point.Material.Path.Pos[0] = X; Point.Material.Path.Pos[1] = Y; Point.Material.Path.Pos[2] = 0.0f; Point.Material.Path.Width = 2.0f; Point.Material.Path.U = U; Point.Material.Path.Color = Color;
		Point.Material.Dissolve = Dissolve; Point.Material.Emission = Emission; Point.Material.UVOffset[0] = SourceU; Point.Material.UVOffset[1] = SourceV;
		Point.Strength = Strength; Point.UVOffset[0] = NoiseU; Point.UVOffset[1] = NoiseV;
		return Point;
	}

	// 二つの128x128画像が全RGBA値まで一致するかを返す。
	bool EqualPixels( const TArray<FColor32>& Left, const TArray<FColor32>& Right )
	{
		if ( Left.Num() != Right.Num() ) return false;
		for ( int32 Index = 0; Index < Left.Num(); ++Index ) if ( Left[Index].r != Right[Index].r || Left[Index].g != Right[Index].g || Left[Index].b != Right[Index].b || Left[Index].a != Right[Index].a ) return false;
		return true;
	}

	// 二枚の全画素差と最初に異なった座標・RGBAを記録する。
	struct FPixelDifference
	{
		// 全RGBAのいずれかが違う画素数。
		int32 Count = 0;
		// 最初に違った画面座標。
		int32 FirstX = -1;
		int32 FirstY = -1;
		// 最初に違った左画像の色。
		FColor32 Left{};
		// 最初に違った右画像の色。
		FColor32 Right{};
	};

	// 全画素を比較して診断用の最初の差を返す。
	FPixelDifference DescribePixelDifference( const TArray<FColor32>& Left, const TArray<FColor32>& Right, int32 Width )
	{
		// 比較結果と最初の差を蓄積する値。
		FPixelDifference Result;
		if ( Left.Num() != Right.Num() || Width <= 0 ) { Result.Count = -1; return Result; }
		for ( int32 Index = 0; Index < Left.Num(); ++Index )
		{
			if ( Left[Index].r == Right[Index].r && Left[Index].g == Right[Index].g && Left[Index].b == Right[Index].b && Left[Index].a == Right[Index].a ) continue;
			++Result.Count;
			if ( Result.FirstX < 0 ) { Result.FirstX = Index % Width; Result.FirstY = Index / Width; Result.Left = Left[Index]; Result.Right = Right[Index]; }
		}
		return Result;
	}
}

// 実D3D11の帯描画で点別UVゆがみ、ghost分割、状態復元をpixel確認する。
void NWorldTest::RunEffectRibbonUVDistortionRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Dir )
{
	C.Group = "effect-ribbon-uv-distortion-real";
	// 実D3D11 executorが新しい帯材質命令を実行できるか先に確認する。
	const bool bRibbonDistortionSupported = Executor.SupportsEffectRibbonUVDistortion();
	C.Check( bRibbonDistortionSupported, "実機executorが材質付き帯のUVゆがみをサポートする" );
	if ( !bRibbonDistortionSupported ) return;
	// sandboxへ保存するsource、mask、noise画像。
	const FString SourcePath = "ribbon-uv-distortion/source.tga";
	const FString AlphaPath = "ribbon-uv-distortion/source-alpha.tga";
	const FString MaskPath = "ribbon-uv-distortion/mask.tga";
	const FString NoiseXPath = "ribbon-uv-distortion/noise-x.tga";
	const FString NoiseNegativeXPath = "ribbon-uv-distortion/noise-negative-x.tga";
	const FString NoiseYPath = "ribbon-uv-distortion/noise-y.tga";
	const FString NoiseXYPath = "ribbon-uv-distortion/noise-xy.tga";
	const FString FlowPath = "ribbon-uv-distortion/noise-flow.tga";
	const FString NestedPath = "ribbon-uv-distortion/noise-nested.tga";
	// sandbox内へ全fixture画像を書けたかを示す結果。
	const bool bImagesSaved = NFileSystem::EnsureDirectory( Dir / "ribbon-uv-distortion" ) && NAssetTest::WriteFileBytes( Dir / SourcePath, MakeRibbonSource( false ) ) && NAssetTest::WriteFileBytes( Dir / AlphaPath, MakeRibbonSource( true ) ) && NAssetTest::WriteFileBytes( Dir / MaskPath, MakeRibbonMask() ) && NAssetTest::WriteFileBytes( Dir / NoiseXPath, MakeConstantNoise( 255, 127 ) ) && NAssetTest::WriteFileBytes( Dir / NoiseNegativeXPath, MakeConstantNoise( 0, 127 ) ) && NAssetTest::WriteFileBytes( Dir / NoiseYPath, MakeConstantNoise( 127, 255 ) ) && NAssetTest::WriteFileBytes( Dir / NoiseXYPath, MakeConstantNoise( 255, 255 ) ) && NAssetTest::WriteFileBytes( Dir / FlowPath, MakeFlowNoise() ) && NAssetTest::WriteFileBytes( Dir / NestedPath, MakeNestedNoise() );
	C.Check( bImagesSaved, "帯ゆがみ試験の既知source・mask・noise画像をsandboxへ保存する" );
	if ( !bImagesSaved ) return;
	// fixture rootを切替え、終了時に元rootへ戻すguard。
	FRibbonDistortionAssetRoot AssetRoot( Assets, Dir );
	// 共有asset managerから同期loadする各入力画像。
	TSharedPtr<CTexture> Source = Assets.LoadTexture( SourcePath );
	TSharedPtr<CTexture> AlphaSource = Assets.LoadTexture( AlphaPath );
	TSharedPtr<CTexture> Mask = Assets.LoadTexture( MaskPath );
	TSharedPtr<CTexture> NoiseX = Assets.LoadTexture( NoiseXPath );
	TSharedPtr<CTexture> NoiseNegativeX = Assets.LoadTexture( NoiseNegativeXPath );
	TSharedPtr<CTexture> NoiseY = Assets.LoadTexture( NoiseYPath );
	TSharedPtr<CTexture> NoiseXY = Assets.LoadTexture( NoiseXYPath );
	TSharedPtr<CTexture> Flow = Assets.LoadTexture( FlowPath );
	TSharedPtr<CTexture> NestedParent = Assets.LoadTexture( NestedPath );
	C.Check( Source.IsValid() && Source->IsReady() && AlphaSource.IsValid() && AlphaSource->IsReady() && Mask.IsValid() && Mask->IsReady() && NoiseX.IsValid() && NoiseX->IsReady() && NoiseNegativeX.IsValid() && NoiseNegativeX->IsReady() && NoiseY.IsValid() && NoiseY->IsReady() && NoiseXY.IsValid() && NoiseXY->IsReady() && Flow.IsValid() && Flow->IsReady() && NestedParent.IsValid() && NestedParent->IsReady(), "全ての既知画像がGPU読込可能になる" );
	if ( !Source.IsValid() || !Source->IsReady() || !AlphaSource.IsValid() || !AlphaSource->IsReady() || !Mask.IsValid() || !Mask->IsReady() || !NoiseX.IsValid() || !NoiseX->IsReady() || !NoiseNegativeX.IsValid() || !NoiseNegativeX->IsReady() || !NoiseY.IsValid() || !NoiseY->IsReady() || !NoiseXY.IsValid() || !NoiseXY->IsReady() || !Flow.IsValid() || !Flow->IsReady() || !NestedParent.IsValid() || !NestedParent->IsReady() ) return;
	// NPOT親から右半分を切り、その内側をもう一度派生させるnoise handle。
	const int32 NestedRegion = DerivationGraph( 5, 0, 5, 7, NestedParent->GetHandle() );
	const int32 NestedNoise = NestedRegion >= 0 ? DerivationGraph( 1, 1, 3, 4, NestedRegion ) : -1;
	// 128x128深度付きscreenと全画素readback領域。
	const int32 Screen = MakeScreen( 128, 128, TRUE );
	const int32 Soft = MakeARGB8ColorSoftImage( 128, 128 );
	C.Check( Screen >= 0 && Soft >= 0 && NestedRegion >= 0 && NestedNoise >= 0, "screen・readback・nested noise regionを作れる" );
	if ( Screen < 0 || Soft < 0 || NestedRegion < 0 || NestedNoise < 0 ) { if ( Screen >= 0 ) DeleteGraph( Screen ); if ( Soft >= 0 ) DeleteSoftImage( Soft ); if ( NestedRegion >= 0 ) DeleteGraph( NestedRegion ); if ( NestedNoise >= 0 ) DeleteGraph( NestedNoise ); return; }
	{
		// cameraとDxLib描画状態をこのscreenへ固定する。
		FRibbonDistortionCallerState Caller;
		SetDrawScreen( Screen ); SetDrawAreaFull(); SetCameraNearFar( 0.1f, 100.0f ); SetCameraDotAspect( 1.0f ); SetCameraScreenCenter( 64.0f, 64.0f ); SetupCamera_Ortho( 8.0f ); SetCameraPositionAndTarget_UpVecY( VGet( 0.0f, 0.0f, -10.0f ), VGet( 0.0f, 0.0f, 0.0f ) );
		// 128x128画面を投影先にしworldをidentityにする。
		MATRIX Viewport{}; CreateViewportMatrix( &Viewport, 64.0f, 64.0f, 128.0f, 128.0f ); SetTransformToViewport( &Viewport ); const MATRIX Identity = MGetIdent(); SetTransformToWorld( &Identity );
		SetDrawBright( 255, 255, 255 ); SetBackgroundColor( 12, 12, 12, 255 );
		C.Check( SetUseGraphZBuffer( Screen, TRUE ) == 0, "native材質帯用screenへZ bufferを割り当てる" );
		RenderVertex();
		// 新commandへ渡す4点の曲線と点別uv/strength属性。
		FDrawDistortedMaterialRibbonData::FPoint Bend[4] = { MakeDistortionPoint( -2.0f, -1.0f, 0.0f, 0.05f, 0.90f, 0.10f, 0.90f, 0.0f, 0.0f, 1.0f, 0xC0FFFFFFu ), MakeDistortionPoint( -0.5f, 1.0f, 0.33f, 0.35f, 1.00f, 0.20f, 1.00f, 0.0f, 0.0f, 1.2f, 0xD0FFFFFFu ), MakeDistortionPoint( 0.8f, -0.4f, 0.67f, 0.70f, 1.10f, 0.30f, 1.10f, 0.0f, 0.0f, 1.6f, 0xE0FFFFFFu ), MakeDistortionPoint( 2.0f, 1.0f, 1.0f, 0.40f, 1.20f, 0.40f, 1.20f, 0.0f, 0.0f, 1.0f, 0xFFFFFFFFu ) };
		// 曲線のghost比較に使う固定材質とnoise設定。
		FDrawDistortedMaterialRibbonData Data{};
		Data.Material.Ribbon.Texture = Source->GetHandle(); Data.Material.Ribbon.NumPoints = 4; Data.Material.Ribbon.UVRepeat = 1.0f; Data.Material.Material.MaskTexture = -1; Data.Material.Material.DepthTest = 1; Data.Material.Material.DepthWrite = 0; Data.Material.Material.UVScale[0] = 1.5f; Data.Material.Material.UVScale[1] = 1.25f; Data.DistortionTexture = NoiseX->GetHandle(); Data.UVScale[0] = 1.25f; Data.UVScale[1] = 1.5f;
		// readback領域から一pixelを色として取得する関数。
		const auto Pixel = [&]( int32 X, int32 Y ) { int32 R = 0, G = 0, B = 0, A = 0; GetPixelSoftImage( Soft, X, Y, &R, &G, &B, &A ); return FColor32( static_cast<uint8>( R ), static_cast<uint8>( G ), static_cast<uint8>( B ), static_cast<uint8>( A ) ); };
		// new distorted ribbon commandを描いてGPU結果をSoftImageへ読む。
		const auto RenderDistorted = [&]( const FDrawDistortedMaterialRibbonData& Ribbon, const FDrawDistortedMaterialRibbonData::FPoint* Points, EDrawBlend Blend = EDrawBlend::Alpha ) { SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen(); FDrawBuffer Buffer; FDrawContext Draw( Buffer ); Draw.Blend( Blend, 255 ); Draw.DistortedMaterialRibbon3D( Ribbon, Points ); const int32 RecordedWords = Draw.GetNumWords(); if ( !Buffer.Words.empty() ) Executor.Execute( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft ); return RecordedWords; };
		// 旧MaterialRibbon命令を同じ形状・blendで描いてGPU結果を読む。
		const auto RenderMaterial = [&]( const FDrawMaterialRibbonData& Ribbon, const FDrawMaterialRibbonData::FPoint* Points, EDrawBlend Blend = EDrawBlend::Alpha ) { SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen(); FDrawBuffer Buffer; FDrawContext Draw( Buffer ); Draw.Blend( Blend, 255 ); Draw.MaterialRibbon3D( Ribbon, Points ); if ( !Buffer.Words.empty() ) Executor.Execute( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft ); };
		// 128x128全画面readbackを独立配列へ保存する関数。
		const auto CapturePixels = [&]() { TArray<FColor32> Pixels; Pixels.SetNum( 128 * 128 ); for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X ) Pixels[Y * 128 + X] = Pixel( X, Y ); return Pixels; };
		// 描画が空でないときだけ使う画素数。
		const auto CountVisiblePixels = [&]( const TArray<FColor32>& Pixels ) { int32 Count = 0; for ( int32 Index = 0; Index < Pixels.Num(); ++Index ) Count += Pixels[Index].r > 20 || Pixels[Index].g > 20 || Pixels[Index].b > 20 ? 1 : 0; return Count; };
		// active distortionを一命令で描いた比較基準画像。
		const int32 FullCommandWords = RenderDistorted( Data, Bend );
		const TArray<FColor32> FullPixels = CapturePixels();
		const int32 FullVisiblePixels = CountVisiblePixels( FullPixels );
		C.Check( FullCommandWords == 3 + 1 + 68 / 4 + 4 * 52 / 4 && FullVisiblePixels > 100, "曲がった半透明帯は有効な1命令で十分な実画素を描く" );
		// ghost点で同じ曲線を二命令へ分けて再描画する。
		FDrawDistortedMaterialRibbonData::FPoint SplitFirst[4] = { Bend[0], Bend[1], Bend[2], Bend[3] };
		FDrawDistortedMaterialRibbonData::FPoint SplitSecond[3] = { Bend[1], Bend[2], Bend[3] };
		FDrawDistortedMaterialRibbonData FirstData = Data; FirstData.Material.Ribbon.NumPoints = 4; FirstData.Material.Ribbon.JoinFlags = FDrawRibbonData::NextPoint;
		FDrawDistortedMaterialRibbonData SecondData = Data; SecondData.Material.Ribbon.NumPoints = 3; SecondData.Material.Ribbon.JoinFlags = FDrawRibbonData::PreviousPoint;
		SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen(); FDrawBuffer SplitBuffer; FDrawContext SplitDraw( SplitBuffer ); SplitDraw.Blend( EDrawBlend::Alpha, 255 ); SplitDraw.DistortedMaterialRibbon3D( FirstData, SplitFirst ); SplitDraw.DistortedMaterialRibbon3D( SecondData, SplitSecond ); Executor.Execute( FDrawSpan{ SplitBuffer.Words.data(), static_cast<int32>( SplitBuffer.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		const TArray<FColor32> SplitPixels = CapturePixels();
		// ghost比較で差が出た場合の画素数と最初のRGBA差。
		const FPixelDifference GhostDifference = DescribePixelDifference( FullPixels, SplitPixels, 128 );
		C.Check( FullVisiblePixels > 100 && GhostDifference.Count == 0, "曲がった半透明帯のnoise・点別強度・連続scrollはghost分割後も全RGBA画素で一致する: visible=%d diff=%d first=(%d,%d) full=(%d,%d,%d,%d) split=(%d,%d,%d,%d)", FullVisiblePixels, GhostDifference.Count, GhostDifference.FirstX, GhostDifference.FirstY, GhostDifference.Left.r, GhostDifference.Left.g, GhostDifference.Left.b, GhostDifference.Left.a, GhostDifference.Right.r, GhostDifference.Right.g, GhostDifference.Right.b, GhostDifference.Right.a );
		// ghost比較の一命令結果を証拠PNGとしてsandbox Savedへ残す。
		const FString ProofDirectory = FString( "Saved" ) / "EffectRibbonUVDistortionProof";
		if ( NFileSystem::EnsureDirectory( ProofDirectory ) ) SaveDrawScreenToPNG( 0, 0, 128, 128, *( ProofDirectory / "ribbon-uv-ghost-split.png" ) );
		// pointごとに強度を0へし、旧MaterialRibbonの画素と比較する。
		FDrawDistortedMaterialRibbonData::FPoint ZeroStrength[4] = { Bend[0], Bend[1], Bend[2], Bend[3] };
		for ( int32 Index = 0; Index < 4; ++Index ) ZeroStrength[Index].Strength = 0.0f;
		FDrawMaterialRibbonData LegacyData = Data.Material; FDrawMaterialRibbonData::FPoint LegacyPoints[4] = { Bend[0].Material, Bend[1].Material, Bend[2].Material, Bend[3].Material };
		RenderMaterial( LegacyData, LegacyPoints ); const TArray<FColor32> LegacyPixels = CapturePixels();
		const int32 ZeroCommandWords = RenderDistorted( Data, ZeroStrength ); const TArray<FColor32> ZeroPixels = CapturePixels();
		C.Check( FullVisiblePixels > 100 && ZeroCommandWords > 0 && CountVisiblePixels( ZeroPixels ) > 100 && EqualPixels( LegacyPixels, ZeroPixels ), "強度0の新帯は非空の旧MaterialRibbonと全RGBA画素が一致する" );
		// 一つ目のpngはnoiseを有効にした完全な描画結果へ更新する。
		RenderDistorted( Data, Bend ); SaveDrawScreenToPNG( 0, 0, 128, 128, *( ProofDirectory / "ribbon-uv-distortion.png" ) );
		// 既知noise方向がsource四象限を正しく移す直線帯。
		FDrawDistortedMaterialRibbonData::FPoint Straight[2] = { MakeDistortionPoint( -2.0f, 0.0f, 0.0f, 0.35f, 0.0f, 0.0f ), MakeDistortionPoint( 2.0f, 0.0f, 1.0f, 0.35f, 0.0f, 0.0f ) };
		FDrawDistortedMaterialRibbonData DirectionData{}; DirectionData.Material.Ribbon.Texture = Source->GetHandle(); DirectionData.Material.Ribbon.NumPoints = 2; DirectionData.Material.Ribbon.UVRepeat = 1.0f; DirectionData.Material.Material.MaskTexture = -1; DirectionData.DistortionTexture = NoiseX->GetHandle();
		RenderDistorted( DirectionData, Straight ); const FColor32 PositiveXPixel = Pixel( 48, 58 );
		DirectionData.DistortionTexture = NoiseNegativeX->GetHandle(); RenderDistorted( DirectionData, Straight ); const FColor32 NegativeXPixel = Pixel( 80, 58 );
		DirectionData.DistortionTexture = NoiseY->GetHandle(); RenderDistorted( DirectionData, Straight ); const FColor32 PositiveYPixel = Pixel( 48, 58 );
		DirectionData.DistortionTexture = NoiseXY->GetHandle(); RenderDistorted( DirectionData, Straight ); const FColor32 PositiveXYPixel = Pixel( 48, 58 );
		C.Check( PositiveXPixel.g > PositiveXPixel.r + 80 && NegativeXPixel.r > NegativeXPixel.g + 80 && PositiveYPixel.b > PositiveYPixel.r + 80 && PositiveXYPixel.r > 150 && PositiveXYPixel.g > 150 && PositiveXYPixel.b < 50, "既知noiseの正負X・正Y・XYが異なるsource四象限を選ぶ: +X=(%d,%d,%d,%d) -X=(%d,%d,%d,%d) +Y=(%d,%d,%d,%d) XY=(%d,%d,%d,%d)", PositiveXPixel.r, PositiveXPixel.g, PositiveXPixel.b, PositiveXPixel.a, NegativeXPixel.r, NegativeXPixel.g, NegativeXPixel.b, NegativeXPixel.a, PositiveYPixel.r, PositiveYPixel.g, PositiveYPixel.b, PositiveYPixel.a, PositiveXYPixel.r, PositiveXYPixel.g, PositiveXYPixel.b, PositiveXYPixel.a );
		// scaleとsource UV移動をnoise座標と分けて設定する。
		FDrawDistortedMaterialRibbonData::FPoint Scaled[2] = { MakeDistortionPoint( -2.0f, 0.0f, 0.0f, 0.25f, 0.15f, 0.0f, -0.25f ), MakeDistortionPoint( 2.0f, 0.0f, 1.0f, 0.25f, 0.15f, 0.0f, -0.25f ) };
		DirectionData.DistortionTexture = NoiseX->GetHandle(); DirectionData.UVScale[0] = 2.25f; DirectionData.UVScale[1] = 1.5f; DirectionData.Material.Material.UVScale[0] = 1.5f; DirectionData.Material.Material.UVScale[1] = 1.25f;
		// 倍率付きnoise帯を記録したcommand word数。
		const int32 ScaledWords = RenderDistorted( DirectionData, Scaled );
		// source scaleとnoise offsetを区別する代表pixel。
		const FColor32 ScaledPixel = Pixel( 44, 58 );
		C.Check( ScaledWords > 0 && DirectionData.UVScale[0] == 2.25f && DirectionData.Material.Material.UVScale[0] == 1.5f && ScaledPixel.r > ScaledPixel.g + 80, "sourceの非整数scale・scroll後にnoise変位を二重scaleせず既知の左上色を選ぶ: pixel=(%d,%d,%d,%d) words=%d", ScaledPixel.r, ScaledPixel.g, ScaledPixel.b, ScaledPixel.a, ScaledWords );
		// 位置によって符号が異なるnoiseを2.5倍で読み、倍率の適用位置を画素で区別する。
		FDrawDistortedMaterialRibbonData FlowScaledData = DirectionData; FlowScaledData.DistortionTexture = Flow->GetHandle(); FlowScaledData.UVScale[0] = 2.5f; FlowScaledData.UVScale[1] = 1.0f; FlowScaledData.Material.Material.UVScale[0] = 1.0f; FlowScaledData.Material.Material.UVScale[1] = 1.0f;
		// noiseの符号が逆なら左右で色が変わるsource座標へ置く点列。
		FDrawDistortedMaterialRibbonData::FPoint FlowScaledPoints[2] = { MakeDistortionPoint( -2.0f, 0.0f, 0.0f, 0.35f, 0.0f, 0.0f, 0.25f ), MakeDistortionPoint( 2.0f, 0.0f, 1.0f, 0.35f, 0.0f, 0.0f, 0.25f ) };
		// noise倍率を適用して描いた命令長と代表色。
		// 非均一noiseを倍率付きで描いたcommand word数。
		const int32 FlowScaledWords = RenderDistorted( FlowScaledData, FlowScaledPoints );
		// scale位置の誤りを検出する代表pixel。
		const FColor32 FlowScaledPixel = Pixel( 52, 58 );
		C.Check( FlowScaledWords > 0 && FlowScaledPixel.r > FlowScaledPixel.g + 80, "非均一noiseの非整数UV倍率が反対符号の既知赤色を選ぶ: pixel=(%d,%d,%d,%d) words=%d", FlowScaledPixel.r, FlowScaledPixel.g, FlowScaledPixel.b, FlowScaledPixel.a, FlowScaledWords );
		// 点のnoise offsetを0.9から1.1へ連続補間し、frac後のflowを得る。
		FDrawDistortedMaterialRibbonData::FPoint WrapPositive[2] = { MakeDistortionPoint( -2.0f, 0.0f, 0.0f, 0.35f, 0.9f, 0.0f ), MakeDistortionPoint( 2.0f, 0.0f, 1.0f, 0.35f, 1.1f, 0.0f ) };
		DirectionData.Material.Material.UVScale[0] = 1.0f; DirectionData.Material.Material.UVScale[1] = 1.0f; DirectionData.UVScale[0] = 1.0f; DirectionData.UVScale[1] = 1.0f; DirectionData.DistortionTexture = Flow->GetHandle();
		RenderDistorted( DirectionData, WrapPositive );
		// 正側offsetを描いた左側の既知色。
		const FColor32 WrapPositiveLeft = Pixel( 48, 58 );
		// 正側offsetを描いた右側の既知色。
		const FColor32 WrapPositiveRight = Pixel( 80, 58 );
		// 正側offset画像の全画素比較用snapshot。
		const TArray<FColor32> WrapPositivePixels = CapturePixels();
		// 負側offset-0.1..0.1は正側0.9..1.1と一周期差だけ持つ。
		FDrawDistortedMaterialRibbonData::FPoint WrapNegative[2] = { WrapPositive[0], WrapPositive[1] }; WrapNegative[0].UVOffset[0] = -0.1f; WrapNegative[1].UVOffset[0] = 0.1f;
		RenderDistorted( DirectionData, WrapNegative );
		// 負側offset画像の全画素比較用snapshot。
		const TArray<FColor32> WrapNegativePixels = CapturePixels();
		const FPixelDifference WrapDifference = DescribePixelDifference( WrapPositivePixels, WrapNegativePixels, 128 );
		C.Check( WrapPositiveLeft.g > WrapPositiveLeft.r + 80 && WrapPositiveRight.r > WrapPositiveRight.g + 80 && WrapDifference.Count == 0, "0.9から1.1のnoise移動は点補間後にwrapし、負側一周期差と既知画素が一致する: left=(%d,%d,%d,%d) right=(%d,%d,%d,%d) diff=%d first=(%d,%d)", WrapPositiveLeft.r, WrapPositiveLeft.g, WrapPositiveLeft.b, WrapPositiveLeft.a, WrapPositiveRight.r, WrapPositiveRight.g, WrapPositiveRight.b, WrapPositiveRight.a, WrapDifference.Count, WrapDifference.FirstX, WrapDifference.FirstY );
		// 同じU位置でnoise offsetだけを0.5変え、FlowNoiseの反対側を読む点列。
		FDrawDistortedMaterialRibbonData::FPoint NoiseOffsetZero[2] = { MakeDistortionPoint( -2.0f, 0.0f, 0.0f, 0.35f, 0.0f, 0.0f ), MakeDistortionPoint( 2.0f, 0.0f, 1.0f, 0.35f, 0.0f, 0.0f ) };
		// 同じ形状へnoiseの横offsetだけを0.5加える点列。
		FDrawDistortedMaterialRibbonData::FPoint NoiseOffsetHalf[2] = { NoiseOffsetZero[0], NoiseOffsetZero[1] };
		NoiseOffsetHalf[0].UVOffset[0] = 0.5f; NoiseOffsetHalf[1].UVOffset[0] = 0.5f;
		// offset比較用にscaleとsource設定を単位倍率・scrollなしへ固定するheader。
		FDrawDistortedMaterialRibbonData NoiseOffsetData = DirectionData;
		NoiseOffsetData.Material.Ribbon.Texture = Source->GetHandle(); NoiseOffsetData.Material.Material.MaskTexture = -1; NoiseOffsetData.Material.Material.bDissolve = 0; NoiseOffsetData.Material.Material.UVScale[0] = 1.0f; NoiseOffsetData.Material.Material.UVScale[1] = 1.0f; NoiseOffsetData.UVScale[0] = 1.0f; NoiseOffsetData.UVScale[1] = 1.0f; NoiseOffsetData.DistortionTexture = Flow->GetHandle();
		// zero offset時に帯を記録した命令語数。
		const int32 NoiseOffsetZeroWords = RenderDistorted( NoiseOffsetData, NoiseOffsetZero );
		// zero offset時のx56,y58にある既知緑画素。
		const FColor32 NoiseOffsetZeroPixel = Pixel( 56, 58 );
		// 0.5 offset時に帯を記録した命令語数。
		const int32 NoiseOffsetHalfWords = RenderDistorted( NoiseOffsetData, NoiseOffsetHalf );
		// 0.5 offset時のx56,y58にある既知赤画素。
		const FColor32 NoiseOffsetHalfPixel = Pixel( 56, 58 );
		C.Check( NoiseOffsetZeroWords > 0 && NoiseOffsetHalfWords > 0 && NoiseOffsetZeroPixel.g > NoiseOffsetZeroPixel.r + 80 && NoiseOffsetHalfPixel.r > NoiseOffsetHalfPixel.g + 80, "point noise offset 0.5はFlowNoiseを反転側から読みsource色を緑から赤へ移す: zero=(%d,%d,%d,%d) half=(%d,%d,%d,%d) words=(%d,%d)", NoiseOffsetZeroPixel.r, NoiseOffsetZeroPixel.g, NoiseOffsetZeroPixel.b, NoiseOffsetZeroPixel.a, NoiseOffsetHalfPixel.r, NoiseOffsetHalfPixel.g, NoiseOffsetHalfPixel.b, NoiseOffsetHalfPixel.a, NoiseOffsetZeroWords, NoiseOffsetHalfWords );
		// 点別strengthを補間したとき右側だけsource反対側へ移る帯。
		FDrawDistortedMaterialRibbonData::FPoint StrengthPoints[2] = { MakeDistortionPoint( -2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f ), MakeDistortionPoint( 2.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f ) };
		// strengthだけを無効にした画素比較用の点列。
		FDrawDistortedMaterialRibbonData::FPoint StrengthZero[2] = { StrengthPoints[0], StrengthPoints[1] }; StrengthZero[1].Strength = 0.0f;
		DirectionData.DistortionTexture = NoiseX->GetHandle(); DirectionData.UVScale[0] = 1.0f; DirectionData.UVScale[1] = 1.0f; DirectionData.Material.Material.UVScale[0] = 1.0f; DirectionData.Material.Material.UVScale[1] = 1.0f;
		// 点別strength有効時と0時に同じ代表画素から読む色。
		RenderDistorted( DirectionData, StrengthPoints );
		// 点ごとの強度補間を有効にした代表pixel。
		const FColor32 InterpolatedStrengthPixel = Pixel( 72, 58 );
		RenderDistorted( DirectionData, StrengthZero );
		// 同じ点で強度を0にしたときの代表pixel。
		const FColor32 ZeroStrengthPixel = Pixel( 72, 58 );
		C.Check( InterpolatedStrengthPixel.r > InterpolatedStrengthPixel.g + 80 && ZeroStrengthPixel.g > ZeroStrengthPixel.r + 80, "点別strength補間が右端のsource象限を変え、0では元象限を保つ: strength=(%d,%d,%d,%d) zero=(%d,%d,%d,%d)", InterpolatedStrengthPixel.r, InterpolatedStrengthPixel.g, InterpolatedStrengthPixel.b, InterpolatedStrengthPixel.a, ZeroStrengthPixel.r, ZeroStrengthPixel.g, ZeroStrengthPixel.b, ZeroStrengthPixel.a );
		// NPOT親の右cropとnested領域は中立noiseだけをsampleする。
		FDrawDistortedMaterialRibbonData::FPoint NestedPoints[2] = { MakeDistortionPoint( -2.0f, 0.0f, 0.0f, 0.35f, 0.0f, 0.0f ), MakeDistortionPoint( 2.0f, 0.0f, 1.0f, 0.35f, 0.0f, 0.0f ) };
		DirectionData.DistortionTexture = NestedNoise; DirectionData.UVScale[0] = 1.0f; DirectionData.UVScale[1] = 1.0f; DirectionData.Material.Material.UVScale[0] = 1.0f; DirectionData.Material.Material.UVScale[1] = 1.0f;
		RenderDistorted( DirectionData, NestedPoints ); const FColor32 NestedLeft = Pixel( 48, 50 ); const FColor32 NestedRight = Pixel( 80, 78 );
		C.Check( NestedLeft.r > 180 && NestedLeft.g < 50 && NestedRight.r > 170 && NestedRight.g > 150 && NestedRight.b < 60, "NPOT nested noise cropは隣の正方向pixelを混ぜない" );
		// maskはsource distortionから独立し、穴・edge・emissionを保つ。
		FDrawDistortedMaterialRibbonData::FPoint Masked[2] = { MakeDistortionPoint( -2.0f, 0.0f, 0.0f, 0.35f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 2.0f ), MakeDistortionPoint( 2.0f, 0.0f, 1.0f, 0.35f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 2.0f ) };
		DirectionData.Material.Ribbon.Texture = Source->GetHandle(); DirectionData.Material.Material.MaskTexture = Mask->GetHandle(); DirectionData.Material.Material.bDissolve = 1; DirectionData.Material.Material.EdgeWidth = 0.2f; DirectionData.Material.Material.EdgeEmission = 4.0f; DirectionData.Material.Material.EdgeColor = { 0, 255, 0, 255 }; DirectionData.DistortionTexture = NoiseX->GetHandle();
		RenderDistorted( DirectionData, Masked ); const FColor32 MaskHole = Pixel( 48, 64 ); const FColor32 MaskAlive = Pixel( 80, 64 ); const FColor32 MaskEdge = Pixel( 64, 64 );
		C.Check( MaskHole.r < 20 && MaskHole.g < 20 && MaskAlive.r > 20 && MaskEdge.g > MaskAlive.g, "noise distortionはmask穴を動かさず、生存側へedge emissionを加える" );
		// source offsetなしのmask穴をscroll後と同じ位置から読む。
		const FColor32 MaskUnscrolledHole = Pixel( 48, 58 );
		// source offsetなしの生存画素色をscroll後と同じ位置から読む。
		const FColor32 MaskUnscrolledAlive = Pixel( 80, 58 );
		// 元のmask境界を保ったままsource色だけを半周期ずらす材質点列。
		FDrawDistortedMaterialRibbonData::FPoint MaskedScrolled[2] = { Masked[0], Masked[1] };
		MaskedScrolled[0].Material.UVOffset[0] = 0.5f; MaskedScrolled[1].Material.UVOffset[0] = 0.5f;
		// source scrollだけを加えた帯を描く記録語数と画素結果。
		const int32 MaskScrollWords = RenderDistorted( DirectionData, MaskedScrolled );
		// source offset適用後も消失側に残るx48,y58のpixel。
		const FColor32 MaskScrolledHole = Pixel( 48, 58 );
		// source offsetで緑へ移り、生存側に残るx80,y58のpixel。
		const FColor32 MaskScrolledAlive = Pixel( 80, 58 );
		C.Check( MaskScrollWords > 0 && MaskUnscrolledHole.r <= 13 && MaskUnscrolledHole.g <= 13 && MaskScrolledHole.r <= 13 && MaskScrolledHole.g <= 13 && MaskUnscrolledAlive.r > MaskUnscrolledAlive.g + 80 && MaskScrolledAlive.g > MaskScrolledAlive.r + 80, "source offset .5で同じalive pixelは赤から緑へ移るがmask穴はx48に残りx80は生存する: baseHole=(%d,%d,%d,%d) hole=(%d,%d,%d,%d) baseAlive=(%d,%d,%d,%d) scrolledAlive=(%d,%d,%d,%d) words=%d", MaskUnscrolledHole.r, MaskUnscrolledHole.g, MaskUnscrolledHole.b, MaskUnscrolledHole.a, MaskScrolledHole.r, MaskScrolledHole.g, MaskScrolledHole.b, MaskScrolledHole.a, MaskUnscrolledAlive.r, MaskUnscrolledAlive.g, MaskUnscrolledAlive.b, MaskUnscrolledAlive.a, MaskScrolledAlive.r, MaskScrolledAlive.g, MaskScrolledAlive.b, MaskScrolledAlive.a, MaskScrollWords );
		// 右上alpha0と右下alpha128を含むsourceで強いemissionのalphaを検査する。
		FDrawDistortedMaterialRibbonData::FPoint AlphaPoints[2] = { MakeDistortionPoint( -2.0f, 0.0f, 0.0f, 0.35f, 0.0f, 0.0f ), MakeDistortionPoint( 2.0f, 0.0f, 1.0f, 0.35f, 0.0f, 0.0f ) };
		DirectionData.Material.Ribbon.Texture = AlphaSource->GetHandle(); DirectionData.Material.Material.MaskTexture = -1; DirectionData.Material.Material.bDissolve = 0; DirectionData.Material.Material.EdgeWidth = 0.0f; DirectionData.DistortionTexture = NoiseX->GetHandle();
		RenderDistorted( DirectionData, AlphaPoints ); const FColor32 AlphaZero = Pixel( 68, 52 ); const FColor32 AlphaHalf = Pixel( 68, 76 );
		AlphaPoints[0].Material.Emission = 32.0f; AlphaPoints[1].Material.Emission = 32.0f; RenderDistorted( DirectionData, AlphaPoints ); const FColor32 BrightAlphaZero = Pixel( 68, 52 ); const FColor32 BrightAlphaHalf = Pixel( 68, 76 );
		C.Check( AlphaZero.r < 20 && BrightAlphaZero.r < 20 && AlphaHalf.r >= 112 && AlphaHalf.r <= 132 && AlphaHalf.g >= 106 && AlphaHalf.g <= 126 && BrightAlphaHalf.r >= 124 && BrightAlphaHalf.r <= 145 && BrightAlphaHalf.g >= 124 && BrightAlphaHalf.g <= 145 && BrightAlphaHalf.b >= 124 && BrightAlphaHalf.b <= 145, "画像alpha0は変位や32倍emissionで復活せずalpha128は背景へ合成される: a0=(%d,%d,%d,%d) a128=(%d,%d,%d,%d) bright0=(%d,%d,%d,%d) bright128=(%d,%d,%d,%d)", AlphaZero.r, AlphaZero.g, AlphaZero.b, AlphaZero.a, AlphaHalf.r, AlphaHalf.g, AlphaHalf.b, AlphaHalf.a, BrightAlphaZero.r, BrightAlphaZero.g, BrightAlphaZero.b, BrightAlphaZero.a, BrightAlphaHalf.r, BrightAlphaHalf.g, BrightAlphaHalf.b, BrightAlphaHalf.a );
		// t2へcaller SRVを設定し、新command後に参照が復元されるか確かめる。
		ID3D11Device* Device = const_cast<ID3D11Device*>( static_cast<const ID3D11Device*>( GetUseDirect3D11Device() ) );
		ID3D11DeviceContext* NativeContext = const_cast<ID3D11DeviceContext*>( static_cast<const ID3D11DeviceContext*>( GetUseDirect3D11DeviceContext() ) );
		ID3D11ShaderResourceView* SentinelView = nullptr;
		ID3D11ShaderResourceView* OriginalView = nullptr;
		const void* SentinelTexture = GetGraphID3D11Texture2D( Flow->GetHandle() );
		const bool bSentinelCreated = Device != nullptr && NativeContext != nullptr && SentinelTexture != nullptr && SUCCEEDED( Device->CreateShaderResourceView( static_cast<ID3D11Resource*>( const_cast<void*>( SentinelTexture ) ), nullptr, &SentinelView ) );
		// t2退避fixture中材質帯が実際に記録・描画されたかを保持する結果。
		bool bSentinelDrawn = false;
		if ( bSentinelCreated )
		{
			NativeContext->PSGetShaderResources( 2, 1, &OriginalView ); RenderVertex(); ClearDrawScreen(); RenderVertex(); NativeContext->PSSetShaderResources( 2, 1, &SentinelView );
			DirectionData.Material.Ribbon.Texture = Source->GetHandle(); DirectionData.Material.Material.MaskTexture = -1; DirectionData.Material.Material.bDissolve = 0; DirectionData.DistortionTexture = NoiseX->GetHandle(); DirectionData.Material.Ribbon.NumPoints = 2;
			// t2 sentinelを残したまま帯命令を作る一時buffer。
			FDrawBuffer SentinelBuffer;
			// caller state復元中に検査帯を記録する窓口。
			FDrawContext SentinelDraw( SentinelBuffer );
			SentinelDraw.Blend( EDrawBlend::Alpha, 255 ); SentinelDraw.DistortedMaterialRibbon3D( DirectionData, Straight );
			// 記録された帯命令全体のword数。
			const int32 SentinelWords = SentinelDraw.GetNumWords();
			if ( !SentinelBuffer.Words.empty() ) Executor.Execute( FDrawSpan{ SentinelBuffer.Words.data(), static_cast<int32>( SentinelBuffer.Words.size() ) } );
			RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
			// t2 sentinel中に帯が描けたことを確かめる既知pixel。
			const FColor32 SentinelPixel = Pixel( 48, 50 );
			bSentinelDrawn = SentinelWords == 3 + 1 + 68 / 4 + 2 * 52 / 4 && SentinelPixel.g > SentinelPixel.r + 80;
		}
		// 描画後にnative slot t2から読み戻すSRV参照。
		ID3D11ShaderResourceView* RestoredView = nullptr;
		if ( bSentinelCreated ) NativeContext->PSGetShaderResources( 2, 1, &RestoredView );
		const bool bSentinelRestored = bSentinelCreated && RestoredView == SentinelView;
		if ( RestoredView != nullptr ) RestoredView->Release();
		if ( bSentinelCreated ) NativeContext->PSSetShaderResources( 2, 1, &OriginalView );
		if ( OriginalView != nullptr ) OriginalView->Release();
		if ( SentinelView != nullptr ) SentinelView->Release();
		C.Check( bSentinelCreated && bSentinelDrawn && bSentinelRestored, "t2 caller SRVを保ったまま帯を描き、描画後の参照も復元する" );
		// direct rendererを有効payloadで先に通し、以後の異常検査を正しい状態で行う。
		FEffectMaterialRenderer GuardRenderer;
		int32 OldBlend = DX_BLENDMODE_NOBLEND; int32 OldBlendParam = 0; GetDrawBlendMode( &OldBlend, &OldBlendParam ); SetDrawBlendMode( DX_BLENDMODE_ALPHA, 255 );
		RenderVertex(); ClearDrawScreen();
		const bool bDirectPositive = GuardRenderer.DrawDistortedRibbon( DirectionData, Straight );
		RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		// direct rendererが有効帯を描いたことを示すpixel。
		const FColor32 DirectPixel = Pixel( 48, 50 );
		C.Check( bDirectPositive && DirectPixel.g > DirectPixel.r + 80, "Alpha状態でdirect rendererの有効帯が既知の緑画素を描く" );
		// 点の強さ範囲違反を独立して拒否する。
		FDrawDistortedMaterialRibbonData::FPoint InvalidPoint[2] = { Straight[0], Straight[1] }; InvalidPoint[1].Strength = 2.0f;
		const bool bInvalidStrengthRejected = !NDrawDistortedMaterialRibbon::IsValid( DirectionData, InvalidPoint ) && !GuardRenderer.DrawDistortedRibbon( DirectionData, InvalidPoint );
		// noise倍率0を独立して拒否するheader。
		FDrawDistortedMaterialRibbonData InvalidScale = DirectionData; InvalidScale.UVScale[0] = 0.0f;
		const bool bInvalidScaleRejected = !NDrawDistortedMaterialRibbon::IsValidHeader( InvalidScale ) && !GuardRenderer.DrawDistortedRibbon( InvalidScale, Straight );
		// noise画像handle-1を独立して拒否するheader。
		FDrawDistortedMaterialRibbonData InvalidTexture = DirectionData; InvalidTexture.DistortionTexture = -1;
		const bool bInvalidTextureRejected = !NDrawDistortedMaterialRibbon::IsValidHeader( InvalidTexture ) && !GuardRenderer.DrawDistortedRibbon( InvalidTexture, Straight );
		SetDrawBlendMode( OldBlend, OldBlendParam ); RenderVertex(); GuardRenderer.Deinitialize();
		C.Check( bInvalidStrengthRejected, "direct rendererは範囲外の点別強さを拒否する" );
		C.Check( bInvalidScaleRejected, "direct rendererは不正noise倍率を拒否する" );
		C.Check( bInvalidTextureRejected, "direct rendererは欠落noise handleを拒否する" );
		// 生の壊れた命令を流した後も中央pixelが背景色のままかを調べる関数。
		const auto RenderForgedCommand = [&]( const std::vector<uint32>& Payload ) { SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen(); FDrawBuffer Buffer; FDrawContext Draw( Buffer ); Draw.Blend( EDrawBlend::Alpha, 255 ); Buffer.Words.insert( Buffer.Words.end(), Payload.begin(), Payload.end() ); Executor.Execute( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } ); RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft ); return Pixel( 64, 64 ); };
		// 固定headerにも足りない2word命令でexecutorの長さguardを通す値。
		const std::vector<uint32> ShortPayload = { NDrawCommand::MakeHeader( EDrawCommand::DistortedMaterialRibbon3D, 0, 2 ), 0u };
		// short command後に背景色が残るかを読む中央pixel。
		const FColor32 ShortPixel = RenderForgedCommand( ShortPayload );
		C.Check( ShortPixel.r <= 13 && ShortPixel.g <= 13 && ShortPixel.b <= 13, "executorは固定header未満の材質帯命令を描かない" );
		// 正常長だがDepthTestだけ不正にした型付き命令header。
		FDrawDistortedMaterialRibbonData InvalidDepthData = DirectionData; InvalidDepthData.Material.Ribbon.NumPoints = 2; InvalidDepthData.Material.Material.DepthTest = 2;
		// 2点payloadとheaderを含む、完全長forged commandのword配列。
		std::vector<uint32> InvalidDepthPayload( 1 + sizeof( InvalidDepthData ) / 4 + 2 * sizeof( FDrawDistortedMaterialRibbonData::FPoint ) / 4 );
		InvalidDepthPayload[0] = NDrawCommand::MakeHeader( EDrawCommand::DistortedMaterialRibbon3D, 0, static_cast<int32>( InvalidDepthPayload.size() ) ); std::memcpy( InvalidDepthPayload.data() + 1, &InvalidDepthData, sizeof( InvalidDepthData ) ); std::memcpy( InvalidDepthPayload.data() + 1 + sizeof( InvalidDepthData ) / 4, Straight, sizeof( Straight ) );
		// 不正header命令を実行した後の背景pixel。
		const FColor32 InvalidDepthPixel = RenderForgedCommand( InvalidDepthPayload );
		C.Check( InvalidDepthPixel.r <= 13 && InvalidDepthPixel.g <= 13 && InvalidDepthPixel.b <= 13, "executorは完全長でも不正DepthTestの材質帯命令を描かない" );
		// 後続描画の旧quadと旧Ribbonがdistortion後も全pixelを保つか比較する。
		FDrawQuad3DData PlainQuad; PlainQuad.Handle = Source->GetHandle(); PlainQuad.Pos[1] = -1.5f; PlainQuad.Width = 1.5f; PlainQuad.Height = 1.5f; PlainQuad.Color = { 255, 255, 255, 255 };
		FDrawRibbonData::FPoint OldPoints[2] = {};
		OldPoints[0].Pos[0] = -2.0f; OldPoints[0].Pos[1] = 1.5f; OldPoints[0].Width = 0.5f; OldPoints[0].U = 0.0f; OldPoints[0].Color = 0xFFFFFFFFu;
		OldPoints[1].Pos[0] = 2.0f; OldPoints[1].Pos[1] = 1.5f; OldPoints[1].Width = 0.5f; OldPoints[1].U = 1.0f; OldPoints[1].Color = 0xFFFFFFFFu;
		// 旧描画だけと材質帯の後続描画を同じ画面へ描いて比べる関数。
		const auto RenderOldObjects = [&]( bool bAfterDistortion )
		{
			SetDrawScreen( Screen ); SetDrawAreaFull(); ClearDrawScreen();
			if ( bAfterDistortion )
			{
				// 先行する材質帯だけを記録する一時buffer。
				FDrawBuffer NewBuffer;
				// 先行材質帯命令を作成する記録窓口。
				FDrawContext NewDraw( NewBuffer );
				NewDraw.Blend( EDrawBlend::Alpha, 255 ); NewDraw.DistortedMaterialRibbon3D( DirectionData, Straight );
				Executor.Execute( FDrawSpan{ NewBuffer.Words.data(), static_cast<int32>( NewBuffer.Words.size() ) } );
				RenderVertex(); ClearDrawScreen(); RenderVertex();
			}
			// 旧quadと旧Ribbonだけを記録する比較用buffer。
			FDrawBuffer OldBuffer;
			// 旧描画命令を作成する記録窓口。
			FDrawContext OldDraw( OldBuffer );
			OldDraw.Blend( EDrawBlend::Alpha, 255 ); OldDraw.Quad3D( PlainQuad ); OldDraw.Ribbon3D( OldPoints, 2, Source->GetHandle(), 1.0f );
			Executor.Execute( FDrawSpan{ OldBuffer.Words.data(), static_cast<int32>( OldBuffer.Words.size() ) } );
			RenderVertex(); GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
			return CapturePixels();
		};
		// 旧描画だけを出したときの全画素snapshot。
		const TArray<FColor32> OldPixels = RenderOldObjects( false );
		// 材質帯の後に旧描画を出したときの全画素snapshot。
		const TArray<FColor32> OldPixelsAfterDistortion = RenderOldObjects( true );
		const FPixelDifference OldObjectDifference = DescribePixelDifference( OldPixels, OldPixelsAfterDistortion, 128 );
		C.Check( CountVisiblePixels( OldPixels ) > 100 && OldObjectDifference.Count == 0, "新ゆがみ帯の前後でも通常quadと旧Ribbonの全RGBA画素が一致する: visible=%d diff=%d first=(%d,%d) old=(%d,%d,%d,%d) after=(%d,%d,%d,%d)", CountVisiblePixels( OldPixels ), OldObjectDifference.Count, OldObjectDifference.FirstX, OldObjectDifference.FirstY, OldObjectDifference.Left.r, OldObjectDifference.Left.g, OldObjectDifference.Left.b, OldObjectDifference.Left.a, OldObjectDifference.Right.r, OldObjectDifference.Right.g, OldObjectDifference.Right.b, OldObjectDifference.Right.a );
		RenderVertex(); SetDrawScreen( Caller.Screen );
	}
	DeleteGraph( Screen ); DeleteSoftImage( Soft ); DeleteGraph( NestedNoise ); DeleteGraph( NestedRegion );
}
