// SPDX-License-Identifier: NOASSERTION
#include "WorldSelfTestSupport.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "WorldEffectSelfTestSupport.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Effect/ParticleEffectBackend.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/FileSystem.h"

namespace
{
	// 実機試験で変える画面・カメラ・変換・合成設定を終了時に戻す。
	struct FTreeLightningCallerState
	{
		// 試験前の描画先とscreen切替時の設定初期化。
		int32 Screen = GetDrawScreen();
		int32 ScreenReset = GetUseSetDrawScreenSettingReset();
		// 呼出元が使用していたcamera行列と変換行列。
		MATRIX CameraView = GetCameraViewMatrix();
		MATRIX CameraProjection = GetCameraProjectionMatrix();
		MATRIX World{};
		MATRIX View{};
		MATRIX Projection{};
		MATRIX Viewport{};
		// 呼出元の視界範囲、中心、depth距離とdot aspect。
		RECT DrawArea{};
		float32 Near = GetCameraNear();
		float32 Far = GetCameraFar();
		float32 DotAspect = GetCameraDotAspect();
		float32 Center[2] = {};
		// 描画blend、bright、背景の呼出元設定。
		int32 Blend[2] = {};
		int32 Bright[3] = {};
		int32 Background[4] = {};

		// DxLib状態を読んでからテストscreenの自動camera更新を止める。
		FTreeLightningCallerState()
		{
			GetTransformToWorldMatrix( &World ); GetTransformToViewMatrix( &View ); GetTransformToProjectionMatrix( &Projection ); GetTransformToViewportMatrix( &Viewport );
			GetDrawArea( &DrawArea ); GetCameraScreenCenter( &Center[0], &Center[1] ); GetDrawBlendMode( &Blend[0], &Blend[1] ); GetDrawBright( &Bright[0], &Bright[1], &Bright[2] ); GetBackgroundColor( &Background[0], &Background[1], &Background[2], &Background[3] );
			SetUseSetDrawScreenSettingReset( FALSE );
		}
		// screenとcamera、全matrix、blendと背景を元へ戻す。
		~FTreeLightningCallerState()
		{
			RenderVertex(); SetDrawScreen( Screen ); SetCameraNearFar( Near, Far ); SetCameraDotAspect( DotAspect ); SetCameraScreenCenter( Center[0], Center[1] ); SetCameraViewMatrix( CameraView ); SetupCamera_ProjectionMatrix( CameraProjection );
			SetTransformToWorld( &World ); SetTransformToView( &View ); SetTransformToProjection( &Projection ); SetTransformToViewport( &Viewport ); SetDrawArea( DrawArea.left, DrawArea.top, DrawArea.right, DrawArea.bottom ); SetDrawBlendMode( Blend[0], Blend[1] ); SetDrawBright( Bright[0], Bright[1], Bright[2] ); SetBackgroundColor( Background[0], Background[1], Background[2], Background[3] ); SetUseSetDrawScreenSettingReset( ScreenReset );
		}
	};

	// TreeのGPU比較で使う、成長とpulseが既知になる一回発生設定。
	FParticleEmitterDefinition MakeTreeRealEmitter( bool bMaterial )
	{
		// 乱数motionをなくし、材質有無だけを比較するTree emitter。
		FParticleEmitterDefinition Emitter = NEffectTest::QuietEmitter();
		Emitter.Path.Kind = FEffectPathDefinition::EKind::TreeLightning;
		Emitter.Path.End = FVector3( 0.0f, 3.0f, 0.0f );
		Emitter.Path.Segments = 12;
		Emitter.Path.Branches = 2;
		Emitter.Path.Jitter = 0.18f;
		Emitter.Path.BranchLength = 0.55f;
		Emitter.Path.Tree.Depth = 2;
		Emitter.Path.Tree.ChildBranches = 2;
		Emitter.Path.Tree.GrowthSeconds = 0.4f;
		Emitter.Path.Tree.BranchDelaySeconds = 0.02f;
		Emitter.Path.Tree.StrokeCount = 1;
		Emitter.Path.Tree.StrokeInterval = 2.0f;
		Emitter.Path.Tree.StrokeSeconds = 1.5f;
		Emitter.StartSize = Emitter.EndSize = { 0.18f, 0.18f };
		Emitter.StartColor = Emitter.EndColor = FColor32( 210, 235, 255, 255 );
		Emitter.Lifetime = { 4.0f, 4.0f };
		Emitter.Bursts.Add( NEffectTest::Burst( 0.0f, 1 ) );
		Emitter.Material.bEnabled = bMaterial;
		Emitter.Material.bDissolve = false;
		return Emitter;
	}

