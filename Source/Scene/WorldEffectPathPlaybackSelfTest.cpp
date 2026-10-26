// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"

#include <cstring>
#include <utility>

#include "Library/Core/Interfaces/Render/DrawMaterialRibbon.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/DebugUI/EffectEditor.h"
#include "Library/DebugUI/EffectEditorInternal.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Component/TransformComponent.h"
#include "Library/Framework/Effect/EffectComponent.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/Scene/Scene.h"

namespace
{
	// 実際の再生が作った命令から、帯だけを集める。
	TArray<TArray<FDrawRibbonData::FPoint>> ReadStrips( const FDrawBuffer& Buffer )
	{
		// 命令から複写した点列。
		TArray<TArray<FDrawRibbonData::FPoint>> Result;
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type != EDrawCommand::Ribbon3D ) return;
			// 可変長の命令を読むための固定ヘッダー。
			const FDrawRibbonData Header = Command.Read<FDrawRibbonData>();
			// バッファの寿命から独立した点のコピー。
			TArray<FDrawRibbonData::FPoint> Points;
			Points.SetNum( Header.NumPoints );
			std::memcpy( Points.GetData(), reinterpret_cast<const uint8*>( Command.Data ) + sizeof( FDrawRibbonData ), static_cast<usize>( Header.NumPoints ) * sizeof( FDrawRibbonData::FPoint ) );
			Result.Add( MoveTemp( Points ) );
		} );
		return Result;
	}

	// 描画命令に記録された端の補助点指定を順に読む。
	TArray<int32> ReadJoinFlags( const FDrawBuffer& Buffer )
	{
		// 命令と同じ順序で保持する補助点指定。
		TArray<int32> Result;
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type == EDrawCommand::Ribbon3D ) Result.Add( Command.Read<FDrawRibbonData>().JoinFlags );
		} );
		return Result;
	}

	// 再生の記録は、更新や描画をせず同じ命令を返さなければならない。
	FDrawBuffer RecordPlayback( const TSharedPtr<IEffectPlayback>& Playback )
	{
		// この呼び出しだけが所有する描画命令。
		FDrawBuffer Buffer;
		// 描画実装へ依存しない記録窓口。
		FDrawContext Draw( Buffer );
		if ( Playback.IsValid() ) Playback->Record( Draw );
		return Buffer;
	}

	// 描画契約の位置と、計算で期待した位置を比較する。
	bool NearPoint( const FDrawRibbonData::FPoint& Point, const FVector3& Position )
	{
		return NEffectTest::Near3( FVector3( Point.Pos[0], Point.Pos[1], Point.Pos[2] ), Position );
	}

	// 材質付き帯命令から各点のワールド座標列だけを複写して返す。
	TArray<TArray<FVector3>> ReadMaterialStripPositions( const FDrawBuffer& Buffer )
	{
		// 命令順に集めた材質帯の座標列。
		TArray<TArray<FVector3>> Result;
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type != EDrawCommand::MaterialRibbon3D || Command.NumDataWords * 4 < static_cast<int32>( sizeof( FDrawMaterialRibbonData ) ) ) return;
			// 可変点数を確認するための材質帯ヘッダー。
			const FDrawMaterialRibbonData Header = Command.Read<FDrawMaterialRibbonData>();
			if ( Header.Ribbon.NumPoints < 2 || Header.Ribbon.NumPoints > FDrawRibbonData::MaxPoints ) return;
			// ヘッダーと全頂点がバッファ内に収まる必要バイト数。
			const usize RequiredBytes = sizeof( FDrawMaterialRibbonData ) + static_cast<usize>( Header.Ribbon.NumPoints ) * sizeof( FDrawMaterialRibbonData::FPoint );
			if ( RequiredBytes > static_cast<usize>( Command.NumDataWords ) * 4 ) return;
			// 命令バッファ外の所有配列へ複写する頂点列。
			TArray<FDrawMaterialRibbonData::FPoint> RawPoints;
			RawPoints.SetNum( Header.Ribbon.NumPoints );
			std::memcpy( RawPoints.GetData(), reinterpret_cast<const uint8*>( Command.Data ) + sizeof( FDrawMaterialRibbonData ), static_cast<usize>( Header.Ribbon.NumPoints ) * sizeof( FDrawMaterialRibbonData::FPoint ) );
			// 比較しやすい三次元座標へ移した点列。
			TArray<FVector3> Positions;
			Positions.Reserve( RawPoints.Num() );
			// 各頂点から三次元位置だけを取り出して並べる。
			for ( const FDrawMaterialRibbonData::FPoint& Point : RawPoints ) Positions.Add( FVector3( Point.Path.Pos[0], Point.Path.Pos[1], Point.Path.Pos[2] ) );
			Result.Add( MoveTemp( Positions ) );
		} );
		return Result;
	}

	// Lightning と TreeLightning の動的終点指定・解除・共存・異常入力抑止を再生窓口で確認する。
	void TestDynamicTargetPlayback( NAssetTest::FChecker& C, CAssetManager& Assets )
	{
		using namespace NEffectTest;
		C.Group = "effect-path-target-playback";
		// 回転と拡大を含み、動的終点へ二重適用しないことを確認する再生姿勢。
		const FTransform Where( FVector3( 1.0f, 2.0f, -1.0f ), FQuaternion::FromAxisAngle( FVector3::Forward, 1.5707963268f ), FVector3( 2.0f, 2.0f, 2.0f ) );
		// End=(0,3,0) が拡大 2 倍・Z 軸 90 度回転された従来のフォールバック終点。
		const FVector3 FallbackEnd( -5.0f, 2.0f, -1.0f );
		// 動的終点を受け取る分岐雷エミッター。
		FParticleEmitterDefinition Bolt = QuietEmitter();
		Bolt.Path.Kind = FEffectPathDefinition::EKind::Lightning;
		Bolt.Path.bUseTarget = true;
		Bolt.Path.Branches = 2;
		Bolt.Path.End = FVector3( 0.0f, 3.0f, 0.0f );
		Bolt.Bursts.Add( Burst( 0.0f, 1 ) );
		Bolt.StartSize = Bolt.EndSize = { 0.1f, 0.1f };
		// 動的終点対応の分岐雷再生インスタンス。
		const TSharedPtr<IEffectPlayback> BoltPlayback = NEffect::CreateParticlePlayback( MakeEffect( { Bolt }, 0.0f, true, 17 ), &Assets, FEffectPlayParams(), Where );
		C.Check( BoltPlayback.IsValid(), "UseTarget=true の分岐雷再生を作成できる" );
		if ( !BoltPlayback.IsValid() ) return;
		BoltPlayback->Update( 0.1f );
		// 動的終点未指定時の記録点列。
		const TArray<TArray<FDrawRibbonData::FPoint>> DefaultStrips = ReadStrips( RecordPlayback( BoltPlayback ) );
		C.Check( DefaultStrips.Num() == 6 && NearPoint( DefaultStrips[0][0], Where.Position ) && NearPoint( DefaultStrips[0].Last(), FallbackEnd ) && NearPoint( DefaultStrips[3].Last(), FallbackEnd ), "SetTargetPosition 未呼出時は従来の位置・回転・拡大による EndOffset が終点になる" );
		// 再生側の回転・拡大と無関係に直接到達させるワールド終点。
		const FVector3 DynamicTarget( 6.0f, -1.0f, 4.0f );
		BoltPlayback->SetTargetPosition( DynamicTarget );
		// 動的終点指定後の記録点列。
		const TArray<TArray<FDrawRibbonData::FPoint>> TargetedStrips = ReadStrips( RecordPlayback( BoltPlayback ) );
		C.Check( TargetedStrips.Num() == 6 && NearPoint( TargetedStrips[0][0], Where.Position ) && NearPoint( TargetedStrips[0].Last(), DynamicTarget ) && NearPoint( TargetedStrips[3][0], Where.Position ) && NearPoint( TargetedStrips[3].Last(), DynamicTarget ), "SetTargetPosition 後は再生姿勢の回転・拡大を二重掛けせず指定ワールド座標が厳密に主幹終点になる" );
		BoltPlayback->ClearTargetPosition();
		// 動的終点解除後の記録点列。
		const TArray<TArray<FDrawRibbonData::FPoint>> ClearedStrips = ReadStrips( RecordPlayback( BoltPlayback ) );
		C.Check( ClearedStrips.Num() == 6 && NearPoint( ClearedStrips[0][0], Where.Position ) && NearPoint( ClearedStrips[0].Last(), FallbackEnd ), "ClearTargetPosition で再び定義側の EndOffset 終点へ戻る" );

		// 成長時間を 0 にして初回から幹先端が終点へ届く非材質 TreeLightning。
		FParticleEmitterDefinition TreeBolt = QuietEmitter();
		TreeBolt.Path.Kind = FEffectPathDefinition::EKind::TreeLightning;
		TreeBolt.Path.bUseTarget = true;
		TreeBolt.Path.End = FVector3( 0.0f, 3.0f, 0.0f );
		TreeBolt.Path.Segments = 12;
		TreeBolt.Path.Branches = 2;
		TreeBolt.Path.Tree.Depth = 2;
		TreeBolt.Path.Tree.ChildBranches = 2;
		TreeBolt.Path.Tree.GrowthSeconds = 0.0f;
		TreeBolt.Path.Tree.BranchDelaySeconds = 0.0f;
		TreeBolt.Path.Tree.StrokeCount = 1;
		TreeBolt.Path.Tree.StrokeInterval = 2.0f;
		TreeBolt.Path.Tree.StrokeSeconds = 1.0f;
		TreeBolt.Bursts.Add( Burst( 0.0f, 1 ) );
		TreeBolt.StartSize = TreeBolt.EndSize = { 0.14f, 0.14f };
		// TreeLightning で確認する動的ワールド終点。
		const FVector3 TreeTarget( -2.0f, 7.0f, 5.0f );
		// 非材質 TreeLightning の再生インスタンス。
		const TSharedPtr<IEffectPlayback> TreePlayback = NEffect::CreateParticlePlayback( MakeEffect( { TreeBolt }, 0.0f, true, 29 ), &Assets, FEffectPlayParams(), Where );
		C.Check( TreePlayback.IsValid(), "UseTarget=true の TreeLightning 再生を作成できる" );
		if ( !TreePlayback.IsValid() ) return;
		TreePlayback->Update( 0.05f );
		// 非材質 TreeLightning の未指定時記録。
		const TArray<TArray<FDrawRibbonData::FPoint>> DefaultTreeStrips = ReadStrips( RecordPlayback( TreePlayback ) );
		TreePlayback->SetTargetPosition( TreeTarget );
		// 非材質 TreeLightning の動的終点指定時記録。
		const TArray<TArray<FDrawRibbonData::FPoint>> TargetedTreeStrips = ReadStrips( RecordPlayback( TreePlayback ) );
		TreePlayback->ClearTargetPosition();
		// 非材質 TreeLightning の解除後記録。
		const TArray<TArray<FDrawRibbonData::FPoint>> ClearedTreeStrips = ReadStrips( RecordPlayback( TreePlayback ) );
		C.Check( !DefaultTreeStrips.IsEmpty() && NearPoint( DefaultTreeStrips[0][0], Where.Position ) && NearPoint( DefaultTreeStrips[0].Last(), FallbackEnd ) && !TargetedTreeStrips.IsEmpty() && NearPoint( TargetedTreeStrips[0][0], Where.Position ) && NearPoint( TargetedTreeStrips[0].Last(), TreeTarget ) && !ClearedTreeStrips.IsEmpty() && NearPoint( ClearedTreeStrips[0].Last(), FallbackEnd ), "非材質 TreeLightning でも SetTargetPosition で幹先端が指定終点へ到達し、ClearTargetPosition で元の EndOffset へ戻る" );

		// 材質を有効化した TreeLightning でも MaterialRibbon3D 経路で動的終点を確認する。
		FParticleEmitterDefinition MaterialTreeBolt = TreeBolt;
		MaterialTreeBolt.Material.bEnabled = true;
		// 材質付き TreeLightning の再生インスタンス。
		const TSharedPtr<IEffectPlayback> MaterialTreePlayback = NEffect::CreateParticlePlayback( MakeEffect( { MaterialTreeBolt }, 0.0f, true, 31 ), &Assets, FEffectPlayParams(), Where );
		C.Check( MaterialTreePlayback.IsValid(), "材質付き UseTarget=true の TreeLightning 再生を作成できる" );
		if ( !MaterialTreePlayback.IsValid() ) return;
		MaterialTreePlayback->Update( 0.05f );
		MaterialTreePlayback->SetTargetPosition( TreeTarget );
		// 材質付き TreeLightning の動的終点記録座標。
		const TArray<TArray<FVector3>> TargetedMaterialTree = ReadMaterialStripPositions( RecordPlayback( MaterialTreePlayback ) );
		MaterialTreePlayback->ClearTargetPosition();
		// 材質付き TreeLightning の解除後記録座標。
		const TArray<TArray<FVector3>> ClearedMaterialTree = ReadMaterialStripPositions( RecordPlayback( MaterialTreePlayback ) );
		C.Check( !TargetedMaterialTree.IsEmpty() && Near3( TargetedMaterialTree[0][0], Where.Position ) && Near3( TargetedMaterialTree[0].Last(), TreeTarget ) && !ClearedMaterialTree.IsEmpty() && Near3( ClearedMaterialTree[0].Last(), FallbackEnd ), "材質付き TreeLightning の MaterialRibbon3D 記録でも幹先端が動的終点と解除後終点へ一致する" );

		// Emitter 0 だけ動的終点を使い、Emitter 1 は局所 EndOffset を使う混在構成の 1 番目。
		FParticleEmitterDefinition DynamicEmitter = Bolt;
		DynamicEmitter.Path.Branches = 0;
		DynamicEmitter.Path.bUseTarget = true;
		// 同じエフェクト内で動的終点を使わない 2 番目の雷エミッター。
		FParticleEmitterDefinition LocalEmitter = Bolt;
		LocalEmitter.Path.Branches = 0;
		LocalEmitter.Path.bUseTarget = false;
		LocalEmitter.Path.End = FVector3( 0.0f, 0.0f, 4.0f );
		// Emitter 1 の期待ワールド終点（Z 方向 4 × 拡大 2 = +8）。
		const FVector3 LocalEmitterEnd( 1.0f, 2.0f, 7.0f );
		// 2 エミッター混在エフェクトの再生インスタンス。
		const TSharedPtr<IEffectPlayback> MixedPlayback = NEffect::CreateParticlePlayback( MakeEffect( { DynamicEmitter, LocalEmitter }, 0.0f, true, 43 ), &Assets, FEffectPlayParams(), Where );
		MixedPlayback->Update( 0.1f );
		MixedPlayback->SetTargetPosition( DynamicTarget );
		// 混在エフェクトから記録された 4 本の帯（各エミッターの外側と芯）。
		const TArray<TArray<FDrawRibbonData::FPoint>> MixedStrips = ReadStrips( RecordPlayback( MixedPlayback ) );
		C.Check( MixedStrips.Num() == 4 && NearPoint( MixedStrips[0].Last(), DynamicTarget ) && NearPoint( MixedStrips[1].Last(), DynamicTarget ) && NearPoint( MixedStrips[2].Last(), LocalEmitterEnd ) && NearPoint( MixedStrips[3].Last(), LocalEmitterEnd ), "2 エミッター構成では bUseTarget=true の Emitter 0 だけが動的終点へ向かい、Emitter 1 は EndOffset 終点を保つ" );

		// 非有限の NaN 終点座標。
		const FVector3 NanTarget( TNumericLimits<float32>::QuietNaN(), 0.0f, 0.0f );
		BoltPlayback->SetTargetPosition( NanTarget );
		TreePlayback->SetTargetPosition( NanTarget );
		C.Check( ReadStrips( RecordPlayback( BoltPlayback ) ).IsEmpty() && ReadStrips( RecordPlayback( TreePlayback ) ).IsEmpty(), "非有限の TargetPosition 指定時は EndOffset へ誤フォールバックせず雷の記録を抑止する" );
		BoltPlayback->SetTargetPosition( Where.Position );
		TreePlayback->SetTargetPosition( Where.Position );
		C.Check( ReadStrips( RecordPlayback( BoltPlayback ) ).IsEmpty() && ReadStrips( RecordPlayback( TreePlayback ) ).IsEmpty(), "始点と一致する TargetPosition でも安全に空の記録になる" );

		// JSON で UseTarget に数値を渡した壊れた Lightning エミッター。
		FParticleEmitterDefinition InvalidLightning = Bolt;
		// JSON で UseTarget に数値を渡した壊れた TreeLightning エミッター。
		FParticleEmitterDefinition InvalidTree = TreeBolt;
		// 壊れた Lightning 用の JSON 入力。
		NlohmannJson BadLightningJson = Bolt.Path.ToJson();
		BadLightningJson["UseTarget"] = 123;
		// 壊れた TreeLightning 用の JSON 入力。
		NlohmannJson BadTreeJson = TreeBolt.Path.ToJson();
		BadTreeJson["UseTarget"] = 123;
		C.Check( FEffectPathDefinition::FromJson( BadLightningJson, InvalidLightning.Path ) && InvalidLightning.Path.HasInvalidUseTarget() && FEffectPathDefinition::FromJson( BadTreeJson, InvalidTree.Path ) && InvalidTree.Path.HasInvalidUseTarget(), "再生抑止確認用の型不正 Path を構築できる" );
		// 型不正な Lightning と TreeLightning を持つ再生インスタンス。
		const TSharedPtr<IEffectPlayback> InvalidPlayback = NEffect::CreateParticlePlayback( MakeEffect( { InvalidLightning, InvalidTree }, 0.0f, true, 59 ), &Assets, FEffectPlayParams(), Where );
		InvalidPlayback->Update( 0.05f );
		// 動的終点未指定時における壊れた定義の記録結果。
		const bool bEmptyWithoutTarget = ReadStrips( RecordPlayback( InvalidPlayback ) ).IsEmpty();
		InvalidPlayback->SetTargetPosition( DynamicTarget );
		// 動的終点指定時における壊れた定義の記録結果。
		const bool bEmptyWithTarget = ReadStrips( RecordPlayback( InvalidPlayback ) ).IsEmpty();
		C.Check( bEmptyWithoutTarget && bEmptyWithTarget, "UseTarget が型不正の壊れた定義では Lightning も TreeLightning も雷を記録しない" );
	}

	// CEffectComponent の対象オブジェクト追従・ローカルオフセット・破棄時フォールバック・直接座標切替を独立シーンで確認する。
	void TestEffectComponentTarget( NAssetTest::FChecker& C, CAssetManager& Assets )
	{
		using namespace NEffectTest;
		C.Group = "effect-component-target";
		// 共有シーンの描画コマンド数を汚さないテスト専用のローカルシーン。
		CScene TargetScene;
		// コンポーネントから再生する動的終点対応の雷エミッター。
		FParticleEmitterDefinition Bolt = QuietEmitter();
		Bolt.Path.Kind = FEffectPathDefinition::EKind::Lightning;
		Bolt.Path.bUseTarget = true;
		Bolt.Path.Branches = 0;
		Bolt.Path.End = FVector3( 0.0f, 3.0f, 0.0f );
		Bolt.Bursts.Add( Burst( 0.0f, 1 ) );
		Bolt.Lifetime = { 20.0f, 20.0f };
		Bolt.StartSize = Bolt.EndSize = { 0.12f, 0.12f };
		// テスト専用アセットの保存先相対パス。
		const FString BoltAssetPath = "editor-path/component_target_bolt.effect.json";
		C.Check( Assets.SaveEffect( BoltAssetPath, *MakeEffect( { Bolt }, 0.0f, true, 71 ) ), "CEffectComponent 動的終点確認用の雷アセットを保存する" );

		// 雷を放つ側のオブジェクト位置。
		const FVector3 CasterPosition( 1.0f, 2.0f, -1.0f );
		// 動的終点解除時に戻る定義側のワールド終点（CasterPosition + (0,3,0)）。
		const FVector3 FallbackEnd( 1.0f, 5.0f, -1.0f );
		// 雷の発生元となるゲームオブジェクト。
		const TSharedPtr<CGameObject> Caster = TargetScene.SpawnObject<CGameObject>();
		Caster->AddComponent<CTransformComponent>()->SetLocalPosition( CasterPosition );
		// 検証対象の CEffectComponent。
		CEffectComponent* CasterFx = Caster->AddComponent<CEffectComponent>( BoltAssetPath );
		CasterFx->SetAutoPlay( false );

		// 追従先となる対象オブジェクト。
		const TSharedPtr<CGameObject> Target = TargetScene.SpawnObject<CGameObject>();
		// 対象オブジェクトの姿勢を操作する変換コンポーネント。
		CTransformComponent* TargetTransform = Target->AddComponent<CTransformComponent>();
		TargetTransform->SetLocalTransform( FTransform( FVector3( 4.0f, 1.0f, 2.0f ), FQuaternion::FromAxisAngle( FVector3::Forward, 1.5707963268f ), FVector3( 2.0f, 2.0f, 2.0f ) ) );
		// 対象オブジェクトのローカル空間で与える終点オフセット。
		const FVector3 LocalOffset( 0.0f, 1.5f, 0.0f );
		// 位置(4,1,2) + Z軸90度回転((0,1.5,0)*2) = (4,1,2) + (-3,0,0) = (1,1,2) の手計算値。
		const FVector3 ExpectedInitialTarget( 1.0f, 1.0f, 2.0f );
		CasterFx->SetTargetObject( Target, LocalOffset );
		// TryGetTargetPosition の出力受け取り用座標。
		FVector3 ResolvedTarget = FVector3::Zero;
		C.Check( CasterFx->GetTargetObject() == Target && Near3( CasterFx->GetTargetOffset(), LocalOffset ) && CasterFx->TryGetTargetPosition( ResolvedTarget ) && Near3( ResolvedTarget, ExpectedInitialTarget ) && Near3( ResolvedTarget, TargetTransform->GetWorldTransform().TransformPoint( LocalOffset ) ), "Play 前に設定した対象オブジェクトとローカルオフセットを TransformPoint の手計算値どおり解決する" );

		C.Check( CasterFx->Play() && CasterFx->GetPlayback().IsValid(), "対象オブジェクト設定済みの CEffectComponent を Play で開始できる" );
		if ( !CasterFx->GetPlayback().IsValid() ) return;
		// Tick を通す前に Playback 単体を 1 回進め、Play 直後の SyncPlaybackTarget が効いているか確かめる。
		CasterFx->GetPlayback()->Update( 0.1f );
		// Play 直後の初回更新で記録された帯点列。
		const TArray<TArray<FDrawRibbonData::FPoint>> PlayImmediateStrips = ReadStrips( RecordPlayback( CasterFx->GetPlayback() ) );
		C.Check( PlayImmediateStrips.Num() == 2 && NearPoint( PlayImmediateStrips[0][0], CasterPosition ) && NearPoint( PlayImmediateStrips[0].Last(), ExpectedInitialTarget ), "Play 前に設定した対象オブジェクト終点が Play 直後の初回描画記録から遅れなく反映される" );
		CasterFx->Tick( 0.1f );
		// 初回 Tick 後の記録点列。
		const TArray<TArray<FDrawRibbonData::FPoint>> FirstTickStrips = ReadStrips( RecordPlayback( CasterFx->GetPlayback() ) );
		C.Check( FirstTickStrips.Num() == 2 && NearPoint( FirstTickStrips[0][0], CasterPosition ) && NearPoint( FirstTickStrips[0].Last(), ExpectedInitialTarget ), "初回 Tick 後も主幹終点が TargetTransform の TransformPoint 位置に一致する" );

		// 次フレームで対象オブジェクトを別の位置と回転へ移動させる。
		TargetTransform->SetLocalTransform( FTransform( FVector3( -2.0f, 5.0f, 3.0f ), FQuaternion::FromAxisAngle( FVector3::Right, 1.5707963268f ), FVector3( 2.0f, 2.0f, 2.0f ) ) );
		// 位置(-2,5,3) + X軸90度回転((0,1.5,0)*2) = (-2,5,3) + (0,0,3) = (-2,5,6) の手計算値。
		const FVector3 ExpectedMovedTarget( -2.0f, 5.0f, 6.0f );
		CasterFx->Tick( 0.1f );
		// 対象移動後の記録点列。
		const TArray<TArray<FDrawRibbonData::FPoint>> MovedStrips = ReadStrips( RecordPlayback( CasterFx->GetPlayback() ) );
		C.Check( CasterFx->TryGetTargetPosition( ResolvedTarget ) && Near3( ResolvedTarget, ExpectedMovedTarget ) && Near3( ResolvedTarget, TargetTransform->GetWorldTransform().TransformPoint( LocalOffset ) ) && MovedStrips.Num() == 2 && NearPoint( MovedStrips[0].Last(), ExpectedMovedTarget ), "対象オブジェクトを移動・回転させて Tick を呼ぶと雷の終点が新しい TransformPoint 位置へ追従する" );

		// 対象オブジェクトを破棄予約すると、強参照が残っていても即座にフォールバックへ戻る。
		Target->Destroy();
		CasterFx->Tick( 0.1f );
		// 対象破棄後の記録点列。
		const TArray<TArray<FDrawRibbonData::FPoint>> DestroyedStrips = ReadStrips( RecordPlayback( CasterFx->GetPlayback() ) );
		C.Check( !CasterFx->TryGetTargetPosition( ResolvedTarget ) && DestroyedStrips.Num() == 2 && NearPoint( DestroyedStrips[0][0], CasterPosition ) && NearPoint( DestroyedStrips[0].Last(), FallbackEnd ), "Target->Destroy 後の Tick で TryGetTargetPosition が false になり定義側の Path.End フォールバックへ戻る" );

		// CTransformComponent を持たない空のゲームオブジェクト。
		const TSharedPtr<CGameObject> TransformlessTarget = TargetScene.SpawnObject<CGameObject>();
		CasterFx->SetTargetObject( TransformlessTarget, LocalOffset );
		// Transform 無しオブジェクト指定時の記録点列。
		const TArray<TArray<FDrawRibbonData::FPoint>> TransformlessStrips = ReadStrips( RecordPlayback( CasterFx->GetPlayback() ) );
		C.Check( !CasterFx->TryGetTargetPosition( ResolvedTarget ) && TransformlessStrips.Num() == 2 && NearPoint( TransformlessStrips[0].Last(), FallbackEnd ), "CTransformComponent を持たない対象オブジェクトでも安全にフォールバック終点へ戻る" );

		// モード切替と nullptr 解除を確認するための生存中ターゲット。
		const TSharedPtr<CGameObject> SecondTarget = TargetScene.SpawnObject<CGameObject>();
		SecondTarget->AddComponent<CTransformComponent>()->SetLocalPosition( FVector3( 5.0f, 0.0f, -2.0f ) );
		CasterFx->SetTargetObject( SecondTarget, FVector3( 1.0f, 0.0f, 0.0f ) );
		C.Check( CasterFx->TryGetTargetPosition( ResolvedTarget ) && Near3( ResolvedTarget, FVector3( 6.0f, 0.0f, -2.0f ) ), "別の有効な対象オブジェクトへ再接続できる" );
		CasterFx->SetTargetObject( nullptr );
		// nullptr 指定後の記録点列。
		const TArray<TArray<FDrawRibbonData::FPoint>> NullTargetStrips = ReadStrips( RecordPlayback( CasterFx->GetPlayback() ) );
		C.Check( !CasterFx->GetTargetObject().IsValid() && !CasterFx->TryGetTargetPosition( ResolvedTarget ) && NullTargetStrips.Num() == 2 && NearPoint( NullTargetStrips[0].Last(), FallbackEnd ), "SetTargetObject( nullptr ) で追従が解除されフォールバックへ戻る" );

		// オブジェクト追従モードから直接ワールド座標モードへ切り替える。
		CasterFx->SetTargetObject( SecondTarget, FVector3( 1.0f, 0.0f, 0.0f ) );
		// 直接指定するワールド終点座標。
		const FVector3 ExplicitTarget( -3.0f, 4.0f, 2.0f );
		CasterFx->SetTargetPosition( ExplicitTarget );
		// 直接座標切替後の記録点列。
		const TArray<TArray<FDrawRibbonData::FPoint>> ExplicitStrips = ReadStrips( RecordPlayback( CasterFx->GetPlayback() ) );
		C.Check( !CasterFx->GetTargetObject().IsValid() && CasterFx->GetTargetOffset() == FVector3::Zero && CasterFx->TryGetTargetPosition( ResolvedTarget ) && Near3( ResolvedTarget, ExplicitTarget ) && ExplicitStrips.Num() == 2 && NearPoint( ExplicitStrips[0].Last(), ExplicitTarget ), "SetTargetPosition で対象オブジェクト追従を解除して直接ワールド座標モードへ切り替わる" );

		// Play 再実行後も直接指定した終点が維持され、初回更新から反映される。
		C.Check( CasterFx->Play() && CasterFx->GetPlayback().IsValid(), "直接座標モードのまま Play を再実行できる" );
		if ( !CasterFx->GetPlayback().IsValid() ) return;
		CasterFx->GetPlayback()->Update( 0.1f );
		// Play 再実行後の記録点列。
		const TArray<TArray<FDrawRibbonData::FPoint>> ReplayedStrips = ReadStrips( RecordPlayback( CasterFx->GetPlayback() ) );
		C.Check( CasterFx->TryGetTargetPosition( ResolvedTarget ) && Near3( ResolvedTarget, ExplicitTarget ) && ReplayedStrips.Num() == 2 && NearPoint( ReplayedStrips[0].Last(), ExplicitTarget ), "Play 再実行後も直接座標の動的終点設定が維持される" );

		CasterFx->ClearTarget();
		// ClearTarget 呼出後の記録点列。
		const TArray<TArray<FDrawRibbonData::FPoint>> ClearedComponentStrips = ReadStrips( RecordPlayback( CasterFx->GetPlayback() ) );
		C.Check( !CasterFx->TryGetTargetPosition( ResolvedTarget ) && ClearedComponentStrips.Num() == 2 && NearPoint( ClearedComponentStrips[0].Last(), FallbackEnd ), "ClearTarget で直接座標指定も解除され定義側のフォールバック終点へ戻る" );
	}
}

