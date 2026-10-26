// SPDX-License-Identifier: NOASSERTION
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/EffectEditor.h"
#include "Library/DebugUI/EffectEditorInternal.h"
#include "Library/DebugUI/AssetTools.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Json/JsonObject.h"
#include "Library/Platform/FileSystem.h"
#include "WorldEffectSelfTestSupport.h"

namespace
{
	// 粒子が動かず長く残る、継続生成だけの編集確認用定義を作る。
	FParticleEffectDefinition MakeEditorEffect()
	{
		// 保存・読み込みの基準となる定義。
		FParticleEffectDefinition Definition;
		Definition.Name = "Editor test";
		// 更新ごとの生成数を数えやすいエミッター。
		FParticleEmitterDefinition Emitter = NEffectTest::QuietEmitter();
		Emitter.Rate = 10.0f;
		Emitter.Lifetime = { 10.0f, 10.0f };
		Definition.Emitters.Add( Emitter );
		return Definition;
	}

	// 内蔵の試し再生の状態を調べる。作成に失敗した場合は空。
	const FParticleSimulation* PreviewSimulation( const FEffectEditorState& State )
	{
		// 型を確認してから読み取る、内蔵の再生。
		const FParticlePlayback* Playback = dynamic_cast<const FParticlePlayback*>( State.Playback.Get() );
		return Playback != nullptr ? &Playback->GetSimulation() : nullptr;
	}

	// 試し再生の経過秒を返す。状態が無ければ比較で失敗する非数を返す。
	float32 PreviewTime( const FEffectEditorState& State )
	{
		// 呼び出し時点の試し再生の状態。
		const FParticleSimulation* Simulation = PreviewSimulation( State );
		return Simulation != nullptr ? Simulation->Time : TNumericLimits<float32>::QuietNaN();
	}

