// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"

#include <cmath>
#include <algorithm>
#include <vector>

#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/FileSystem.h"

namespace
{
	// 実機描画で変更する画面・カメラ・変換・合成状態を呼び出し元へ戻す。
	struct FAppearanceCallerState
	{
		// 呼び出し前の描画先。
		int32 Screen = GetDrawScreen();
		// screen切替でDxLib設定を自動初期化するか。
		int32 ScreenReset = GetUseSetDrawScreenSettingReset();
		// 呼び出し前のカメラ行列。
		MATRIX CameraView = GetCameraViewMatrix();
		// 呼び出し前のカメラ投影行列。
		MATRIX CameraProjection = GetCameraProjectionMatrix();
		// 呼び出し前のWorld/View/Projection/Viewport変換。
		MATRIX World{};
		MATRIX View{};
		MATRIX Projection{};
		MATRIX Viewport{};
		// 呼び出し前のnear/farとdot aspect。
		float32 Near = GetCameraNear();
		float32 Far = GetCameraFar();
		float32 DotAspect = GetCameraDotAspect();
		// 呼び出し前のscreen中心。
		float32 Center[2] = {};
		// 呼び出し前の描画範囲・blend・明るさ・背景。
		RECT DrawArea{};
		int32 Blend[2] = {};
		int32 Bright[3] = {};
		int32 Background[4] = {};

		// 現在設定を取得し、screen切替の自動camera更新を止める。
		FAppearanceCallerState()
		{
			GetTransformToWorldMatrix( &World ); GetTransformToViewMatrix( &View ); GetTransformToProjectionMatrix( &Projection ); GetTransformToViewportMatrix( &Viewport );
			GetCameraScreenCenter( &Center[0], &Center[1] ); GetDrawArea( &DrawArea ); GetDrawBlendMode( &Blend[0], &Blend[1] ); GetDrawBright( &Bright[0], &Bright[1], &Bright[2] ); GetBackgroundColor( &Background[0], &Background[1], &Background[2], &Background[3] );
			SetUseSetDrawScreenSettingReset( FALSE );
		}

		// 描画先、カメラ、blend、背景を呼び出し前の値へ戻す。
		~FAppearanceCallerState()
		{
			RenderVertex(); SetDrawScreen( Screen ); SetCameraNearFar( Near, Far ); SetCameraDotAspect( DotAspect ); SetCameraScreenCenter( Center[0], Center[1] ); SetCameraViewMatrix( CameraView ); SetupCamera_ProjectionMatrix( CameraProjection );
			SetTransformToWorld( &World ); SetTransformToView( &View ); SetTransformToProjection( &Projection ); SetTransformToViewport( &Viewport ); SetDrawArea( DrawArea.left, DrawArea.top, DrawArea.right, DrawArea.bottom ); SetDrawBlendMode( Blend[0], Blend[1] ); SetDrawBright( Bright[0], Bright[1], Bright[2] ); SetBackgroundColor( Background[0], Background[1], Background[2], Background[3] );
			SetUseSetDrawScreenSettingReset( ScreenReset );
		}
	};

	// 一回のPlayback描画から中心色と非背景範囲を返す。
	struct FAppearancePixelResult
	{
		// 画像中心で読んだ合成後RGBA。
		FColor32 Center{};
		// 曲がったRibbon中央の粒子位置で読んだ合成後RGBA。
		FColor32 Middle{};
		// 背景と異なるpixelを含む四角形の幅。
		int32 Width = 0;
		// 背景と異なるpixelを含む四角形の高さ。
		int32 Height = 0;
		// Playbackが素材命令を作りExecutorへ渡せたか。
		bool bRendered = false;
	};