// 定義から再生・記録、保存したプリセットの複製までを既存の窓口で確認する。
void NWorldTest::RunEffectPathPlaybackTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir )
{
	using namespace NEffectTest;
	C.Group = "effect-path-playback";
	Assets.SetRootDirectory( Dir );
	// 動かず一度だけ出る、形の確認用の雷。
	FParticleEmitterDefinition Lightning = QuietEmitter();
	Lightning.Path.Kind = FEffectPathDefinition::EKind::Lightning;
	Lightning.Path.Branches = 4;
	Lightning.Path.End = FVector3( 0.0f, 3.0f, 0.0f );
	Lightning.Bursts.Add( Burst( 0.0f, 1 ) );
	Lightning.StartSize = Lightning.EndSize = { 0.1f, 0.1f };
	// 原点以外と回転・拡大を組み合わせ、終点の座標空間を確認する。
	const FTransform Where( FVector3( 1.0f, 2.0f, 0.0f ), FQuaternion::FromAxisAngle( FVector3::Forward, 1.5707963268f ), FVector3( 2.0f, 2.0f, 2.0f ) );
	// 保存する定義と、実行時の再生指定。
	const TSharedPtr<FParticleEffectDefinition> Definition = MakeEffect( { Lightning }, 0.0f, true, 7 );
	FEffectPlayParams Params;
	Params.Seed = 123;
	// アセット管理・描画ライブラリの詳細を知らない共通の再生窓口。
	const TSharedPtr<IEffectPlayback> Playback = NEffect::CreateParticlePlayback( Definition, &Assets, Params, Where );
	C.Check( Playback.IsValid(), "分岐雷は既存の再生窓口から作れる" );
	if ( !Playback.IsValid() ) return;
	Playback->Update( 0.125f );
	// 同じ状態を二度記録した結果。
	const FDrawBuffer First = RecordPlayback( Playback );
	const FDrawBuffer Again = RecordPlayback( Playback );
	const TArray<TArray<FDrawRibbonData::FPoint>> Strips = ReadStrips( First );
	C.Check( First.Words == Again.Words && Playback->GetNumParticles() == 1, "記録だけでは時刻や粒子数を変えず、命令を再現できる" );
	C.Check( Strips.Num() == ( Lightning.Path.Branches + 1 ) * 2, "雷の主幹とすべての枝に、淡い外側と細い芯を記録する" );
	if ( Strips.Num() > 0 && !Strips[0].IsEmpty() )
	{
		C.Check( NearPoint( Strips[0][0], Where.Position ) && NearPoint( Strips[0].Last(), FVector3( -5.0f, 2.0f, 0.0f ) ), "雷の両端は再生の位置・回転・拡大を反映する" );
		C.Check( Strips[0][0].Width > 0.5f && Strips[0][0].Width < 0.7f && Strips[0].Last().U >= 6.0f, "幅と模様の距離にも再生の拡大を反映する" );
	}
	// 定義のシードよりも、明示した再生シードを優先する。
	Params.Seed = 456;
	const TSharedPtr<IEffectPlayback> DifferentSeed = NEffect::CreateParticlePlayback( Definition, &Assets, Params, Where );
	DifferentSeed->Update( 0.125f );
	C.Check( First.Words != RecordPlayback( DifferentSeed ).Words, "再生指定の Seed を変えると雷の途中の形が変わる" );
	Playback->Stop( true );
	C.Check( RecordPlayback( Playback ).Words.empty() && Playback->IsFinished(), "停止時に雷の描画を残さない" );
	// 通常の透明合成では、外側は暗い色を描くのでなく不透明度を下げる。
	Lightning.Blend = EParticleBlend::Alpha;
	Lightning.StartColor = Lightning.EndColor = FColor32( 100, 180, 240, 200 );
	const TSharedPtr<IEffectPlayback> AlphaBolt = NEffect::CreateParticlePlayback( MakeEffect( { Lightning } ), &Assets, FEffectPlayParams(), FTransform::Identity );
	AlphaBolt->Update( 0.1f );
	const TArray<TArray<FDrawRibbonData::FPoint>> AlphaStrips = ReadStrips( RecordPlayback( AlphaBolt ) );
	C.Check( AlphaStrips.Num() == 10 && ( AlphaStrips[0][0].Color & 0xFFFFFFu ) == ( AlphaStrips[5][0].Color & 0xFFFFFFu ) && ( AlphaStrips[0][0].Color >> 24 ) < ( AlphaStrips[5][0].Color >> 24 ), "透明な雷の外側は同じ色の不透明度を下げ、暗い太枠にしない" );
	// 静止指定の雷は、先に生まれた雷が消えても途中の形を変えない。
	Lightning.Blend = EParticleBlend::Add;
	Lightning.Path.Branches = 0;
	Lightning.Path.RefreshHz = 0.0f;
	Lightning.Lifetime = { 1.0f, 1.0f };
	Lightning.Bursts.Add( Burst( 0.25f, 1 ) );
	const TSharedPtr<IEffectPlayback> StableBolts = NEffect::CreateParticlePlayback( MakeEffect( { Lightning } ), &Assets, FEffectPlayParams(), FTransform::Identity );
	StableBolts->Update( 0.125f );
	StableBolts->SetTransform( FTransform( FVector3( 2.0f, 0.0f, 0.0f ) ) );
	StableBolts->Update( 0.125f );
	const TArray<TArray<FDrawRibbonData::FPoint>> TwoBolts = ReadStrips( RecordPlayback( StableBolts ) );
	C.Check( TwoBolts.Num() == 4 && TwoBolts[0].Num() > 8 && NMath::Abs( ( TwoBolts[0][8].Pos[0] - TwoBolts[2][8].Pos[0] ) + 2.0f ) > 0.001f, "別々に生まれた雷は、生成番号に応じて異なる途中の形を持つ" );
	StableBolts->Update( 0.9f );
	const TArray<TArray<FDrawRibbonData::FPoint>> OneBolt = ReadStrips( RecordPlayback( StableBolts ) );
	C.Check( TwoBolts.Num() == 4 && OneBolt.Num() == 2 && TwoBolts[2].Num() == OneBolt[0].Num() && std::memcmp( TwoBolts[2].GetData(), OneBolt[0].GetData(), static_cast<usize>( OneBolt[0].Num() ) * sizeof( FDrawRibbonData::FPoint ) ) == 0, "ほかの雷が消えて配列が詰まっても、静止した雷の形を保つ" );
	// 発生元が周回しても、生きている雷自身の時間は巻き戻さない。
	Lightning.Path.RefreshHz = 12.0f;
	Lightning.Lifetime = { 2.0f, 2.0f };
	Lightning.Bursts.SetNum( 1 );
	const TSharedPtr<IEffectPlayback> LoopingBolt = NEffect::CreateParticlePlayback( MakeEffect( { Lightning }, 0.5f, true ), &Assets, FEffectPlayParams(), FTransform::Identity );
	LoopingBolt->Update( 0.01f );
	LoopingBolt->Update( 0.489f );
	const TArray<TArray<FDrawRibbonData::FPoint>> BeforeLoop = ReadStrips( RecordPlayback( LoopingBolt ) );
	LoopingBolt->Update( 0.002f );
	const TArray<TArray<FDrawRibbonData::FPoint>> AfterLoop = ReadStrips( RecordPlayback( LoopingBolt ) );
	// 周回前後の主幹の最大移動量。
	float32 LoopJump = 1000.0f;
	if ( BeforeLoop.Num() >= 1 && AfterLoop.Num() >= 1 && BeforeLoop[0].Num() == AfterLoop[0].Num() )
	{
		LoopJump = 0.0f;
		for ( int32 Index = 0; Index < BeforeLoop[0].Num(); ++Index )
		{
			// 同じ雷の同じ点を比較する。
			const FVector3 Before( BeforeLoop[0][Index].Pos[0], BeforeLoop[0][Index].Pos[1], BeforeLoop[0][Index].Pos[2] );
			const FVector3 After( AfterLoop[0][Index].Pos[0], AfterLoop[0][Index].Pos[1], AfterLoop[0][Index].Pos[2] );
			LoopJump = NMath::Max( LoopJump, ( After - Before ).Length() );
		}
	}
	C.Check( LoopJump < 0.03f, "発生元の周回境界でも、生存している雷の形を不連続に戻さない（最大変化 %.5f）", LoopJump );

	// 移動した発生元から静止粒子を出し、その軌跡を結ぶ帯。
	FParticleEmitterDefinition Ribbon = QuietEmitter();
	Ribbon.Path.Kind = FEffectPathDefinition::EKind::Ribbon;
	Ribbon.Path.BreakDistance = 2.0f;
	Ribbon.Rate = 10.0f;
	const TSharedPtr<FParticleEffectDefinition> RibbonDefinition = MakeEffect( { Ribbon } );
	const TSharedPtr<IEffectPlayback> Trail = NEffect::CreateParticlePlayback( RibbonDefinition, &Assets, FEffectPlayParams(), FTransform::Identity );
	for ( int32 Index = 0; Index < 3; ++Index )
	{
		Trail->SetTransform( FTransform( FVector3( static_cast<float32>( Index ), 0.0f, 0.0f ) ) );
		Trail->Update( 0.1f );
	}
	// 軌跡の幅・模様・並びを、別々の粒子の四角形ではなく一本の帯として確認する。
	const TArray<TArray<FDrawRibbonData::FPoint>> RibbonStrips = ReadStrips( RecordPlayback( Trail ) );
	C.Check( RibbonStrips.Num() == 1 && RibbonStrips[0].Num() == 3 && NearPoint( RibbonStrips[0][0], FVector3::Zero ) && NearPoint( RibbonStrips[0][2], FVector3( 2.0f, 0.0f, 0.0f ) ) && RibbonStrips[0][2].U == 2.0f, "移動の軌跡を生まれた順に、距離が連続した帯として記録する" );
	Trail->SetTransform( FTransform( FVector3( 20.0f, 0.0f, 0.0f ) ) );
	Trail->Update( 0.1f );
	Trail->SetTransform( FTransform( FVector3( 21.0f, 0.0f, 0.0f ) ) );
	Trail->Update( 0.1f );
	// 瞬間移動の区間をつながず、二つの帯に分ける。
	const TArray<TArray<FDrawRibbonData::FPoint>> Split = ReadStrips( RecordPlayback( Trail ) );
	C.Check( Split.Num() == 2 && Split[0].Num() == 3 && Split[1].Num() == 2 && NearPoint( Split[1][0], FVector3( 20.0f, 0.0f, 0.0f ) ), "瞬間移動で長い帯を引き伸ばさず、連続した区間だけを描く" );
	Trail->Stop( true );
	C.Check( RecordPlayback( Trail ).Words.empty(), "帯も停止後には命令を残さない" );
	// 長い帯を複数の命令へ分けても、模様の位相を途中で戻さない。
	Ribbon.MaxParticles = 2000;
	Ribbon.Lifetime = { 200.0f, 200.0f };
	const TSharedPtr<IEffectPlayback> LongTrail = NEffect::CreateParticlePlayback( MakeEffect( { Ribbon } ), &Assets, FEffectPlayParams(), FTransform::Identity );
	for ( int32 Index = 0; Index < 1050; ++Index )
	{
		LongTrail->SetTransform( FTransform( FVector3( Index * 0.013f, 0.0f, 0.0f ) ) );
		LongTrail->Update( 0.1f );
	}
	// 複数命令へ分割された軌跡の記録。
	const FDrawBuffer LongTrailBuffer = RecordPlayback( LongTrail );
	const TArray<TArray<FDrawRibbonData::FPoint>> Chunks = ReadStrips( LongTrailBuffer );
	// 各命令のghost区間指定。
	const TArray<int32> ChunkJoinFlags = ReadJoinFlags( LongTrailBuffer );
	C.Check( Chunks.Num() == 2 && ChunkJoinFlags.Num() == 2 && ChunkJoinFlags[0] == FDrawRibbonData::NextPoint && ChunkJoinFlags[1] == FDrawRibbonData::PreviousPoint && Chunks[0].Num() == 1024 && Chunks[1][1].U == Chunks[0].Last( 1 ).U && Chunks[1][1].U > 13.0f && Chunks[1][0].U == Chunks[0].Last( 2 ).U && Chunks[1][2].U == Chunks[0].Last().U, "命令の上限で帯を分割しても、共有点・UV・接線用の隣接点を保ち、ghost区間を描かない指定を記録する" );

	// このテストだけが所有する、複製元のファイル。
	const FString PresetPath = "editor-path/ribbon_preset.effect.json";
	C.Check( Assets.SaveEffect( PresetPath, *RibbonDefinition ), "複製元の経路定義を保存する" );
	// 未保存の文書を、プリセットの選択でも黙って捨てない。
	FEffectEditorState Editor;
	NDebugUI::NewEffect( Editor );
	const NlohmannJson OldDocument = Editor.Document.Get().ToJson();
	C.Check( !NEffectEditorUI::RequestAction( Editor, FEffectEditorState::EPending::Preset, PresetPath, nullptr ) && Editor.Document.Get().ToJson() == OldDocument, "未保存の文書でプリセットを選んだときも、確認が済むまで内容を保つ" );
	C.Check( NEffectEditorUI::CompletePendingAction( Editor, false, nullptr ) && Editor.FilePath.IsEmpty() && Editor.Document.IsDirty() && Editor.Document.Get().Emitters[0].Path.Kind == FEffectPathDefinition::EKind::Ribbon && Editor.SaveAsPath != PresetPath && Editor.bPreviewMotion, "プリセットは原本への保存先を持たない未保存のコピーとして開く" );
	NDebugUI::UpdateEffectEditor( Editor, 0.1f );
	NDebugUI::UpdateEffectEditor( Editor, 0.1f );
	// 明示的な移動プレビューで、軌跡に必要な発生元の動きを与える。
	const FParticlePlayback* Moving = dynamic_cast<const FParticlePlayback*>( Editor.Playback.Get() );
	C.Check( Moving != nullptr && Moving->GetSimulation().Transform.Position.LengthSquared() > 0.01f, "移動プレビューが再生窓口の位置へ反映される" );
	NDebugUI::RestartEffectPreview( Editor );
	C.Check( Editor.PreviewMotionTime == 0.0f && !Editor.Playback.IsValid(), "再スタートで移動プレビューの時刻も巻き戻す" );
	C.Check( NDebugUI::OpenEffectFile( Editor, PresetPath ) && !Editor.bPreviewMotion && !Editor.Document.IsDirty(), "通常の開く操作は保存済み定義を読み、移動プレビューを自動では有効にしない" );
	NDebugUI::ReleaseEffectEditor( Editor );
	// 保存前のコピーを編集しても、原本は変わらない。
	const TSharedPtr<CEffectAsset> Original = Assets.LoadEffect( PresetPath );
	C.Check( Original.IsValid() && Original->GetDefinition().ToJson() == RibbonDefinition->ToJson(), "プリセットのコピーやプレビューが原本のファイルを書き換えない" );

	TestDynamicTargetPlayback( C, Assets );
	TestEffectComponentTarget( C, Assets );
}