	// 編集と保存済みアセットを分離し、更新・停止・履歴・終了を実際に進めて確認する。
	void TestPreviewLifecycle( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& SourcePath )
	{
		C.Group = "effect-editor-real-lifecycle";
		// このテストの文書と再生を所有する編集状態。
		FEffectEditorState State;
		NDebugUI::NewEffect( State );
		C.Check( State.bInitialized && State.Document.IsDirty() && State.Document.Get().Name == "NewEffect" && State.Document.Get().Emitters.Num() == 1 && State.SelectedEmitter == 0 && State.FilePath.IsEmpty(), "新規文書は 1 個のエミッターを選び、未保存として始まる" );
		NDebugUI::UpdateEffectEditor( State, 0.0f );
		// 読み込み失敗の前に保持していた文書全体。
		const NlohmannJson BeforeFailure = State.Document.Get().ToJson();
		// 読み込み失敗でも保持する再生。
		const TSharedPtr<IEffectPlayback> BeforeFailurePlayback = State.Playback;
		C.Check( !NDebugUI::OpenEffectFile( State, "editor-real/absent.effect.json" ) && State.Document.Get().ToJson() == BeforeFailure && State.Playback == BeforeFailurePlayback && State.Document.IsDirty() && State.FilePath.IsEmpty(), "開く操作に失敗しても、編集中の文書・再生・未保存の印を保つ" );
		C.Check( NDebugUI::OpenEffectFile( State, SourcePath ) && !State.Document.IsDirty() && State.FilePath == SourcePath && State.SelectedEmitter == 0 && !State.Playback.IsValid(), "開く操作は保存済みの文書に入れ替え、古い試し再生を破棄する" );
		// 別の利用者として、読み込んだアセットを保持する。
		const TSharedPtr<CEffectAsset> Loaded = Assets.LoadEffect( SourcePath );
		NDebugUI::UpdateEffectEditor( State, 0.1f );
		// 編集前の再生を、変更後も保持して比較する。
		const TSharedPtr<IEffectPlayback> First = State.Playback;
		// 編集前の内蔵再生の状態。
		const FParticleSimulation* FirstSimulation = PreviewSimulation( State );
		C.Check( Loaded.IsValid() && First.IsValid() && FirstSimulation != nullptr && First->GetNumParticles() == 1 && FirstSimulation->Definition.Get() != Loaded->GetDefinitionShared().Get() && FirstSimulation->Definition->Emitters[0].Rate == 10.0f, "保存済みアセットを共有せず、定義のコピーから試し再生を作る" );
		// 保存前の変更として倍の数を出す設定。
		FParticleEmitterDefinition Edited = State.Document.Get().Emitters[0];
		Edited.Rate = 20.0f;
		C.Check( NParticleEdit::SetEmitter( State.Document, 0, Edited ), "保存前の生成速度を変更する" );
		NDebugUI::UpdateEffectEditor( State, 0.1f );
		C.Check( State.Playback.IsValid() && State.Playback != First && State.Playback->GetNumParticles() == 2 && Loaded.IsValid() && Loaded->GetDefinition().Emitters[0].Rate == 10.0f && FirstSimulation != nullptr && FirstSimulation->Definition->Emitters[0].Rate == 10.0f && First->GetNumParticles() == 1 && NMath::Abs( FirstSimulation->Time - 0.1f ) < 1.0e-6f, "未保存の変更は新しい試し再生だけへ反映し、共有アセットと古い再生を変えない" );
		// 一時停止中に保つ再生位置。
		const float32 PausedTime = PreviewTime( State );
		State.bPlaying = false;
		NDebugUI::UpdateEffectEditor( State, 0.1f );
		C.Check( PreviewTime( State ) == PausedTime, "一時停止中は再生位置を進めない" );
		State.bPlaying = true;
		// 無効な刻みと、明示的に進めない刻み。
		const float32 InvalidSteps[] = { 0.0f, -1.0f, TNumericLimits<float32>::QuietNaN(), TNumericLimits<float32>::Infinity() };
		// いずれも再生位置を変えてはいけない入力。
		for ( const float32 Step : InvalidSteps ) NDebugUI::UpdateEffectEditor( State, Step );
		C.Check( PreviewTime( State ) == PausedTime, "0・負・非数・無限大の刻みでは時間を進めない" );
		State.PreviewSpeed = 0.0f;
		NDebugUI::UpdateEffectEditor( State, 0.1f );
		C.Check( PreviewTime( State ) == PausedTime, "プレビュー速度 0 では粒子の時間を進めない" );
		State.PreviewSpeed = TNumericLimits<float32>::QuietNaN();
		NDebugUI::UpdateEffectEditor( State, 0.1f );
		C.Check( NMath::Abs( PreviewTime( State ) - PausedTime - 0.1f ) < 1.0e-6f, "非数のプレビュー速度は通常速度として扱う" );
		State.PreviewSpeed = 8.0f;
		NDebugUI::UpdateEffectEditor( State, 10.0f );
		C.Check( NMath::Abs( PreviewTime( State ) - PausedTime - 0.5f ) < 1.0e-6f, "長い停止後の刻みは 0.1 秒、プレビュー速度は 4 倍までに抑える" );
		State.PreviewSpeed = 1.0f;
		C.Check( State.Document.Undo(), "保存前の変更を取り消す" );
		NDebugUI::UpdateEffectEditor( State, 0.1f );
		C.Check( State.Playback.IsValid() && State.Playback->GetNumParticles() == 1 && !State.Document.IsDirty(), "取り消した定義を新しい試し再生へ反映する" );
		C.Check( State.Document.Redo(), "生成速度の変更をやり直す" );
		NDebugUI::UpdateEffectEditor( State, 0.1f );
		C.Check( State.Playback.IsValid() && State.Playback->GetNumParticles() == 2 && State.Document.IsDirty(), "やり直した定義も再生へ反映する" );
		// 再開始で手放される再生を、寿命を延ばさず観察する。
		const TWeakPtr<IEffectPlayback> Restarted = State.Playback;
		NDebugUI::RestartEffectPreview( State );
		C.Check( !State.Playback.IsValid() && !Restarted.IsValid() && State.FinishedTime == 0.0f, "再開始は現在の再生を解放して待ち時間を消す" );
		State.bPlaying = false;
		NDebugUI::UpdateEffectEditor( State, 0.1f );
		C.Check( State.Playback.IsValid() && PreviewTime( State ) == 0.0f && State.Playback->GetNumParticles() == 0, "一時停止中でも新しい定義の再生を作り、時間は進めない" );
		State.bPlaying = true;
		Edited.Lifetime = { 0.0f, 0.0f };
		NParticleEdit::SetEmitter( State.Document, 0, Edited );
		NDebugUI::UpdateEffectEditor( State, 0.0f );
		// 拒否された保存では増やさない保存成功番号。
		const uint32 InvalidSerial = State.SaveSerial;
		C.Check( !State.Playback.IsValid() && FParticleEffectDefinition::HasErrors( State.Issues ) && !NDebugUI::SaveEffectFile( State ) && State.Document.IsDirty() && State.FilePath == SourcePath && State.SaveSerial == InvalidSerial, "不正な定義は再生せず、保存も拒否して未保存の状態を保つ" );
		C.Check( State.Document.Undo(), "不正な定義の変更を取り消す" );
		NDebugUI::UpdateEffectEditor( State, 0.0f );
		C.Check( State.Playback.IsValid() && !FParticleEffectDefinition::HasErrors( State.Issues ), "定義を直せば試し再生も復帰する" );
		// 終了時に解放される最後の再生。
		const TWeakPtr<IEffectPlayback> Released = State.Playback;
		// 解放後も保持する編集中の文書。
		const NlohmannJson BeforeRelease = State.Document.Get().ToJson();
		NDebugUI::ReleaseEffectEditor( State );
		C.Check( !State.Playback.IsValid() && !Released.IsValid() && State.Surface.GetHandle() < 0 && State.Document.Get().ToJson() == BeforeRelease, "終了処理は再生と画面を解放し、文書を保つ" );
		C.Check( NDebugUI::OpenEffectFile( State, SourcePath ), "終了処理後も同じ編集状態でファイルを開ける" );
		NDebugUI::UpdateEffectEditor( State, 0.1f );
		C.Check( State.Playback.IsValid() && State.Playback->GetNumParticles() == 1, "開き直した後に新しい試し再生を始められる" );
		NDebugUI::ReleaseEffectEditor( State );
	}