	// Playbackを更新して実機DxLib画面へ記録・描画し、soft imageを読む。
	FAppearancePixelResult DrawAppearancePlayback( const TSharedPtr<IEffectPlayback>& Playback, IDrawExecutor& Executor, int32 Screen, int32 Soft, int32 Background, bool bUseAdd )
	{
		// 描画前に再利用画面を一色で初期化する。
		SetDrawScreen( Screen );
		SetBackgroundColor( Background, Background, Background );
		ClearDrawScreen();
		SetCameraNearFar( 0.1f, 100.0f );
		SetupCamera_Ortho( 8.0f );
		SetCameraPositionAndTarget_UpVecY( VGet( 0.0f, 0.0f, -10.0f ), VGet( 0.0f, 0.0f, 0.0f ) );
		SetCameraDotAspect( 1.0f );
		SetCameraScreenCenter( 64.0f, 64.0f );
		SetDrawAreaFull();
		// DxLibの論理viewportを128x128画面全体へ明示する。
		MATRIX LogicalViewport{};
		CreateViewportMatrix( &LogicalViewport, 64.0f, 64.0f, 128.0f, 128.0f );
		SetTransformToViewport( &LogicalViewport );
		// Playbackのworld座標をそのまま使い、呼出側worldの影響を排除する。
		const MATRIX IdentityWorld = MGetIdent();
		SetTransformToWorld( &IdentityWorld );
		SetDrawBright( 255, 255, 255 );
		// DxLibにlighting状態のgetterがないため、呼出側の値は変更しない。
		// Playbackが自ら生成するDrawCommand列。
		FDrawBuffer Buffer;
		// simulationの結果を描画命令へ渡す窓口。
		FDrawContext Draw( Buffer );
		Draw.Blend( bUseAdd ? EDrawBlend::Add : EDrawBlend::Alpha );
		if ( Playback.IsValid() ) Playback->Record( Draw );
		// Blend command込みのPlayback命令を実機executorへ渡す。
		if ( !Buffer.Words.empty() ) Executor.Execute( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
		RenderVertex();
		GetDrawScreenSoftImage( 0, 0, 128, 128, Soft );
		// 画面中心の色と背景差分の範囲を測る。
		FAppearancePixelResult Result;
		int32 R = 0, G = 0, B = 0, A = 0;
		GetPixelSoftImage( Soft, 64, 64, &R, &G, &B, &A );
		Result.Center = FColor32( static_cast<uint8>( R ), static_cast<uint8>( G ), static_cast<uint8>( B ), static_cast<uint8>( A ) );
		GetPixelSoftImage( Soft, 64, 56, &R, &G, &B, &A );
		Result.Middle = FColor32( static_cast<uint8>( R ), static_cast<uint8>( G ), static_cast<uint8>( B ), static_cast<uint8>( A ) );
		int32 Left = 128, Top = 128, Right = -1, Bottom = -1;
		for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X )
		{
			int32 PixelR = 0, PixelG = 0, PixelB = 0, PixelA = 0;
			GetPixelSoftImage( Soft, X, Y, &PixelR, &PixelG, &PixelB, &PixelA );
			if ( std::abs( PixelR - Background ) + std::abs( PixelG - Background ) + std::abs( PixelB - Background ) < 12 ) continue;
			Left = std::min( Left, X ); Top = std::min( Top, Y ); Right = std::max( Right, X ); Bottom = std::max( Bottom, Y );
		}
		if ( Right >= Left && Bottom >= Top ) { Result.Width = Right - Left + 1; Result.Height = Bottom - Top + 1; Result.bRendered = true; }
		return Result;
	}

	// alpha255の8x8 TGAを作り、RGB/alphaの合成期待値を独立させる。
	std::vector<uint8> MakeOpaqueWhiteTga()
	{
		// TGA無圧縮32bit、上から下へ保存する18byte header。
		std::vector<uint8> Bytes = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 8, 0, 32, 0x28 };
		for ( int32 Index = 0; Index < 64; ++Index ) { Bytes.push_back( 255 ); Bytes.push_back( 255 ); Bytes.push_back( 255 ); Bytes.push_back( 255 ); }
		return Bytes;
	}

	// 二つの実機readbackが画素のRGB/範囲でほぼ一致するか比較する。
	bool SimilarAppearancePixels( const FAppearancePixelResult& A, const FAppearancePixelResult& B )
	{
		return A.bRendered && B.bRendered && std::abs( static_cast<int32>( A.Center.r ) - B.Center.r ) <= 5 && std::abs( static_cast<int32>( A.Center.g ) - B.Center.g ) <= 5 && std::abs( static_cast<int32>( A.Center.b ) - B.Center.b ) <= 5 && std::abs( static_cast<int32>( A.Middle.r ) - B.Middle.r ) <= 5 && std::abs( static_cast<int32>( A.Middle.g ) - B.Middle.g ) <= 5 && std::abs( static_cast<int32>( A.Middle.b ) - B.Middle.b ) <= 5 && std::abs( A.Width - B.Width ) <= 2 && std::abs( A.Height - B.Height ) <= 2;
	}
}