	// 一回の実描画で得る可視画素数と命令数。
	struct FTreeLightningFrameResult
	{
		// 黒背景から変化したreadback画素の数。
		int32 LitPixels = 0;
		// 非背景画素の境界。材質ON/OFFで同じ帯幅を比較する。
		int32 MinX = 256;
		int32 MinY = 256;
		int32 MaxX = -1;
		int32 MaxY = -1;
		// 材質Ribbon命令の記録数。
		int32 MaterialCommands = 0;
		// 従来Ribbon命令の記録数。
		int32 PlainCommands = 0;
		// 後の成長frameと位置固定を比べる全readback画素。
		TArray<FColor32> Pixels;
	};

	// 指定Age、camera、material modeとseedで本物のPlaybackを実行してreadbackする。
	FTreeLightningFrameResult RenderTreeFrame( CAssetManager& Assets, IDrawExecutor& Executor, int32 Screen, int32 Soft, const FString& ProofDirectory, const char* ProofName, float32 Age, bool bMaterial, bool bOblique, uint32 Seed )
	{
		// Tree形状と材質ON/OFFを切替えて新規particleを再生する定義。
		FParticleEmitterDefinition Emitter = MakeTreeRealEmitter( bMaterial );
		// この一回だけが所有するEffect定義。
		const TSharedPtr<FParticleEffectDefinition> Definition = NEffectTest::MakeEffect( { Emitter }, 0.0f, true, 1337 );
		// 呼出元が指定したseedで再生する窓口。
		FEffectPlayParams Params;
		Params.Seed = Seed;
		const TSharedPtr<IEffectPlayback> Playback = NEffect::CreateParticlePlayback( Definition, &Assets, Params, FTransform::Identity, "tree-lightning-gpu-proof" );
		if ( !Playback.IsValid() ) return {};
		Playback->Update( 0.01f );
		if ( Age > 0.0f ) Playback->Update( Age );
		// 実際のsimulationからplaybackが記録した所有命令buffer。
		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		Playback->Record( Draw );
		// Ribbon命令数を実行前に数え、空の描画を成功扱いしない。
		FTreeLightningFrameResult Result;
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::Ribbon3D ) ++Result.PlainCommands; if ( Command.Type == EDrawCommand::MaterialRibbon3D ) ++Result.MaterialCommands; } );
		SetDrawScreen( Screen ); SetDrawAreaFull(); SetBackgroundColor( 0, 0, 0, 255 ); ClearDrawScreen();
		SetCameraNearFar( 0.1f, 100.0f ); SetCameraDotAspect( 1.0f ); SetCameraScreenCenter( 128.0f, 128.0f ); SetupCamera_Ortho( 8.0f );
		if ( bOblique ) SetCameraPositionAndTarget_UpVecY( VGet( 2.8f, 0.9f, -9.0f ), VGet( 0.0f, 1.5f, 0.0f ) );
		else SetCameraPositionAndTarget_UpVecY( VGet( 0.0f, 1.5f, -10.0f ), VGet( 0.0f, 1.5f, 0.0f ) );
		// logical viewportとworldを固定し、World座標のRibbonへ呼出元matrixが重ならないようにする。
		MATRIX Viewport{};
		CreateViewportMatrix( &Viewport, 128.0f, 128.0f, 256.0f, 256.0f );
		SetTransformToViewport( &Viewport );
		const MATRIX Identity = MGetIdent();
		SetTransformToWorld( &Identity );
		SetDrawBright( 255, 255, 255 );
		if ( !Buffer.Words.empty() ) Executor.Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
		RenderVertex(); GetDrawScreenSoftImage( 0, 0, 256, 256, Soft );
		// readback画像を走査し、黒い背景から変化した実画素を数える。
		Result.Pixels.SetNum( 256 * 256 );
		for ( int32 Y = 0; Y < 256; ++Y ) for ( int32 X = 0; X < 256; ++X )
		{
			// readbackした一画素のRGBA値。
			int32 Channels[4] = {};
			GetPixelSoftImage( Soft, X, Y, &Channels[0], &Channels[1], &Channels[2], &Channels[3] );
			Result.Pixels[Y * 256 + X] = FColor32( static_cast<uint8>( Channels[0] ), static_cast<uint8>( Channels[1] ), static_cast<uint8>( Channels[2] ), static_cast<uint8>( Channels[3] ) );
			if ( Channels[0] > 5 || Channels[1] > 5 || Channels[2] > 5 ) { ++Result.LitPixels; Result.MinX = NMath::Min( Result.MinX, X ); Result.MinY = NMath::Min( Result.MinY, Y ); Result.MaxX = NMath::Max( Result.MaxX, X ); Result.MaxY = NMath::Max( Result.MaxY, Y ); }
		}
		if ( ProofName[0] != '\0' && NFileSystem::EnsureDirectory( ProofDirectory ) ) SaveDrawScreenToPNG( 0, 0, 256, 256, *( ProofDirectory / FString::FromUtf8( ProofName ) ) );
		return Result;
	}

	// 二枚のreadbackで明るい画素の占有状態が異なるか調べる。
	bool HaveDifferentTreePixelOccupancy( const TArray<FColor32>& First, const TArray<FColor32>& Second )
	{
		if ( First.Num() != 256 * 256 || Second.Num() != 256 * 256 ) return false;
		for ( int32 i = 0; i < First.Num(); ++i )
		{
			// 一枚目の画素が発光しているかを示す。
			const bool bFirstLit = First[i].r > 5 || First[i].g > 5 || First[i].b > 5;
			// 二枚目の画素が発光しているかを示す。
			const bool bSecondLit = Second[i].r > 5 || Second[i].g > 5 || Second[i].b > 5;
			if ( bFirstLit != bSecondLit ) return true;
		}
		return false;
	}

	// 画像中心の主幹周辺で明るいpixel数を数え、成長中の同位置を比べる。
	int32 CountTreeTrunkPixels( const TArray<FColor32>& Pixels )
	{
		if ( Pixels.Num() < 256 * 256 ) return 0;
		int32 Count = 0;
		for ( int32 Y = 168; Y <= 178; ++Y ) for ( int32 X = 118; X <= 138; ++X )
		{
			// root付近の一画素。
			const FColor32& Pixel = Pixels[Y * 256 + X];
			if ( Pixel.r > 5 || Pixel.g > 5 || Pixel.b > 5 ) ++Count;
		}
		return Count;
	}

	// 主幹中心から離れた、左右に広がる枝側pixel数を数える。
	int32 CountTreeSidePixels( const TArray<FColor32>& Pixels )
	{
		if ( Pixels.Num() < 256 * 256 ) return 0;
		int32 Count = 0;
		for ( int32 Y = 0; Y < 256; ++Y ) for ( int32 X = 0; X < 256; ++X ) if ( NMath::Abs( X - 128 ) >= 10 )
		{
			// 枝側の一画素。
			const FColor32& Pixel = Pixels[Y * 256 + X];
			if ( Pixel.r > 5 || Pixel.g > 5 || Pixel.b > 5 ) ++Count;
		}
		return Count;
	}

	// 同じ主幹周辺の同一pixelが成長前後に残る数を数える。
	int32 CountStableTreeTrunkPixels( const TArray<FColor32>& Before, const TArray<FColor32>& After )
	{
		if ( Before.Num() < 256 * 256 || After.Num() < 256 * 256 ) return 0;
		int32 Count = 0;
		for ( int32 Y = 168; Y <= 178; ++Y ) for ( int32 X = 118; X <= 138; ++X )
		{
			// 成長前の主幹pixel。
			const FColor32& First = Before[Y * 256 + X];
			// 枝が伸びた後の同座標pixel。
			const FColor32& Second = After[Y * 256 + X];
			if ( ( First.r > 5 || First.g > 5 || First.b > 5 ) && ( Second.r > 5 || Second.g > 5 || Second.b > 5 ) ) ++Count;
		}
		return Count;
	}
}