	// 一度だけの再生を最後まで進め、終了後の待ち時間と繰り返しを確認する。
	void TestRepeat( NAssetTest::FChecker& C )
	{
		C.Group = "effect-editor-real-repeat";
		// 一度だけ粒子を出す、短い定義。
		FParticleEffectDefinition Once;
		Once.Duration = 0.1f;
		Once.bLoop = false;
		// 終了位置を 0.1 秒刻みで確かめる粒子。
		FParticleEmitterDefinition Emitter = NEffectTest::QuietEmitter();
		Emitter.Lifetime = { 0.15f, 0.15f };
		Emitter.Bursts.Add( NEffectTest::Burst( 0.0f, 1 ) );
		Once.Emitters.Add( Emitter );
		// 繰り返しを最初は無効にした編集状態。
		FEffectEditorState State;
		State.Document.Reset( Once );
		State.bRepeatPreview = false;
		NDebugUI::UpdateEffectEditor( State, 0.1f );
		C.Check( State.Playback.IsValid() && State.Playback->GetNumParticles() == 1 && !State.Playback->IsFinished(), "一度だけの定義も、開始時には粒子が生まれる" );
		NDebugUI::UpdateEffectEditor( State, 0.1f );
		NDebugUI::UpdateEffectEditor( State, 0.1f );
		C.Check( State.Playback.IsValid() && State.Playback->IsFinished() && State.FinishedTime == 0.0f, "繰り返しを切れば終了した状態を保ち、待ち時間を増やさない" );
		State.bRepeatPreview = true;
		// 待ち時間を超えていない最初の 0.7 秒。
		for ( int32 Index = 0; Index < 7; ++Index ) NDebugUI::UpdateEffectEditor( State, 0.1f );
		C.Check( State.Playback.IsValid() && State.Playback->IsFinished() && State.FinishedTime > 0.69f && State.FinishedTime < 0.75f, "繰り返しを有効にしても、終了から 0.75 秒未満なら再開始しない" );
		NDebugUI::UpdateEffectEditor( State, 0.1f );
		C.Check( !State.Playback.IsValid() && State.FinishedTime == 0.0f, "待ち時間を過ぎたフレームで再生を捨て、次の更新へ再開始を予約する" );
		NDebugUI::UpdateEffectEditor( State, 0.1f );
		C.Check( State.Playback.IsValid() && State.Playback->GetNumParticles() == 1 && !State.Playback->IsFinished(), "待ち時間の後は、同じ定義を最初から再生する" );
		NDebugUI::ReleaseEffectEditor( State );
	}

	// 4 行 4 列の行列が、描画状態の復元で変わっていないかを確かめる。
	bool SameMatrix( const MATRIX& A, const MATRIX& B )
	{
		// 比べる行の番号。
		for ( int32 Row = 0; Row < 4; ++Row )
		{
			// 比べる列の番号。
			for ( int32 Column = 0; Column < 4; ++Column ) if ( NMath::Abs( A.m[Row][Column] - B.m[Row][Column] ) > 1.0e-5f ) return false;
		}
		return true;
	}

