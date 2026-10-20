// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"

#include "Library/DebugUI/EffectEditor.h"
#include "Library/DebugUI/EffectEditorInternal.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Render/DrawContext.h"

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
			Result.Add( std::move( Points ) );
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
	C.Check( TwoBolts.Num() == 4 && TwoBolts[0].Num() > 8 && std::fabs( ( TwoBolts[0][8].Pos[0] - TwoBolts[2][8].Pos[0] ) + 2.0f ) > 0.001f, "別々に生まれた雷は、生成番号に応じて異なる途中の形を持つ" );
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
			LoopJump = std::max( LoopJump, ( After - Before ).Length() );
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
}