// 実シミュレーション・Playback・DxLib executorを通し、色・幅・合成結果を画素で確認する。
void NWorldTest::RunEffectAppearanceRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Dir )
{
	using namespace NEffectTest;
	C.Group = "effect-appearance-simulation-playback-gpu";
	Assets.SetRootDirectory( Dir );
	// APIで返された以前のDxLib状態を、関数終了時に復元する。
	FAppearanceCallerState CallerState;
	C.Check( Executor.SupportsEffectMaterials(), "D3D11通常実機で材質付きAppearance描画を実行できる" );
	if ( !Executor.SupportsEffectMaterials() ) return;
	// GPUへ渡す白いsoft-dotをtest専用sandboxへ保存する。
	const FString TexturePath = "EffectAppearance/appearance-white.tga";
	const std::vector<uint8> FileBytes = MakeOpaqueWhiteTga();
	// fixture画像の親directoryをsandbox内に確保する。
	const bool bDirectoryReady = NFileSystem::EnsureDirectory( Dir / "EffectAppearance" );
	C.Check( bDirectoryReady && NAssetTest::WriteFileBytes( Dir / TexturePath, FileBytes ), "GPU Appearance用の白い画像をtest sandboxへ書く" );
	const TSharedPtr<CTexture> SourceTexture = Assets.LoadTexture( TexturePath );
	C.Check( SourceTexture.IsValid(), "GPU Appearance用source画像をAssetManagerから読み込む" );
	if ( !SourceTexture.IsValid() ) return;
	// DxLib画面とpixel readback用soft imageをtestだけ所有する。
	const int32 Screen = MakeScreen( 128, 128, TRUE );
	const int32 Soft = MakeARGB8ColorSoftImage( 128, 128 );
	C.Check( Screen >= 0 && Soft >= 0, "Appearance実機pixel test用画面を作成する" );
	if ( Screen < 0 || Soft < 0 ) { if ( Screen >= 0 ) DeleteGraph( Screen ); if ( Soft >= 0 ) DeleteSoftImage( Soft ); return; }
	// testで変更するカメラと画面を保存する。
	// 一つの固定乱数結果を使い、各比較で同じ寿命・位置になる定義。
	FParticleEmitterDefinition Emitter = QuietEmitter();
	Emitter.Texture = TexturePath;
	Emitter.Blend = EParticleBlend::Alpha;
	Emitter.Lifetime = { 2.0f, 2.0f };
	Emitter.StartSize = { 1.0f, 1.0f };
	Emitter.EndSize = { 3.0f, 3.0f };
	Emitter.StartColor = Emitter.EndColor = FColor32( 255, 255, 255, 255 );
	Emitter.Bursts.Add( Burst( 0.0f, 1 ) );
	Emitter.Material.bEnabled = true;
	Emitter.Material.bDissolve = false;
	Emitter.Material.Emission.Keys.Add( { 0.0f, 1.0f } );
	Emitter.Material.Emission.Keys.Add( { 1.0f, 1.0f } );
	Emitter.Appearance.Size.Interpolation = FEffectScalarCurve::EInterpolation::Linear;
	Emitter.Appearance.Size.Keys.Add( { 0.0f, 1.0f } );
	Emitter.Appearance.Size.Keys.Add( { 0.25f, 2.0f } );
	Emitter.Appearance.Size.Keys.Add( { 0.75f, 0.5f } );
	Emitter.Appearance.Size.Keys.Add( { 1.0f, 0.5f } );
	Emitter.Appearance.Color.Interpolation = FEffectScalarCurve::EInterpolation::Linear;
	Emitter.Appearance.Color.Keys.Add( { 0.0f, FColor( 0.1f, 0.8f, 1.0f, 1.0f ) } );
	Emitter.Appearance.Color.Keys.Add( { 0.25f, FColor( 0.1f, 0.8f, 1.0f, 1.0f ) } );
	Emitter.Appearance.Color.Keys.Add( { 0.75f, FColor( 1.0f, 0.25f, 0.1f, 0.5f ) } );
	Emitter.Appearance.Color.Keys.Add( { 1.0f, FColor( 1.0f, 0.25f, 0.1f, 0.5f ) } );
	// .25と.75を再生し、各時刻で材質ON/OFFとAlpha/Addを比較する。
	for ( int32 MaterialIndex = 0; MaterialIndex < 2; ++MaterialIndex ) for ( int32 BlendIndex = 0; BlendIndex < 2; ++BlendIndex )
	{
		Emitter.Material.bEnabled = MaterialIndex != 0;
		Emitter.Blend = BlendIndex == 0 ? EParticleBlend::Alpha : EParticleBlend::Add;
		for ( int32 PhaseIndex = 0; PhaseIndex < 2; ++PhaseIndex )
		{
			// 各phaseで新規simulationを作り、particle ageを0.5秒または1.5秒に固定する。
			TSharedPtr<IEffectPlayback> Playback = NEffect::CreateParticlePlayback( MakeEffect( { Emitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, FString::Printf( "appearance-gpu-%d-%d-%d", MaterialIndex, BlendIndex, PhaseIndex ) );
			if ( !Playback.IsValid() ) { C.Check( false, "Appearance実機fixtureのPlaybackを作れる" ); continue; }
			Playback->Update( 0.01f );
			Playback->Update( PhaseIndex == 0 ? 0.5f : 1.5f );
			// 既知の暗背景上で実際のparticleを描画したreadback。
			const FAppearancePixelResult Pixel = DrawAppearancePlayback( Playback, Executor, Screen, Soft, 24, BlendIndex != 0 );
			C.Check( Pixel.bRendered, "simulation→Playback→DrawContext→Executor→GPU画素の経路が粒子を描く" );
			if ( BlendIndex == 0 && PhaseIndex == 0 ) C.Check( std::abs( static_cast<int32>( Pixel.Center.r ) - 26 ) <= 8 && std::abs( static_cast<int32>( Pixel.Center.g ) - 204 ) <= 12 && Pixel.Center.b > 235, "寿命四分の一のcyanは背景24からRGB=(26,204,255)へ合成される (got %u,%u,%u)", Pixel.Center.r, Pixel.Center.g, Pixel.Center.b );
			if ( BlendIndex == 0 && PhaseIndex == 1 ) C.Check( std::abs( static_cast<int32>( Pixel.Center.r ) - 140 ) <= 10 && std::abs( static_cast<int32>( Pixel.Center.g ) - 44 ) <= 8 && std::abs( static_cast<int32>( Pixel.Center.b ) - 25 ) <= 8, "寿命四分の三の色は背景24へ合成される (got %u,%u,%u)", Pixel.Center.r, Pixel.Center.g, Pixel.Center.b );
			if ( MaterialIndex == 1 && PhaseIndex == 0 ) C.Check( Pixel.Width > 35, "大きさ倍率2を使う早期particleのGPU範囲を読む (got %d x %d)", Pixel.Width, Pixel.Height );
			if ( MaterialIndex == 1 && PhaseIndex == 1 ) C.Check( Pixel.Width > 10 && Pixel.Width < 30, "縮小した後期particleのGPU範囲を読む (got %d x %d)", Pixel.Width, Pixel.Height );
			Playback.Reset();
		}
	}
	// 曲がったRibbonとLightningも実際のsimulationから記録し、材質ON/OFFのpixelを比較する。
	for ( int32 PathIndex = 1; PathIndex < 3; ++PathIndex ) for ( int32 BlendIndex = 0; BlendIndex < 2; ++BlendIndex )
	{
		// 点間で一定の色と幅を使い、材質経路だけを比較する定義。
		FParticleEmitterDefinition PathEmitter = QuietEmitter();
		PathEmitter.Texture = TexturePath;
		PathEmitter.Blend = BlendIndex == 0 ? EParticleBlend::Alpha : EParticleBlend::Add;
		PathEmitter.Lifetime = { 2.0f, 2.0f };
		PathEmitter.StartSize = { 1.0f, 1.0f };
		PathEmitter.EndSize = { 3.0f, 3.0f };
		PathEmitter.StartColor = PathEmitter.EndColor = FColor32( 255, 255, 255, 255 );
		PathEmitter.Appearance.Size.Interpolation = FEffectScalarCurve::EInterpolation::Linear;
		PathEmitter.Appearance.Size.Keys.Add( { 0.0f, 1.0f } );
		PathEmitter.Appearance.Size.Keys.Add( { 0.125f, 1.5f } );
		PathEmitter.Appearance.Size.Keys.Add( { 0.25f, 2.0f } );
		PathEmitter.Appearance.Size.Keys.Add( { 0.5f, 1.5f } );
		PathEmitter.Appearance.Size.Keys.Add( { 1.0f, 1.5f } );
		PathEmitter.Appearance.Color.Interpolation = FEffectScalarCurve::EInterpolation::Linear;
		PathEmitter.Appearance.Color.Keys.Add( { 0.0f, FColor( 1.0f, 1.0f, 1.0f, 1.0f ) } );
		PathEmitter.Appearance.Color.Keys.Add( { 0.125f, FColor( 0.5f, 0.25f, 0.75f, 0.7f ) } );
		PathEmitter.Appearance.Color.Keys.Add( { 0.25f, FColor( 0.5f, 0.25f, 0.75f, 0.5f ) } );
		PathEmitter.Appearance.Color.Keys.Add( { 0.75f, FColor( 1.0f, 0.25f, 0.1f, 0.5f ) } );
		PathEmitter.Appearance.Color.Keys.Add( { 1.0f, FColor( 1.0f, 0.25f, 0.1f, 0.5f ) } );
		PathEmitter.Material.bDissolve = false;
		PathEmitter.Material.EdgeWidth = 0.0f;
		PathEmitter.Material.Emission.Keys.Add( { 0.0f, 1.0f } );
		PathEmitter.Material.Emission.Keys.Add( { 1.0f, 1.0f } );
		if ( PathIndex == 1 ) { PathEmitter.Path.Kind = FEffectPathDefinition::EKind::Ribbon; PathEmitter.Path.BreakDistance = 2.0f; PathEmitter.Path.UVRepeat = 1.0f; PathEmitter.Rate = 4.0f; PathEmitter.Speed = { 0.0f, 0.0f }; PathEmitter.Bursts.Reset(); }
		else { PathEmitter.Path.Kind = FEffectPathDefinition::EKind::Lightning; PathEmitter.Path.End = FVector3( 0.0f, 2.0f, 0.0f ); PathEmitter.Path.Segments = 12; PathEmitter.Path.Branches = 0; PathEmitter.Path.Jitter = 0.0f; PathEmitter.Bursts.Add( Burst( 0.0f, 1 ) ); }
		FAppearancePixelResult PathPixels[2]{};
		for ( int32 MaterialIndex = 0; MaterialIndex < 2; ++MaterialIndex )
		{
			PathEmitter.Material.bEnabled = MaterialIndex != 0;
			TSharedPtr<IEffectPlayback> Playback = NEffect::CreateParticlePlayback( MakeEffect( { PathEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, FString::Printf( "appearance-path-gpu-%d-%d-%d", PathIndex, MaterialIndex, BlendIndex ) );
			if ( !Playback.IsValid() ) { C.Check( false, "Appearance pathのGPU Playbackを作れる" ); continue; }
			if ( PathIndex == 1 )
			{
				Playback->SetTransform( FTransform( FVector3( -1.0f, 0.0f, 0.0f ) ) ); Playback->Update( 0.25f );
				Playback->SetTransform( FTransform( FVector3( 0.0f, 0.5f, 0.0f ) ) ); Playback->Update( 0.25f );
				Playback->SetTransform( FTransform( FVector3( 1.0f, 0.0f, 0.0f ) ) ); Playback->Update( 0.25f );
			}
			else { Playback->SetTransform( FTransform( FVector3( 0.0f, -1.0f, 0.0f ) ) ); Playback->Update( 0.01f ); Playback->Update( 0.5f ); }
			PathPixels[MaterialIndex] = DrawAppearancePlayback( Playback, Executor, Screen, Soft, 24, BlendIndex != 0 );
			C.Check( PathPixels[MaterialIndex].bRendered && PathPixels[MaterialIndex].Width > 2 && PathPixels[MaterialIndex].Height > 2, "Appearance付きRibbon/Lightningの実PlaybackはGPU画素範囲を持つ" );
			Playback.Reset();
		}
		// 同じ曲線経路でAppearance倍率だけを外した幅の基準を作る。
		FParticleEmitterDefinition PlainAppearanceEmitter = PathEmitter;
		PlainAppearanceEmitter.Appearance = FEffectAppearanceDefinition();
		TSharedPtr<IEffectPlayback> PlainAppearancePlayback = NEffect::CreateParticlePlayback( MakeEffect( { PlainAppearanceEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, FString::Printf( "appearance-path-baseline-%d-%d", PathIndex, BlendIndex ) );
		FAppearancePixelResult PlainAppearancePixels{};
		if ( PlainAppearancePlayback.IsValid() )
		{
			if ( PathIndex == 1 )
			{
				PlainAppearancePlayback->SetTransform( FTransform( FVector3( -1.0f, 0.0f, 0.0f ) ) ); PlainAppearancePlayback->Update( 0.25f );
				PlainAppearancePlayback->SetTransform( FTransform( FVector3( 0.0f, 0.5f, 0.0f ) ) ); PlainAppearancePlayback->Update( 0.25f );
				PlainAppearancePlayback->SetTransform( FTransform( FVector3( 1.0f, 0.0f, 0.0f ) ) ); PlainAppearancePlayback->Update( 0.25f );
			}
			else { PlainAppearancePlayback->SetTransform( FTransform( FVector3( 0.0f, -1.0f, 0.0f ) ) ); PlainAppearancePlayback->Update( 0.01f ); PlainAppearancePlayback->Update( 0.5f ); }
			PlainAppearancePixels = DrawAppearancePlayback( PlainAppearancePlayback, Executor, Screen, Soft, 24, BlendIndex != 0 );
			PlainAppearancePlayback.Reset();
		}
		C.Check( PlainAppearancePixels.bRendered && ( PathPixels[1].Width > PlainAppearancePixels.Width + 2 || PathPixels[1].Height > PlainAppearancePixels.Height + 2 ), "Appearanceの非線形サイズ倍率が同じRibbon/Lightningの実画素幅を変える (curve %d x %d, base %d x %d)", PathPixels[1].Width, PathPixels[1].Height, PlainAppearancePixels.Width, PlainAppearancePixels.Height );
		C.Check( SimilarAppearancePixels( PathPixels[0], PathPixels[1] ), "材質ON/OFFのRibbon/Lightningで色と画素範囲を維持する" );
		if ( BlendIndex == 0 && PathIndex == 1 ) C.Check( std::abs( static_cast<int32>( PathPixels[1].Middle.r ) - 97 ) <= 14 && std::abs( static_cast<int32>( PathPixels[1].Middle.g ) - 52 ) <= 14 && std::abs( static_cast<int32>( PathPixels[1].Middle.b ) - 141 ) <= 18, "曲がったRibbon中央は背景24へ合成される (got %u,%u,%u)", PathPixels[1].Middle.r, PathPixels[1].Middle.g, PathPixels[1].Middle.b );
		if ( BlendIndex == 0 && PathIndex == 2 ) C.Check( std::abs( static_cast<int32>( PathPixels[1].Center.r ) - 76 ) <= 14 && std::abs( static_cast<int32>( PathPixels[1].Center.g ) - 44 ) <= 14 && std::abs( static_cast<int32>( PathPixels[1].Center.b ) - 108 ) <= 18, "Lightning中央は背景24へ合成される (got %u,%u,%u)", PathPixels[1].Center.r, PathPixels[1].Center.g, PathPixels[1].Center.b );
	}
	// test所有screenを削除する前に呼出側の描画先へ戻す。
	RenderVertex();
	SetDrawScreen( CallerState.Screen );
	DeleteGraph( Screen );
	DeleteSoftImage( Soft );
}