	// テスト用の画面切り替えを、呼び出し元の描画設定へ戻す。
	struct FCallerDrawState
	{
		// 元の描画先。
		int32 Screen = GetDrawScreen();
		// 画面切り替え時の自動初期化の設定。
		int32 ScreenReset = GetUseSetDrawScreenSettingReset();
		// 元のカメラの近い側の表示限界。
		float32 Near = GetCameraNear();
		// 元のカメラの遠い側の表示限界。
		float32 Far = GetCameraFar();
		// 元のカメラの向きと位置。
		MATRIX CameraView = GetCameraViewMatrix();
		// 元のカメラの投影。
		MATRIX CameraProjection = GetCameraProjectionMatrix();
		// 元の物体の座標変換。
		MATRIX World = {};
		// 直接指定された元の視点変換。
		MATRIX View = {};
		// 直接指定された元の投影変換。
		MATRIX Projection = {};
		// 元の描画範囲への配置。
		MATRIX Viewport = {};
		// 元の描画を許す範囲。
		RECT Area = {};
		// 元の色の混ぜ方。
		int32 Blend = 0;
		// 元の混ぜる強さ。
		int32 BlendParam = 0;
		// 元の描画色の赤倍率。
		int32 BrightR = 255;
		// 元の描画色の緑倍率。
		int32 BrightG = 255;
		// 元の描画色の青倍率。
		int32 BrightB = 255;

		// 画面を変える前の、直接指定された行列と描画設定を保持する。
		FCallerDrawState()
		{
			GetTransformToWorldMatrix( &World );
			GetTransformToViewMatrix( &View );
			GetTransformToProjectionMatrix( &Projection );
			GetTransformToViewportMatrix( &Viewport );
			GetDrawArea( &Area );
			GetDrawBlendMode( &Blend, &BlendParam );
			GetDrawBright( &BrightR, &BrightG, &BrightB );
		}

		// テスト用画面の破棄前に元の画面へ戻し、設定を復元する。
		~FCallerDrawState()
		{
			SetUseSetDrawScreenSettingReset( FALSE );
			SetDrawScreen( Screen );
			SetCameraNearFar( Near, Far );
			SetCameraViewMatrix( CameraView );
			SetupCamera_ProjectionMatrix( CameraProjection );
			SetTransformToWorld( &World );
			SetTransformToView( &View );
			SetTransformToProjection( &Projection );
			SetTransformToViewport( &Viewport );
			SetDrawArea( Area.left, Area.top, Area.right, Area.bottom );
			SetDrawBlendMode( Blend, BlendParam );
			SetDrawBright( BrightR, BrightG, BrightB );
			SetUseSetDrawScreenSettingReset( ScreenReset );
		}
	};

