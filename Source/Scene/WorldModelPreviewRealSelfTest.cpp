// SPDX-License-Identifier: NOASSERTION

#include "WorldModelPreviewSelfTest.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "AssetSelfTestModelSurface.h"
#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/WorldEditor.h"
#include "Library/DebugUI/WorldEditorInternal.h"
#include "Library/Framework/Animation/ModelAnimator.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/EditorAssetInfo.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Asset/Model.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/World/WorldDefinition.h"
#include "Library/Framework/World/WorldModelMaterialEffect.h"
#include "Library/Platform/DxLib/DxLibAssetBackend.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/FileSystem.h"

namespace NWorldTest
{
	namespace
	{
		// 材質3の不透明度0設定を作る。
		FWorldModelMaterialEffect MakeRealPreviewBinding()
		{
			FWorldModelMaterialEffect Binding; // 対象fixtureの材質効果設定
			Binding.MaterialIndex = 3;
			Binding.Effect.Material.bEnabled = true;
			Binding.Effect.Duration = 2.0f;
			Binding.Effect.bLoop = true;
			Binding.Effect.Opacity = 0.0f;
			Binding.Effect.Blend = EDrawBlend::Alpha;
			Binding.Effect.Material.Emission.Keys.Add( FEffectScalarCurve::FKey{ 0.0f, 1.0f } );
			Binding.Effect.Material.Emission.Keys.Add( FEffectScalarCurve::FKey{ 1.0f, 1.0f } );
			return Binding;
		}

		// 指定IDとmodel pathのworld objectを作る。
		FWorldObject MakeRealPreviewObject( int32 Id, const FString& Path, const FWorldModelMaterialEffect* Binding, const FVector3& Position )
		{
			FWorldObject Object; // 配置するworld object
			Object.Id = Id;
			Object.Name = "preview-model";
			Object.Transform.Position = Position;
			FWorldModel Model; // objectへ保存するmodel設定
			Model.Path = Path;
			if ( Binding != nullptr ) Model.MaterialEffect = *Binding;
			Object.Model = Model;
			return Object;
		}