// 実simulation→Playback→DxLib executorから成長、完成、放電間隙と視点差を確認する。
void NWorldTest::RunEffectTreeLightningRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Dir )
{
	C.Group = "effect-tree-lightning-real";
	C.Check( Executor.SupportsEffectMaterialRibbons(), "通常のDxLib D3D11実行先は材質Tree帯を扱える" );
	if ( !Executor.SupportsEffectMaterialRibbons() ) return;
	const FTreeLightningCallerState Saved;
	const int32 Screen = MakeScreen( 256, 256, FALSE );
	const int32 Soft = MakeARGB8ColorSoftImage( 256, 256 );
	C.Check( Screen >= 0 && Soft >= 0 && SetUseGraphZBuffer( Screen, TRUE ) == 0, "Tree実機確認用screenとdepth bufferを作る" );
	if ( Screen < 0 || Soft < 0 )
	{
		if ( Screen >= 0 ) DeleteGraph( Screen );
		if ( Soft >= 0 ) DeleteSoftImage( Soft );
		return;
	}
	Assets.SetRootDirectory( Dir );
	// 全proof画像をworld selftest用sandbox配下へ限定する。
	const FString ProofDirectory = FString( "Saved" ) / "EffectTreeLightningProof";
	const FTreeLightningFrameResult Early = RenderTreeFrame( Assets, Executor, Screen, Soft, ProofDirectory, "tree-early-front.png", 0.04f, true, false, 31337u );
	const FTreeLightningFrameResult Mid = RenderTreeFrame( Assets, Executor, Screen, Soft, ProofDirectory, "tree-mid-front.png", 0.2f, true, false, 31337u );
	const FTreeLightningFrameResult Branches = RenderTreeFrame( Assets, Executor, Screen, Soft, ProofDirectory, "tree-branches-front.png", 0.42f, true, false, 31337u );
	const FTreeLightningFrameResult Full = RenderTreeFrame( Assets, Executor, Screen, Soft, ProofDirectory, "tree-full-front.png", 1.0f, true, false, 31337u );
	const FTreeLightningFrameResult NextStrike = RenderTreeFrame( Assets, Executor, Screen, Soft, ProofDirectory, "tree-next-strike-front.png", 1.0f, true, false, 31338u );
	const FTreeLightningFrameResult Gap = RenderTreeFrame( Assets, Executor, Screen, Soft, ProofDirectory, "tree-pulse-gap-front.png", 2.2f, true, false, 31337u );
	const FTreeLightningFrameResult Oblique = RenderTreeFrame( Assets, Executor, Screen, Soft, ProofDirectory, "tree-full-oblique.png", 1.0f, true, true, 31337u );
	const FTreeLightningFrameResult Plain = RenderTreeFrame( Assets, Executor, Screen, Soft, ProofDirectory, "tree-full-no-material.png", 1.0f, false, false, 31337u );
	C.Check( Early.LitPixels > 0 && Mid.LitPixels > Early.LitPixels && Branches.LitPixels > Mid.LitPixels && Full.LitPixels > 0, "実particleの成長が主幹と枝を伸ばし、完成後のGPU画素も残る (early=%d mid=%d branches=%d full=%d)", Early.LitPixels, Mid.LitPixels, Branches.LitPixels, Full.LitPixels );
	// 完成frameの画素配列が全画面分そろっているかを示す。
	const bool bFullPixelsValid = Full.Pixels.Num() == 256 * 256;
	// 次Strikeの画素配列が全画面分そろっているかを示す。
	const bool bNextStrikePixelsValid = NextStrike.Pixels.Num() == 256 * 256;
	C.Check( bFullPixelsValid && bNextStrikePixelsValid, "完成frameと次StrikeのGPU readbackが256x256画素を持つ (full=%d next=%d)", Full.Pixels.Num(), NextStrike.Pixels.Num() );
	C.Check( Full.LitPixels > 0 && NextStrike.LitPixels > 0, "完成frameと別seedの次Strikeに実GPU画素が残る (full=%d next=%d)", Full.LitPixels, NextStrike.LitPixels );
	C.Check( bFullPixelsValid && bNextStrikePixelsValid && HaveDifferentTreePixelOccupancy( Full.Pixels, NextStrike.Pixels ), "別seedの次Strikeは完成frameと画素の占有パターンが異なる" );
	// 始点付近の同じ主幹画素が成長中の前後でも変わらず残るか調べる。
	const int32 EarlyTrunkPixels = CountTreeTrunkPixels( Early.Pixels );
	const int32 BranchTrunkPixels = CountTreeTrunkPixels( Branches.Pixels );
	const int32 StableTrunkPixels = CountStableTreeTrunkPixels( Early.Pixels, Branches.Pixels );
	const int32 MidSidePixels = CountTreeSidePixels( Mid.Pixels );
	const int32 BranchSidePixels = CountTreeSidePixels( Branches.Pixels );
	C.Check( EarlyTrunkPixels > 0 && BranchTrunkPixels > 0 && StableTrunkPixels > 0, "枝が伸びても早期frameの主幹は同じroot周辺pixelに残る (early=%d branch=%d overlap=%d)", EarlyTrunkPixels, BranchTrunkPixels, StableTrunkPixels );
	C.Check( BranchSidePixels > MidSidePixels, "固定Ageの中間frameで主幹外へ一次枝・孫枝のpixelが増える (mid=%d branch=%d)", MidSidePixels, BranchSidePixels );
	C.Check( Gap.LitPixels == 0, "最後の短いpulse窓後は材質Treeを画素へ描かない (lit=%d)", Gap.LitPixels );
	const bool bBoundsMatch = NMath::Abs( Full.MinX - Plain.MinX ) <= 2 && NMath::Abs( Full.MinY - Plain.MinY ) <= 2 && NMath::Abs( Full.MaxX - Plain.MaxX ) <= 2 && NMath::Abs( Full.MaxY - Plain.MaxY ) <= 2;
	C.Check( Full.MaterialCommands > 0 && Oblique.MaterialCommands > 0 && Plain.PlainCommands > 0 && Oblique.LitPixels > 0 && Plain.LitPixels > 0 && bBoundsMatch, "完成Treeは材質ON/OFFで同じ占有幅になり正面・斜めの両視点で実画像を持つ (material=%d legacy=%d bounds=%d oblique=%d plain=%d)", Full.MaterialCommands, Plain.PlainCommands, bBoundsMatch, Oblique.LitPixels, Plain.LitPixels );
	SetDrawScreen( Saved.Screen );
	DeleteGraph( Screen );
	DeleteSoftImage( Soft );
}