	// 専用描画先の確保・描画・再利用・再確保・解放と、呼び出し元の画面復元を確認する。
	void TestPreviewSurface( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor )
	{
		C.Group = "effect-editor-real-surface";
		// 最後に復元する、この確認を呼び出した側の描画状態。
		const FCallerDrawState Original;
		// プレビューとは違う大きさの、呼び出し元を模した描画先。
		const int32 Caller = MakeScreen( 80, 60, FALSE );
		C.Check( Caller >= 0, "プレビューの呼び出し元を模した描画先を作成できる" );
		if ( Caller < 0 ) return;
		SetUseSetDrawScreenSettingReset( FALSE );
		SetDrawScreen( Caller );
		SetCameraNearFar( 0.2f, 90.0f );
		SetupCamera_Perspective( 0.95f );
		SetCameraPositionAndTargetAndUpVec( VGet( 2.0f, 3.0f, -7.0f ), VGet( 0.0f, 1.0f, 0.0f ), VGet( 0.0f, 1.0f, 0.0f ) );
		// プレビューに漏らさず、描画後には復元する物体の変換。
		const MATRIX CallerWorld = MGetTranslate( VGet( 3.0f, 4.0f, 5.0f ) );
		SetTransformToWorld( &CallerWorld );
		SetDrawArea( 3, 4, 70, 50 );
		SetDrawBlendMode( DX_BLENDMODE_ADD, 137 );
		SetDrawBright( 160, 180, 200 );
		// 描画で変わってはいけないカメラの向きと位置。
		const MATRIX BeforeView = GetCameraViewMatrix();
		// 描画で変わってはいけないカメラの投影。
		const MATRIX BeforeProjection = GetCameraProjectionMatrix();
		// 格子から離れた赤い粒子を表示する定義。
		TSharedPtr<FParticleEffectDefinition> Definition = MakeShared<FParticleEffectDefinition>();
		// 中心の色を実際のピクセルで確かめる、動かない粒子。
		FParticleEmitterDefinition Emitter = NEffectTest::QuietEmitter();
		Emitter.StartColor = Emitter.EndColor = FColor32( 255, 0, 0, 255 );
		Emitter.StartSize = Emitter.EndSize = { 1.2f, 1.2f };
		Emitter.Bursts.Add( NEffectTest::Burst( 0.0f, 1 ) );
		Definition->Emitters.Add( Emitter );
		// 保存前の定義を受け取る、本番と同じ再生作成関数。
		const TSharedPtr<IEffectPlayback> Playback = NEffect::CreateParticlePlayback( Definition, &Assets, FEffectPlayParams(), FTransform( FVector3( 1.0f, 1.0f, 0.0f ) ), "surface-test" );
		C.Check( !NEffect::CreateParticlePlayback( TSharedPtr<const FParticleEffectDefinition>(), &Assets, FEffectPlayParams(), FTransform() ).IsValid() && !NEffect::CreateParticlePlayback( Definition, nullptr, FEffectPlayParams(), FTransform() ).IsValid(), "定義またはアセット管理が無い場合は、保存前の再生作成を拒否する" );
		if ( Playback.IsValid() ) Playback->Update( 0.1f );
		// 粒子の方へ正面から向けるカメラ。
		FEditorCamera Camera;
		Camera.Yaw = 0.0f;
		Camera.Pitch = 0.0f;
		Camera.Target = FVector3( 0.0f, 1.0f, 0.0f );
		Camera.Distance = 6.0f;
		// 呼び出し元とは異なる縦横比の表示範囲。
		FEditorViewport View;
		View.Width = 160;
		View.Height = 120;
		// 確認が終わるまで所有するプレビュー描画先。
		FEffectPreviewSurface Surface;
		C.Check( Playback.IsValid() && Surface.Render( Camera, View, Playback.Get(), Executor ) && Surface.GetHandle() >= 0, "保存前の再生を専用画面へ実際に描画できる" );
		// 確保した最初の画面。
		const int32 FirstHandle = Surface.GetHandle();
		// 実際に確保された画面の横幅。
		int32 Width = 0;
		// 実際に確保された画面の縦幅。
		int32 Height = 0;
		C.Check( GetDrawScreen() == Caller && SameMatrix( GetCameraViewMatrix(), BeforeView ) && SameMatrix( GetCameraProjectionMatrix(), BeforeProjection ) && Surface.GetHandle() >= 0 && GetGraphSize( Surface.GetHandle(), &Width, &Height ) == 0 && Width == 160 && Height == 120, "描画先とカメラ行列を呼び出し元へ戻し、要求どおりの大きさを確保する" );
		// 描画後に復元された物体の変換。
		MATRIX RestoredWorld = {};
		GetTransformToWorldMatrix( &RestoredWorld );
		// 描画後に復元された範囲。
		RECT RestoredArea = {};
		GetDrawArea( &RestoredArea );
		C.Check( SameMatrix( RestoredWorld, CallerWorld ) && RestoredArea.left == 3 && RestoredArea.top == 4 && RestoredArea.right == 70 && RestoredArea.bottom == 50, "呼び出し元の物体の変換と描画範囲も復元する" );
		C.Check( Surface.Render( Camera, View, Playback.Get(), Executor ) && Surface.GetHandle() == FirstHandle, "同じ大きさで描き直す場合は画面を再利用する" );
		// 粒子の中心を読み取る、画面と同じ大きさの画像。
		const int32 Soft = MakeARGB8ColorSoftImage( View.Width, View.Height );
		if ( Surface.GetHandle() >= 0 && Soft >= 0 )
		{
			SetDrawScreen( Surface.GetHandle() );
			GetDrawScreenSoftImage( 0, 0, View.Width, View.Height, Soft );
			// 粒子の位置を、描画と独立したカメラ計算で投影した点。
			FVector2 Centre;
			// 粒子の中心ピクセルの赤成分。
			int32 R = 0;
			// 粒子の中心ピクセルの緑成分。
			int32 G = 0;
			// 粒子の中心ピクセルの青成分。
			int32 B = 0;
			// 粒子の中心ピクセルの不透明度。
			int32 A = 0;
			if ( Camera.WorldToScreen( View, FVector3( 1.0f, 1.0f, 0.0f ), Centre ) ) GetPixelSoftImage( Soft, static_cast<int32>( std::lround( Centre.x ) ), static_cast<int32>( std::lround( Centre.y ) ), &R, &G, &B, &A );
			SetDrawScreen( Caller );
			C.Check( R > 180 && G < 100 && B < 110, "プレビューの投影位置に赤い粒子が描かれる（%d, %d, %d）", R, G, B );
		}
		else C.Check( false, "プレビューのピクセルを取得する画像を作成できる" );
		if ( Soft >= 0 ) DeleteSoftImage( Soft );
		View.Width = 240;
		View.Height = 96;
		C.Check( Surface.Render( Camera, View, Playback.Get(), Executor ) && Surface.GetHandle() != FirstHandle && GetGraphSize( Surface.GetHandle(), &Width, &Height ) == 0 && Width == 240 && Height == 96 && GetDrawScreen() == Caller, "大きさの変更時は新しい画面を確保し、元の描画先へ戻す" );
		// 不正な表示範囲でも保持する、作り直し後の画面。
		const int32 ResizedHandle = Surface.GetHandle();
		View.Width = 1;
		C.Check( !Surface.Render( Camera, View, Playback.Get(), Executor ) && Surface.GetHandle() == ResizedHandle && GetDrawScreen() == Caller, "不正な表示範囲は描かず、前の画面と呼び出し元を保持する" );
		View.Width = 240;
		C.Check( Surface.Render( Camera, View, nullptr, Executor ) && Surface.GetHandle() == ResizedHandle, "再生が空でも同じ画面に格子だけを描ける" );
		Surface.Release();
		Surface.Release();
		C.Check( Surface.GetHandle() < 0 && GetGraphSize( ResizedHandle, &Width, &Height ) < 0 && GetDrawScreen() == Caller, "繰り返し解放しても安全で、所有していた画面を破棄する" );
		SetDrawScreen( Original.Screen );
		DeleteGraph( Caller );
	}