		// 記録buffer内の指定command数を数える。
		int32 CountRealPreviewCommands( const FDrawBuffer& Buffer, EDrawCommand Type )
		{
			int32 Count = 0; // 指定commandの出現数
			if ( !Buffer.Words.empty() ) ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == Type ) ++Count; } );
			return Count;
		}

		// 同じ長さの画素配列から差分画素数を数える。
		int32 CountRealPreviewDifferences( const TArray<FColor32>& Left, const TArray<FColor32>& Right, int32 X0, int32 Y0, int32 X1, int32 Y1 )
		{
			if ( Left.Num() != 128 * 128 || Right.Num() != 128 * 128 ) return -1;
			int32 Differences = 0; // 不一致画素数
			// 矩形内の座標だけを比較する。
			for ( int32 Y = Y0; Y < Y1; ++Y ) for ( int32 X = X0; X < X1; ++X ) if ( std::memcmp( &Left[Y * 128 + X], &Right[Y * 128 + X], sizeof( FColor32 ) ) != 0 ) ++Differences;
			return Differences;
		}

		// 指定領域で背景色から変化した画素数を数える。
		int32 CountRealPreviewVisiblePixels( const TArray<FColor32>& Pixels, int32 X0, int32 Y0, int32 X1, int32 Y1 )
		{
			if ( Pixels.Num() != 128 * 128 ) return -1;
			int32 Visible = 0; // 背景色から変わった画素数
			// 背景と異なる位置を可視画素として数える。
			for ( int32 Y = Y0; Y < Y1; ++Y ) for ( int32 X = X0; X < X1; ++X )
			{
				const FColor32& Pixel = Pixels[Y * 128 + X]; // 比較対象の画素
				if ( Pixel.r != 17 || Pixel.g != 23 || Pixel.b != 31 ) ++Visible;
			}
			return Visible;
		}

		// fixture triangleの投影内側へ余白を置いたpixel ROI。
		struct FRealPreviewTriangleROI
		{
			int32 Left = 0; // 左端を含むpixel座標
			int32 Top = 0; // 上端を含むpixel座標
			int32 Right = 0; // 右端を含まないpixel座標
			int32 Bottom = 0; // 下端を含まないpixel座標
		};

		// fixtureのframe translationとlocal三角形をDxLib cameraで投影し、辺から離れたROIを返す。
		bool ProjectRealPreviewFixtureROI( float32 ObjectX, bool bChildFrame, FRealPreviewTriangleROI& Out )
		{
			SetCameraNearFar( 0.1f, 100.0f );
			SetCameraDotAspect( 1.0f );
			SetCameraScreenCenter( 64.0f, 64.0f );
			SetupCamera_Ortho( 16.0f );
			SetCameraPositionAndTargetAndUpVec( VGet( 0.0f, 1.0f, 10.0f ), VGet( 0.0f, 1.0f, 0.0f ), VGet( 0.0f, 1.0f, 0.0f ) );
			const float32 FrameX = bChildFrame ? 4.0f : 0.0f; // child frameの既知translation
			const float32 LocalX[3] = { 0.0f, bChildFrame ? 0.0f : 2.0f, bChildFrame ? -2.0f : 0.0f }; // fixture triangleのlocal X
			const float32 LocalY[3] = { 0.0f, bChildFrame ? 2.0f : 0.0f, bChildFrame ? 0.0f : 2.0f }; // fixture triangleのlocal Y
			float32 MinX = 0.0f; // 投影三角形の左端候補
			float32 MaxX = 0.0f; // 投影三角形の右端候補
			float32 MinY = 0.0f; // 投影三角形の上端候補
			float32 MaxY = 0.0f; // 投影三角形の下端候補
			for ( int32 Corner = 0; Corner < 3; ++Corner )
			{
				const VECTOR Screen = ConvWorldPosToScreenPos( VGet( ObjectX + FrameX + LocalX[Corner], LocalY[Corner], 0.0f ) ); // native投影座標
				if ( !NMath::IsFinite( Screen.x ) || !NMath::IsFinite( Screen.y ) ) return false;
				if ( Corner == 0 )
				{
					MinX = Screen.x;
					MaxX = Screen.x;
					MinY = Screen.y;
					MaxY = Screen.y;
				}
				else
				{
					MinX = NMath::Min( MinX, Screen.x );
					MaxX = NMath::Max( MaxX, Screen.x );
					MinY = NMath::Min( MinY, Screen.y );
					MaxY = NMath::Max( MaxY, Screen.y );
				}
			}
			Out.Left = static_cast<int32>( std::ceil( MinX + 2.0f ) );
			Out.Top = static_cast<int32>( std::ceil( MinY + 2.0f ) );
			Out.Right = static_cast<int32>( std::floor( MaxX - 2.0f ) );
			Out.Bottom = static_cast<int32>( std::floor( MaxY - 2.0f ) );
			return Out.Left >= 0 && Out.Top >= 0 && Out.Right <= 128 && Out.Bottom <= 128 && Out.Left < Out.Right && Out.Top < Out.Bottom;
		}

		// 投影ROI同士に重なりがあるか調べる。
		bool RealPreviewROIsOverlap( const FRealPreviewTriangleROI& Left, const FRealPreviewTriangleROI& Right )
		{
			return Left.Left < Right.Right && Right.Left < Left.Right && Left.Top < Right.Bottom && Right.Top < Left.Bottom;
		}

		// 変更するDxLib描画状態を保存し、終了時に元へ戻す。
		struct FRealPreviewDrawState
		{
			int32 Screen = GetDrawScreen(); // 元の描画先
			int32 ScreenReset = GetUseSetDrawScreenSettingReset(); // 画面切替時の自動reset設定
			int32 Culling = GetUseBackCulling(); // 元の裏面除去設定
			float32 Near = GetCameraNear(); // 元cameraのnear値
			float32 Far = GetCameraFar(); // 元cameraのfar値
			float32 Aspect = GetCameraDotAspect(); // 元cameraの縦横比
			float32 Center[2] = {}; // 元cameraの画面中心
			MATRIX CameraView{}; // 元camera view行列
			MATRIX CameraProjection{}; // 元camera projection行列
			MATRIX World{}; // 元world行列
			MATRIX View{}; // 元view行列
			MATRIX Projection{}; // 元projection行列
			MATRIX Viewport{}; // 元viewport行列
			RECT Area{}; // 元の描画範囲
			int32 Blend[2] = {}; // 元blend modeとparameter
			int32 Bright[3] = {}; // 元RGB明るさ
			int32 Background[4] = {}; // 元背景RGBA
			bool bRestored = false; // 復元済みか

			FRealPreviewDrawState()
			{
				GetCameraScreenCenter( &Center[0], &Center[1] );
				CameraView = GetCameraViewMatrix();
				CameraProjection = GetCameraProjectionMatrix();
				GetTransformToWorldMatrix( &World );
				GetTransformToViewMatrix( &View );
				GetTransformToProjectionMatrix( &Projection );
				GetTransformToViewportMatrix( &Viewport );
				GetDrawArea( &Area );
				GetDrawBlendMode( &Blend[0], &Blend[1] );
				GetDrawBright( &Bright[0], &Bright[1], &Bright[2] );
				GetBackgroundColor( &Background[0], &Background[1], &Background[2], &Background[3] );
				SetUseSetDrawScreenSettingReset( FALSE );
			}

			~FRealPreviewDrawState() { Restore(); }

			// 保存した画面・camera・行列・blend設定を復元する。
			void Restore()
			{
				if ( bRestored ) return;
				RenderVertex();
				SetDrawScreen( Screen );
				SetCameraNearFar( Near, Far );
				SetCameraDotAspect( Aspect );
				SetCameraScreenCenter( Center[0], Center[1] );
				SetCameraViewMatrix( CameraView );
				SetupCamera_ProjectionMatrix( CameraProjection );
				SetTransformToWorld( &World );
				SetTransformToView( &View );
				SetTransformToProjection( &Projection );
				SetTransformToViewport( &Viewport );
				SetDrawArea( Area.left, Area.top, Area.right, Area.bottom );
				SetDrawBlendMode( Blend[0], Blend[1] );
				SetDrawBright( Bright[0], Bright[1], Bright[2] );
				SetBackgroundColor( Background[0], Background[1], Background[2], Background[3] );
				SetUseBackCulling( Culling );
				SetUseSetDrawScreenSettingReset( ScreenReset );
				bRestored = true;
			}
		};

		// 本番と同じworld record・executor経路で画素を取得する。
		bool CaptureRealPreview( FWorldEditorState& State, IDrawExecutor& Executor, int32 Screen, int32 Soft, TArray<FColor32>& Pixels )
		{
			if ( Screen < 0 || Soft < 0 ) return false;
			SetDrawScreen( Screen );
			SetDrawAreaFull();
			SetBackgroundColor( 17, 23, 31, 255 );
			ClearDrawScreen();
			SetDrawBlendMode( DX_BLENDMODE_NOBLEND, 0 );
			SetDrawBright( 255, 255, 255 );
			SetUseBackCulling( DX_CULLING_NONE );
			SetCameraNearFar( 0.1f, 100.0f );
			SetCameraDotAspect( 1.0f );
			SetCameraScreenCenter( 64.0f, 64.0f );
			SetupCamera_Ortho( 16.0f );
			SetCameraPositionAndTargetAndUpVec( VGet( 0.0f, 1.0f, 10.0f ), VGet( 0.0f, 1.0f, 0.0f ), VGet( 0.0f, 1.0f, 0.0f ) );
			FDrawBuffer Buffer; // viewportが作る命令列
			FDrawContext Draw( Buffer ); // 本番scene記録context
			NWorldEditorUI::RecordWorldScene( State, Draw );
			if ( CountRealPreviewCommands( Buffer, EDrawCommand::Model3D ) + CountRealPreviewCommands( Buffer, EDrawCommand::ModelMaterial3D ) != 2 ) return false;
			if ( !Buffer.Words.empty() ) Executor.Execute( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
			RenderVertex();
			if ( GetDrawScreenSoftImage( 0, 0, 128, 128, Soft ) != 0 ) return false;
			Pixels.Reset();
			Pixels.Reserve( 128 * 128 );
			// SoftImageから画面全体のRGBA値を取得する。
			for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X )
			{
				int32 Red = 0; // 読み取った赤
				int32 Green = 0; // 読み取った緑
				int32 Blue = 0; // 読み取った青
				int32 Alpha = 0; // 読み取ったalpha
				if ( GetPixelSoftImage( Soft, X, Y, &Red, &Green, &Blue, &Alpha ) != 0 ) return false;
				Pixels.Add( FColor32( static_cast<uint8>( Red ), static_cast<uint8>( Green ), static_cast<uint8>( Blue ), static_cast<uint8>( Alpha ) ) );
			}
			return Pixels.Num() == 128 * 128;
		}

		// native model命令を直接実行し、world記録経路のStop結果と比べる画素を読む。
		bool CaptureDirectNativePreview( CModel& Source, const FTransform& Transform, IDrawExecutor& Executor, int32 Screen, int32 Soft, TArray<FColor32>& Pixels )
		{
			if ( Screen < 0 || Soft < 0 ) return false;
			SetDrawScreen( Screen );
			SetDrawAreaFull();
			SetBackgroundColor( 17, 23, 31, 255 );
			ClearDrawScreen();
			SetDrawBlendMode( DX_BLENDMODE_NOBLEND, 0 );
			SetDrawBright( 255, 255, 255 );
			SetUseBackCulling( DX_CULLING_NONE );
			SetCameraNearFar( 0.1f, 100.0f );
			SetCameraDotAspect( 1.0f );
			SetCameraScreenCenter( 64.0f, 64.0f );
			SetupCamera_Ortho( 16.0f );
			SetCameraPositionAndTargetAndUpVec( VGet( 0.0f, 1.0f, 10.0f ), VGet( 0.0f, 1.0f, 0.0f ), VGet( 0.0f, 1.0f, 0.0f ) );
			FDrawBuffer Buffer; // direct native model命令列
			FDrawContext Draw( Buffer ); // native Model記録context
			Draw.Model( Source.GetHandle(), Transform.ToMatrix() );
			Executor.Execute( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
			RenderVertex();
			if ( GetDrawScreenSoftImage( 0, 0, 128, 128, Soft ) != 0 ) return false;
			Pixels.Reset();
			Pixels.Reserve( 128 * 128 );
			// native比較画像の全pixelを保存する。
			for ( int32 Y = 0; Y < 128; ++Y ) for ( int32 X = 0; X < 128; ++X )
			{
				int32 Red = 0; // 読み取った赤
				int32 Green = 0; // 読み取った緑
				int32 Blue = 0; // 読み取った青
				int32 Alpha = 0; // 読み取ったalpha
				if ( GetPixelSoftImage( Soft, X, Y, &Red, &Green, &Blue, &Alpha ) != 0 ) return false;
				Pixels.Add( FColor32( static_cast<uint8>( Red ), static_cast<uint8>( Green ), static_cast<uint8>( Blue ), static_cast<uint8>( Alpha ) ) );
			}
			return Pixels.Num() == 128 * 128;
		}
	}

	void RunWorldModelPreviewRealChecks( NAssetTest::FChecker& Checker, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Directory )
	{
		const char* OldGroup = Checker.Group; // 呼出元のchecker group
		Checker.Group = "world-model-preview-real";
		const FString OldRoot = Assets.GetRootDirectory(); // 呼出元asset root
		CAssetManager* AppAssets = GetAppSubsystem<CAssetManager>(); // world editorのapp asset manager
		Checker.Check( AppAssets == &Assets, "実preview検査が本番world editorと同じasset managerを使う" );
		if ( AppAssets != &Assets )
		{
			Checker.Group = OldGroup;
			return;
		}
		const int32 Screen = MakeScreen( 128, 128, FALSE ); // hidden render target
		const int32 Soft = MakeARGB8ColorSoftImage( 128, 128 ); // readback用soft image
		FRealPreviewDrawState DrawState; // 描画状態の復元guard
		CDxLibAssetBackend* Backend = dynamic_cast<CDxLibAssetBackend*>( Assets.GetBackend() ); // resource数を読むDxLib backend
		const int32 InitialGeometryCount = Backend != nullptr ? Backend->GetNumModelMaterialGeometries() : -1; // 試験開始時geometry数
		const FString FixtureDirectory = Directory / "world_model_preview_real"; // modelと画像のsandbox path
		const FString ProofDirectory = "Saved/WorldModelPreviewProof"; // runner作業directory内の画素証拠
		const bool bFixtureWritten = NFileSystem::EnsureDirectory( FixtureDirectory ) && NAssetTest::WriteStaticModelSurfaceFixture( FixtureDirectory, "world_model_preview.x" ); // native fixture作成結果
		Assets.SetRootDirectory( Directory );
		const FString ModelPath = "world_model_preview_real/world_model_preview.x"; // world内の相対model path
		const FWorldModelMaterialEffect Binding = MakeRealPreviewBinding(); // 材質3の効果設定
		FWorldDefinition Definition; // 保存しOpenするworld値
		Definition.Name = "Model preview real proof";
		Definition.NextId = 3;
		Definition.Objects.Add( MakeRealPreviewObject( 1, ModelPath, &Binding, FVector3( -4.0f, 0.0f, 0.0f ) ) );
		Definition.Objects.Add( MakeRealPreviewObject( 2, ModelPath, nullptr, FVector3( 2.0f, 0.0f, 0.0f ) ) );
		const bool bWorldSaved = bFixtureWritten && Assets.SaveWorld( "world_model_preview_real.world", Definition ); // fixture worldの保存成否
		FWorldEditorState State; // 実world editorと同じpreview owner
		State.bShowGrid = false;
		State.bShowNames = false;
		State.bShowGizmo = false;
		State.ColliderView = FWorldEditorState::EColliderView::Off;
		State.AssetInfo = MakeUnique<FAssetEditorInfo>( Assets );
		const bool bWorldOpened = bWorldSaved && NDebugUI::OpenWorldFile( State, "world_model_preview_real.world" ); // diskからの読み込み成否
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.25f, &Executor );
		FWorldEditorModelPreviewKey TargetKey; // effect対象のroot object key
		TargetKey.ObjectIds.Add( 1 );
		FWorldEditorModelPreviewKey SiblingKey; // 効果を持たないsibling key
		SiblingKey.ObjectIds.Add( 2 );
		FWorldEditorModelPreview* Target = State.ModelPreviews.Find( TargetKey ); // 効果個体preview
		FWorldEditorModelPreview* Sibling = State.ModelPreviews.Find( SiblingKey ); // native sibling preview
		const int32 GeometryCountPrepared = Backend != nullptr ? Backend->GetNumModelMaterialGeometries() : -1; // 準備後geometry数
		const bool bPrepared = bWorldOpened && Target != nullptr && Sibling != nullptr && Target->Source.IsValid() && Target->Animator.Instance.IsValid() && Target->Animator.GetMaterialEffect().IsPrepared() && Sibling->Animator.Instance.IsValid() && GeometryCountPrepared > InitialGeometryCount; // 実機準備の成立
		Checker.Check( bPrepared, "4材質fixtureの材質3を専用個体へ準備し、native siblingも専用instanceにする" );
		TArray<FColor32> ActivePixels; // 効果再生中のworld pixel
		TArray<FColor32> StoppedPixels; // Stop後のworld pixel
		TArray<FColor32> NativePixels; // bindingなしnative pixel
		bool bActiveCaptured = false; // active capture成功
		bool bStoppedCaptured = false; // stopped capture成功
		bool bNativeCaptured = false; // native baseline capture成功
		TWeakPtr<CModel> LifecycleSource; // preview解放を監視するsource
		TWeakPtr<CModelInstance> LifecycleInstance; // preview個体の寿命監視
		if ( bPrepared && Screen >= 0 && Soft >= 0 )
		{
			LifecycleSource = Target->Source;
			LifecycleInstance = Target->Animator.Instance;
			const float64 TimeBeforeFailedOpen = Target->Animator.GetMaterialEffect().GetEffectTime(); // 失敗open前のclock
			const bool bFailedOpenPreserved = !NDebugUI::OpenWorldFile( State, "world_model_preview_missing.world" ) && State.ModelPreviews.Num() == 2 && LifecycleInstance.IsValid() && LifecycleSource.IsValid() && Target->Animator.GetMaterialEffect().GetEffectTime() == TimeBeforeFailedOpen && Backend->GetNumModelMaterialGeometries() == GeometryCountPrepared; // 旧owner保持結果
			Checker.Check( bFailedOpenPreserved, "存在しないworldを開く失敗は専用instance・source・clock・geometryを保つ" );
			const float64 TimeBeforeRecord = Target->Animator.GetMaterialEffect().GetEffectTime(); // 反復record直前clock
			const int32 GeometryCountBeforeRecord = Backend->GetNumModelMaterialGeometries(); // 反復record直前resource数
			FDrawBuffer RepeatedRecord; // 同一bufferへ連続recordする結果
			FDrawContext RepeatedDraw( RepeatedRecord ); // 本番world scene record context
			NWorldEditorUI::RecordWorldScene( State, RepeatedDraw );
			NWorldEditorUI::RecordWorldScene( State, RepeatedDraw );
			const float64 TimeAfterRecord = Target->Animator.GetMaterialEffect().GetEffectTime(); // record後clock
			Checker.Check( TimeBeforeRecord == 0.25 && TimeAfterRecord == TimeBeforeRecord && Backend->GetNumModelMaterialGeometries() == GeometryCountBeforeRecord && CountRealPreviewCommands( RepeatedRecord, EDrawCommand::ModelMaterial3D ) == 2 && CountRealPreviewCommands( RepeatedRecord, EDrawCommand::Model3D ) == 2, "更新時計は既知の0.25秒で、繰返しRecordは対象・sibling命令とgeometry数を保つ" );
			bActiveCaptured = CaptureRealPreview( State, Executor, Screen, Soft, ActivePixels );
			const bool bActiveProofSaved = bActiveCaptured && NFileSystem::EnsureDirectory( ProofDirectory ) && SaveDrawScreenToPNG( 0, 0, 128, 128, *( ProofDirectory / "active.png" ) ) == 0; // active画像の保存結果
			Target->Animator.GetMaterialEffect().Stop();
			bStoppedCaptured = CaptureRealPreview( State, Executor, Screen, Soft, StoppedPixels );
			const bool bStoppedProofSaved = bStoppedCaptured && SaveDrawScreenToPNG( 0, 0, 128, 128, *( ProofDirectory / "stopped.png" ) ) == 0; // Stop画像の保存結果
			FWorldDefinition NativeDefinition = Definition; // bindingなしnative対照world
			FWorldObject* NativeObject = NativeDefinition.FindObject( 1 ); // 効果対象object
			if ( NativeObject != nullptr && NativeObject->Model.IsSet() ) NativeObject->Model.GetValue().MaterialEffect.Reset();
			State.Session.Reset( NativeDefinition, "world_model_preview_real.world" );
			NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.0f, &Executor );
			bNativeCaptured = CaptureRealPreview( State, Executor, Screen, Soft, NativePixels );
			const bool bNativeProofSaved = bNativeCaptured && SaveDrawScreenToPNG( 0, 0, 128, 128, *( ProofDirectory / "native.png" ) ) == 0; // native画像の保存結果
			FWorldEditorModelPreview* NativeTargetPreview = State.ModelPreviews.Find( TargetKey ); // bindingを外した対象preview
			TArray<FColor32> DirectNativePixels; // native Model命令だけの画素
			const bool bDirectNativeCaptured = NativeTargetPreview != nullptr && NativeTargetPreview->Source.IsValid() && CaptureDirectNativePreview( *NativeTargetPreview->Source, Definition.Objects[0].Transform.ToTransform(), Executor, Screen, Soft, DirectNativePixels ); // direct native capture結果
			const bool bDirectNativeProofSaved = bDirectNativeCaptured && SaveDrawScreenToPNG( 0, 0, 128, 128, *( ProofDirectory / "direct-native.png" ) ) == 0; // direct native画像の保存結果
			FRealPreviewTriangleROI TargetROI; // target child frameの独立投影ROI
			FRealPreviewTriangleROI TargetRootROI; // target root frameの重なり検証ROI
			FRealPreviewTriangleROI SiblingROI; // sibling child frameの独立投影ROI
			FRealPreviewTriangleROI SiblingRootROI; // sibling root frameの重なり検証ROI
			const bool bTargetROI = ProjectRealPreviewFixtureROI( -4.0f, true, TargetROI ) && ProjectRealPreviewFixtureROI( -4.0f, false, TargetRootROI ); // target fixture面のprojection
			const bool bSiblingROI = ProjectRealPreviewFixtureROI( 2.0f, true, SiblingROI ) && ProjectRealPreviewFixtureROI( 2.0f, false, SiblingRootROI ); // sibling fixture面のprojection
			const int32 ActiveSelectedVisible = bTargetROI ? CountRealPreviewVisiblePixels( ActivePixels, TargetROI.Left, TargetROI.Top, TargetROI.Right, TargetROI.Bottom ) : -1; // active時target child ROIの可視数
			const int32 NativeSelectedVisible = bTargetROI ? CountRealPreviewVisiblePixels( NativePixels, TargetROI.Left, TargetROI.Top, TargetROI.Right, TargetROI.Bottom ) : -1; // native時target child ROIの可視数
			const int32 NativeSiblingVisible = bSiblingROI ? CountRealPreviewVisiblePixels( NativePixels, SiblingROI.Left, SiblingROI.Top, SiblingROI.Right, SiblingROI.Bottom ) : -1; // native時sibling child ROIの可視数
			Checker.Check( bActiveCaptured && bStoppedCaptured && bNativeCaptured && NativeSelectedVisible > 0 && NativeSiblingVisible > 0, "native baselineは選択面とsiblingの両方に可視pixelを持つ" );
			Checker.Check( bTargetROI && bSiblingROI && !RealPreviewROIsOverlap( TargetROI, TargetRootROI ) && !RealPreviewROIsOverlap( SiblingROI, SiblingRootROI ), "projection ROIはそれぞれchild frameだけを含みroot triangleと重ならない" );
			Checker.Check( TargetROI.Left == 66 && TargetROI.Right == 78 && SiblingROI.Left == 18 && SiblingROI.Right == 30, "camera right=-Xとchild translation 4を投影した内側ROI (target=%d..%d sibling=%d..%d)", TargetROI.Left, TargetROI.Right, SiblingROI.Left, SiblingROI.Right );
			Checker.Check( bActiveProofSaved && bStoppedProofSaved && bNativeProofSaved && bDirectNativeProofSaved, "active・Stop・native・direct nativeのPNG証拠を保存する" );
			Checker.Check( bTargetROI && bSiblingROI && ActiveSelectedVisible == 0 && CountRealPreviewDifferences( ActivePixels, NativePixels, TargetROI.Left, TargetROI.Top, TargetROI.Right, TargetROI.Bottom ) > 0 && CountRealPreviewDifferences( ActivePixels, NativePixels, SiblingROI.Left, SiblingROI.Top, SiblingROI.Right, SiblingROI.Bottom ) == 0, "Opacity 0は投影したchild面だけを消す (ROI=%d,%d..%d,%d active=%d native=%d siblingDiff=%d)", TargetROI.Left, TargetROI.Top, TargetROI.Right, TargetROI.Bottom, ActiveSelectedVisible, NativeSelectedVisible, CountRealPreviewDifferences( ActivePixels, NativePixels, SiblingROI.Left, SiblingROI.Top, SiblingROI.Right, SiblingROI.Bottom ) );
			Checker.Check( CountRealPreviewDifferences( StoppedPixels, NativePixels, 0, 0, 128, 128 ) == 0 && bDirectNativeCaptured && CountRealPreviewDifferences( StoppedPixels, DirectNativePixels, TargetROI.Left, TargetROI.Top, TargetROI.Right, TargetROI.Bottom ) == 0, "Stop後のworld画素はbindingなしbaselineに一致し、投影したchild ROIは直接native Model描画にも一致する" );
		}
		else Checker.Check( false, "画素検査にはready model、prepared material effect、screen、soft imageが必要" );

		// 同じIDとpathの成功Openでも古い個体を捨て、新しい時計を作る。
		FWorldEditorModelPreview* BeforeReopen = State.ModelPreviews.Find( TargetKey ); // 成功Open前の専用view
		if ( BeforeReopen != nullptr )
		{
			LifecycleSource = BeforeReopen->Source;
			LifecycleInstance = BeforeReopen->Animator.Instance;
		}
		const bool bSameWorldOpened = NDebugUI::OpenWorldFile( State, "world_model_preview_real.world" ); // 同じID・pathを持つdisk worldの再Open結果
		Assets.ReleaseUnused();
		Assets.Tick( 0.0f );
		Checker.Check( bSameWorldOpened && State.ModelPreviews.IsEmpty() && !LifecycleInstance.IsValid() && !LifecycleSource.IsValid(), "同じworldの成功Openは旧個体・source・geometry ownerを解放する" );
		Checker.Check( Backend == nullptr || Backend->GetNumModelMaterialGeometries() == InitialGeometryCount, "成功Open後にこのpreviewが追加したgeometry resourceが残らない" );
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.0f, &Executor );
		FWorldEditorModelPreview* Reopened = State.ModelPreviews.Find( TargetKey ); // 新規Open後の専用view
		const bool bReprepared = bSameWorldOpened && Reopened != nullptr && Reopened->Animator.GetMaterialEffect().IsPrepared() && Reopened->Animator.GetMaterialEffect().GetEffectTime() == 0.0; // clock0で効果を再準備したか
		Checker.Check( bReprepared && !LifecycleInstance.IsValid() && ( Backend == nullptr || Backend->GetNumModelMaterialGeometries() > InitialGeometryCount ), "再Open後は新個体が時刻0から準備される" );
		TWeakPtr<CModelInstance> NewWorldInstance; // New後に解放されるinstanceの観測値
		TWeakPtr<CModel> CachedSource; // AssetInfoに残るsourceの観測値
		if ( Reopened != nullptr )
		{
			NewWorldInstance = Reopened->Animator.Instance;
			CachedSource = Reopened->Source;
		}
		NDebugUI::NewWorld( State );
		Checker.Check( State.ModelPreviews.IsEmpty() && !NewWorldInstance.IsValid() && CachedSource.IsValid(), "Newは専用instanceを解放しAssetInfo cacheのsourceを保持する" );
		Checker.Check( Backend == nullptr || Backend->GetNumModelMaterialGeometries() == InitialGeometryCount, "New後に専用geometry resourceが残らない" );
		const bool bReleaseWorldOpened = NDebugUI::OpenWorldFile( State, "world_model_preview_real.world" ); // Release前に専用ownerを再作成するworld読込
		NWorldEditorUI::UpdateModelMaterialPreviews( State, 0.0f, &Executor );
		FWorldEditorModelPreview* BeforeRelease = State.ModelPreviews.Find( TargetKey ); // Release直前の専用view
		TWeakPtr<CModelInstance> ReleaseInstance; // Release後に消えるべきinstance
		TWeakPtr<CModel> ReleaseSource; // Release後に消えるべきAssetInfo source
		if ( BeforeRelease != nullptr )
		{
			ReleaseInstance = BeforeRelease->Animator.Instance;
			ReleaseSource = BeforeRelease->Source;
		}
		const bool bReleaseOwnerPrepared = bReleaseWorldOpened && BeforeRelease != nullptr && BeforeRelease->Animator.Instance.IsValid() && BeforeRelease->Animator.GetMaterialEffect().IsPrepared() && ( Backend == nullptr || Backend->GetNumModelMaterialGeometries() > InitialGeometryCount ); // 解放対象ownerが存在するか
		Checker.Check( bReleaseOwnerPrepared, "Release検査用に専用instanceとgeometry ownerを再準備する" );
		NDebugUI::ReleaseWorldEditor( State );
		Assets.ReleaseUnused();
		Assets.Tick( 0.0f );
		Checker.Check( State.ModelPreviews.IsEmpty() && !State.AssetInfo.IsValid() && !ReleaseInstance.IsValid() && !ReleaseSource.IsValid(), "ReleaseWorldEditorは存在するpreview・AssetInfo・source ownerを解放する" );
		Checker.Check( Backend == nullptr || Backend->GetNumModelMaterialGeometries() == InitialGeometryCount, "ReleaseWorldEditor後にこのpreviewが追加したgeometry resourceが残らない" );
		DrawState.Restore();
		if ( Soft >= 0 ) DeleteSoftImage( Soft );
		if ( Screen >= 0 ) DeleteGraph( Screen );
		Assets.SetRootDirectory( OldRoot );
		Checker.Group = OldGroup;
	}
}