	// 保存の成否と未保存確認の分岐を、実ファイルを使って確認する。
	void TestFilesAndPending( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir, const FString& SourcePath )
	{
		C.Group = "effect-editor-real-files";
		// 保存先の拡張子と区切りを確認する編集状態。
		FEffectEditorState State;
		NDebugUI::NewEffect( State );
		C.Check( NDebugUI::SaveEffectFile( State, "  editor-real\\saved  " ) && State.FilePath == "editor-real/saved.effect.json" && State.SaveAsPath == State.FilePath && !State.Document.IsDirty() && State.SaveSerial == 1u && NFileSystem::FileExists( Dir / State.FilePath ), "保存は余白と区切りを揃えて拡張子を補い、成功後にだけ保存済みとする" );
		// 保存先から読み戻した定義。
		const TSharedPtr<CEffectAsset> Saved = Assets.LoadEffect( State.FilePath );
		C.Check( Saved.IsValid() && Saved->GetDefinition().ToJson() == State.Document.Get().ToJson(), "保存した定義は全項目を同じ内容で読み戻せる" );
		C.Check( NDebugUI::SaveEffectFile( State ) && State.SaveSerial == 2u && !State.Document.IsDirty(), "保存先を省略すれば同じファイルへ保存し、成功番号を増やす" );
		// ファイルとして保存できない、同名のディレクトリ。
		const FString Blocked = "editor-real/blocked.effect.json";
		C.Check( NFileSystem::EnsureDirectory( Dir / Blocked ), "保存失敗を確認するため、同名のディレクトリを用意する" );
		NParticleEdit::SetProperties( State.Document, "Unsaved name", 0.0f, true, 1u );
		// 保存失敗前のファイル名。
		const FString OldPath = State.FilePath;
		// 保存失敗前の内容と成功番号。
		const NlohmannJson BeforeFailure = State.Document.Get().ToJson();
		const uint32 OldSerial = State.SaveSerial;
		C.Check( !NDebugUI::SaveEffectFile( State, Blocked ) && State.Document.Get().ToJson() == BeforeFailure && State.FilePath == OldPath && State.SaveSerial == OldSerial && State.Document.IsDirty(), "書き込み失敗では、文書・保存先・成功番号・未保存の印を保つ" );

		// キャッシュとは別に、外部の編集者が書き換える保存先。
		const FString ExternalPath = "editor-real/external.effect.json";
		C.Check( Assets.SaveEffect( ExternalPath, MakeEditorEffect() ), "外部編集前のファイルを保存する" );
		// 別利用者が長く保持する、外部編集前の読み込み済みアセット。
		const TSharedPtr<CEffectAsset> Cached = Assets.LoadEffect( ExternalPath );
		// ファイルだけを直接変更する、新しい内容。
		FParticleEffectDefinition Disk = MakeEditorEffect();
		Disk.Name = "Disk edit";
		Disk.Emitters[0].Rate = 30.0f;
		// アセットのキャッシュ破棄を伴わずに書き込む JSON。
		NJson::CJsonObject ExternalJson;
		ExternalJson.GetRoot() = Disk.ToJson();
		C.Check( ExternalJson.SaveToFile( Dir / ExternalPath ) && NDebugUI::OpenEffectFile( State, ExternalPath ) && State.Document.Get().Name == "Disk edit" && State.Document.Get().Emitters[0].Rate == 30.0f && Cached.IsValid() && Cached->GetDefinition().Emitters[0].Rate == 10.0f && Cached->GetDefinition().Name == "Editor test", "明示的に開き直すとディスクの新版を読み、別利用者が保持する旧アセットは変更しない" );

		C.Group = "effect-editor-real-pending";
		NDebugUI::NewEffect( State );
		State.SaveAsPath = Blocked;
		// 閉じる処理で実際に変更される表示フラグ。
		bool bOpen = true;
		// 保留中の操作では変えない文書。
		const NlohmannJson PendingDocument = State.Document.Get().ToJson();
		C.Check( !NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::New, FString(), &bOpen ) && State.Pending == FEffectEditorState::EPending::New && State.bRequestUnsavedPopup && State.Document.Get().ToJson() == PendingDocument && bOpen, "未保存文書への新規要求は、文書を変えず確認待ちにする" );
		C.Check( !NEffectEditorUI::CompletePendingAction( State, true, &bOpen ) && State.Pending == FEffectEditorState::EPending::New && State.bRequestUnsavedPopup && State.Document.Get().ToJson() == PendingDocument && State.Document.IsDirty() && bOpen, "保存して続行が失敗すれば、文書と保留中の操作を残す" );
		C.Check( NEffectEditorUI::CompletePendingAction( State, false, &bOpen ) && State.Pending == FEffectEditorState::EPending::None && !State.bRequestUnsavedPopup && State.Document.IsDirty() && State.FilePath.IsEmpty() && State.Document.Get().Emitters.Num() == 1, "変更を破棄して続行すれば、新規文書を作って確認待ちを解く" );
		C.Check( !NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Open, SourcePath, &bOpen ) && State.Pending == FEffectEditorState::EPending::Open && State.PendingPath == SourcePath && State.FilePath.IsEmpty(), "未保存文書への開く要求は、読み込み先だけを保留する" );
		C.Check( NEffectEditorUI::CompletePendingAction( State, false, &bOpen ) && State.FilePath == SourcePath && !State.Document.IsDirty() && State.Pending == FEffectEditorState::EPending::None && State.PendingPath.IsEmpty(), "開く要求を破棄して続行すると、選んだファイルを保存済みとして開く" );
		NParticleEdit::SetProperties( State.Document, "Before close", 0.0f, true, 1u );
		NDebugUI::UpdateEffectEditor( State, 0.1f );
		C.Check( !NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Close, FString(), &bOpen ) && bOpen && State.Playback.IsValid() && State.Pending == FEffectEditorState::EPending::Close, "未保存の閉じる要求は、表示と再生を保って確認待ちにする" );
		// 保存して閉じる前の成功番号。
		const uint32 BeforeCloseSerial = State.SaveSerial;
		C.Check( NEffectEditorUI::CompletePendingAction( State, true, &bOpen ) && !bOpen && !State.Document.IsDirty() && !State.Playback.IsValid() && State.SaveSerial == BeforeCloseSerial + 1u && State.Pending == FEffectEditorState::EPending::None, "保存して閉じると、保存成功後に再生を解放して表示を閉じる" );
		C.Check( !NEffectEditorUI::CompletePendingAction( State, false, &bOpen ), "保留中の操作が無ければ続行を拒否する" );
		C.Check( NDebugUI::OpenEffectFile( State, SourcePath ) && NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::New, FString(), &bOpen ) && State.Document.IsDirty() && State.FilePath.IsEmpty() && State.Pending == FEffectEditorState::EPending::None, "保存済み文書からの新規要求は、確認待ちなしで実行できる" );
		bOpen = true;
		C.Check( !NEffectEditorUI::RequestAction( State, FEffectEditorState::EPending::Close, FString(), &bOpen ) && NEffectEditorUI::CompletePendingAction( State, false, &bOpen ) && !bOpen && State.SaveSerial == BeforeCloseSerial + 1u, "変更を破棄して閉じる場合は保存成功番号を増やさない" );
		NDebugUI::ReleaseEffectEditor( State );
	}

	// 非表示中の API 保存も、配置済みプレビューへ一度だけ伝わることを実経路で確認する。
	void TestAssetToolsSaveNotification( NAssetTest::FChecker& C, const FString& SourcePath )
	{
		C.Group = "effect-editor-real-save-notification";
		// コマンドライン処理や画面表示を行わず、保存通知だけを通すツールの所有元。
		FAssetToolsState Tools;
		Tools.bCommandLineApplied = true;
		Tools.bShowAssets = false;
		Tools.bShowAudio = false;
		Tools.bShowSpriteSheetEditor = false;
		Tools.bShowModelAnimationEditor = false;
		Tools.bShowWorldEditor = false;
		Tools.bShowEffectEditor = false;
		C.Check( NDebugUI::OpenEffectFile( Tools.EffectEditor, SourcePath ), "非表示中の保存通知を確認する定義を開く" );
		NDebugUI::UpdateEffectEditor( Tools.EffectEditor, 0.1f );
		// 保存前から配置済みのオブジェクトが保持している再生。
		FWorldEditorState::FEffectPreview& Preview = Tools.WorldEditor.EffectPreviews.FindOrAdd( 7 );
		Preview.Playback = Tools.EffectEditor.Playback;
		C.Check( Preview.Playback.IsValid() && Tools.WorldEditor.EffectPreviews.Num() == 1, "配置済みオブジェクトの旧プレビューを一つ保持する" );
		// 保存通知で一度だけ増やす、配置済みプレビューの再開始番号。
		const uint32 BeforeRestart = Tools.WorldEditor.EffectRestartSerial;
		// 名前だけを変えて保存する前の、ほかの全体設定。
		const FParticleEffectDefinition& Definition = Tools.EffectEditor.Document.Get();
		C.Check( NParticleEdit::SetProperties( Tools.EffectEditor.Document, "Saved while hidden", Definition.Duration, Definition.bLoop, Definition.Seed ) && NDebugUI::SaveEffectFile( Tools.EffectEditor ) && Tools.EffectEditor.SaveSerial != Tools.ObservedEffectSaveSerial && Tools.WorldEditor.EffectPreviews.Num() == 1, "画面を表示せず API から編集・保存しても、次のツール更新まで通知を保持する" );
		NDebugUI::DrawAssetTools( Tools );
		C.Check( Tools.WorldEditor.EffectPreviews.IsEmpty() && Tools.WorldEditor.EffectRestartSerial == BeforeRestart + 1u && Tools.ObservedEffectSaveSerial == Tools.EffectEditor.SaveSerial, "非表示中の保存を実際のツール更新が検出し、旧プレビューを捨てて再開始番号を一度増やす" );
		NDebugUI::DrawAssetTools( Tools );
		C.Check( Tools.WorldEditor.EffectPreviews.IsEmpty() && Tools.WorldEditor.EffectRestartSerial == BeforeRestart + 1u && Tools.ObservedEffectSaveSerial == Tools.EffectEditor.SaveSerial, "同じ保存を次の更新でも重ねて通知しない" );
		NDebugUI::ReleaseEffectEditor( Tools.EffectEditor );
	}
}

// 実ファイルと実際の再生を使い、編集画面の文書・保存・再生の寿命を確認する。
void NWorldTest::RunEffectEditorRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& Dir )
{
	// この関数専用の、アセットのルートからの保存先。
	const FString SourcePath = "editor-real/source.effect.json";
	C.Group = "effect-editor-real-setup";
	C.Check( Assets.SaveEffect( SourcePath, MakeEditorEffect() ), "編集画面の実機確認用の定義を保存する" );
	TestPreviewLifecycle( C, Assets, SourcePath );
	TestRepeat( C );
	TestFilesAndPending( C, Assets, Dir, SourcePath );
	TestPreviewSurface( C, Assets, Executor );
	TestAssetToolsSaveNotification( C, SourcePath );
}
