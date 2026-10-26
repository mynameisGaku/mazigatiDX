// SPDX-License-Identifier: NOASSERTION
#include "WorldSelfTestSupport.h"
#include "WorldEffectSelfTestSupport.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "Library/Core/Interfaces/Render/DrawMaterialRibbon.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/DebugUI/EffectEditor.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Effect/ParticleEffectBackend.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Json/JsonObject.h"

namespace
{
	// 材質Ribbonの固定設定と所有点列を一緒に持つPlayback出力。
	struct FTreeRibbonCommand
	{
		// 画像・mask・point数・全材質設定。
		FDrawMaterialRibbonData Header;
		// command bufferから独立したRibbon点。
		TArray<FDrawMaterialRibbonData::FPoint> Points;
	};

	// Playbackが記録したMaterialRibbon命令を長さ確認後に所有コピーする。
	TArray<FTreeRibbonCommand> ReadTreeRibbons( const FDrawBuffer& Buffer )
	{
		// command順の材質Ribbon結果。
		TArray<FTreeRibbonCommand> Result;
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type != EDrawCommand::MaterialRibbon3D || Command.NumDataWords * 4 < static_cast<int32>( sizeof( FDrawMaterialRibbonData ) ) ) return;
			// 固定headerと点数を読み、可変部を触る前に範囲を確認する。
			const FDrawMaterialRibbonData Header = Command.Read<FDrawMaterialRibbonData>();
			if ( Header.Ribbon.NumPoints < 2 || Header.Ribbon.NumPoints > FDrawRibbonData::MaxPoints ) return;
			// 必要なheader+全点のbyte数。
			const usize RequiredBytes = sizeof( Header ) + static_cast<usize>( Header.Ribbon.NumPoints ) * sizeof( FDrawMaterialRibbonData::FPoint );
			if ( RequiredBytes > static_cast<usize>( Command.NumDataWords ) * 4 ) return;
			// 一命令の所有コピー。
			FTreeRibbonCommand Item;
			Item.Header = Header;
			Item.Points.SetNum( Header.Ribbon.NumPoints );
			std::memcpy( Item.Points.GetData(), reinterpret_cast<const uint8*>( Command.Data ) + sizeof( Header ), static_cast<usize>( Item.Points.Num() ) * sizeof( FDrawMaterialRibbonData::FPoint ) );
			Result.Add( MoveTemp( Item ) );
		} );
		return Result;
	}

	// 旧Ribbon命令の固定headerと可変点列が実際に存在する数を集計する。
	void CountLegacyRibbonPayload( const FDrawBuffer& Buffer, int32& OutCommands, int32& OutPoints )
	{
		OutCommands = 0;
		OutPoints = 0;
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type != EDrawCommand::Ribbon3D || Command.NumDataWords * 4 < static_cast<int32>( sizeof( FDrawRibbonData ) ) ) return;
		// 旧Ribbon headerを可変点数の検査へ使う。
		const FDrawRibbonData Header = Command.Read<FDrawRibbonData>();
		if ( Header.NumPoints < 2 || Header.NumPoints > FDrawRibbonData::MaxPoints ) return;
		// headerと全pointがcommand領域へ収まるかを確認する。
		const usize RequiredBytes = sizeof( Header ) + static_cast<usize>( Header.NumPoints ) * sizeof( FDrawRibbonData::FPoint );
			if ( RequiredBytes > static_cast<usize>( Command.NumDataWords ) * 4 ) return;
			++OutCommands;
			OutPoints += Header.NumPoints;
		} );
	}

	// Tree材質命令に先行するblend命令のモードを読み取る。
	bool ReadTreeBlendMode( const FDrawBuffer& Buffer, EDrawBlend& OutMode )
	{
		bool bFound = false;
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type != EDrawCommand::Blend || Command.NumDataWords * 4 < static_cast<int32>( sizeof( FDrawBlendData ) ) ) return;
			OutMode = static_cast<EDrawBlend>( Command.Read<FDrawBlendData>().Mode );
			bFound = true;
		} );
		return bFound;
	}

	// Playbackを更新せず記録だけ行い、独立した命令bufferを返す。
	FDrawBuffer RecordTreePlayback( const TSharedPtr<IEffectPlayback>& Playback )
	{
		// この記録だけが所有するbuffer。
		FDrawBuffer Buffer;
		// particleが使う既存の記録API。
		FDrawContext Draw( Buffer );
		if ( Playback.IsValid() ) Playback->Record( Draw );
		return Buffer;
	}

	// 実シミュレーションで枝を作るTree Lightning emitterを構成する。
	FParticleEmitterDefinition MakeTreeEmitter( bool bMaterial )
	{
		// 基本 emission・motionを既定化し、burst一回だけを使う。
		FParticleEmitterDefinition Emitter = NEffectTest::QuietEmitter();
		Emitter.Path.Kind = FEffectPathDefinition::EKind::TreeLightning;
		Emitter.Path.End = FVector3( 0.0f, 3.0f, 0.0f );
		Emitter.Path.Segments = 12;
		Emitter.Path.Branches = 2;
		Emitter.Path.Jitter = 0.2f;
		Emitter.Path.BranchLength = 0.55f;
		Emitter.Path.Tree.Depth = 2;
		Emitter.Path.Tree.ChildBranches = 2;
		Emitter.Path.Tree.BranchAngleDegrees = 35.0f;
		Emitter.Path.Tree.GrowthSeconds = 0.4f;
		Emitter.Path.Tree.BranchDelaySeconds = 0.02f;
		Emitter.Path.Tree.StrokeCount = 1;
		Emitter.Path.Tree.StrokeInterval = 2.0f;
		Emitter.Path.Tree.StrokeSeconds = 1.5f;
		Emitter.StartSize = Emitter.EndSize = { 0.18f, 0.18f };
		Emitter.Lifetime = { 2.0f, 2.0f };
		Emitter.Speed = { 0.0f, 0.0f };
		Emitter.StartColor = Emitter.EndColor = FColor32( 220, 240, 255, 255 );
		Emitter.Bursts.Add( NEffectTest::Burst( 0.0f, 1 ) );
		Emitter.Material.bEnabled = bMaterial;
		Emitter.Material.bDissolve = false;
		Emitter.Material.Emission.Keys.Add( { 0.0f, 1.0f } );
		Emitter.Material.Emission.Keys.Add( { 1.0f, 1.5f } );
		Emitter.Material.Dissolve.Keys.Add( { 0.0f, 0.2f } );
		Emitter.Material.Dissolve.Keys.Add( { 1.0f, 0.8f } );
		Emitter.Material.UVScroll = FVector2( 0.25f, -0.125f );
		return Emitter;
	}

	// 保存した定義をpreset-copy操作で開き、専用名へ別保存した後に両方をdiskから読む。
	bool TestTreePresetCopyAndFreshRead( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& OriginalPath, const FParticleEffectDefinition& Source )
	{
		// 元presetを先に保存し、保存成功後にAssetManager経由で開く。
		const bool bSavedSource = Assets.SaveEffect( OriginalPath, Source );
		C.Check( bSavedSource, "Tree Lightningの元定義をtest sandboxへ保存する" );
		if ( !bSavedSource ) return false;
		// 保存済み定義を実際のEditor preset-copy操作へ渡す状態。
		FEffectEditorState Editor;
		const bool bCopied = NDebugUI::NewEffectFromPreset( Editor, OriginalPath );
		C.Check( bCopied && Editor.FilePath.IsEmpty() && !Editor.SaveAsPath.IsEmpty(), "presetを独立した未保存文書として開く" );
		if ( !bCopied || Editor.Document.Get().Emitters.IsEmpty() ) return false;
		// copyだけのTree設定を変更し、Save As操作で別assetを作る。
		Editor.Document.BeginEdit().Emitters[0].Path.Tree.Depth = 1;
		Editor.Document.EndEditSession();
		const FString CopyPath = OriginalPath.Left( OriginalPath.Len() - 12 ) + "_copy.effect.json";
		C.Check( Editor.SaveAsPath == CopyPath, "preset copyは原本と別のSave As名を提案する" );
		const bool bSavedCopy = NDebugUI::SaveEffectFile( Editor, CopyPath );
		C.Check( bSavedCopy, "編集したTree presetをSave Asで別名保存する" );
		if ( !bSavedCopy ) return false;
		// cacheを通さずdisk上の定義を復元する領域。
		FParticleEffectDefinition FreshOriginal;
		// Save Asされたcopyをdiskから復元する領域。
		FParticleEffectDefinition FreshCopy;
		// 原本JSONを直接読むreader。
		NJson::CJsonObject OriginalJson;
		// copy JSONを直接読むreader。
		NJson::CJsonObject CopyJson;
		// sandbox内原本の絶対path。
		const TOptional<FString> OriginalFullPath = Assets.ResolvePath( OriginalPath );
		// Save Asされたcopyの絶対path。
		const TOptional<FString> CopyFullPath = Assets.ResolvePath( CopyPath );
		// 原本をdiskから直接読み、effect定義へ解析する結果。
		const bool bReadOriginal = OriginalFullPath.IsSet() && OriginalJson.LoadFromFile( OriginalFullPath.GetValue() ) && FParticleEffectDefinition::FromJson( OriginalJson.GetRoot(), FreshOriginal );
		// copyをdiskから直接読み、effect定義へ解析する結果。
		const bool bReadCopy = CopyFullPath.IsSet() && CopyJson.LoadFromFile( CopyFullPath.GetValue() ) && FParticleEffectDefinition::FromJson( CopyJson.GetRoot(), FreshCopy );
		// 変更をDepthだけに限定した複製の期待値。
		FParticleEffectDefinition ExpectedCopy = Source;
		if ( ExpectedCopy.Emitters.IsValidIndex( 0 ) ) ExpectedCopy.Emitters[0].Path.Tree.Depth = 1;
		const bool bFreshMatch = bReadOriginal && bReadCopy && FreshOriginal.ToJson() == Source.ToJson() && FreshCopy.ToJson() == ExpectedCopy.ToJson();
		C.Check( bFreshMatch, "AssetManager cacheを介さないdisk readで原本維持とcopyのDepth変更を確認する" );
		NDebugUI::ReleaseEffectEditor( Editor );
		return bFreshMatch;
	}
}

// Tree Lightningの再生から材質・従来経路、固定形状、上限とpresetを確認する。
void NWorldTest::RunEffectTreeLightningPlaybackTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir )
{
	using namespace NEffectTest;
	C.Group = "effect-tree-lightning-playback";
	Assets.SetRootDirectory( Dir );
	// sandbox内の専用assetでpresetコピー、編集、別名保存、fresh readを通す。
	const FParticleEmitterDefinition CopySourceEmitter = MakeTreeEmitter( true );
	const TSharedPtr<FParticleEffectDefinition> CopySource = MakeEffect( { CopySourceEmitter }, 0.0f, true, 521 );
	const FString OriginalPath = "Saved/TreeLightningPlayback/tree-lightning.effect.json";
	const bool bFreshAssets = CopySource.IsValid() && TestTreePresetCopyAndFreshRead( C, Assets, OriginalPath, *CopySource );
	if ( !bFreshAssets ) return;
	// Save As後にAssetManagerから再取得した定義だけを再生へ渡す。
	// Editorが提案したSave As先を再読用にも使う。
	const FString CopyPath = OriginalPath.Left( OriginalPath.Len() - 12 ) + "_copy.effect.json";
	const TSharedPtr<CEffectAsset> OriginalDisk = Assets.LoadEffect( OriginalPath );
	const TSharedPtr<CEffectAsset> CopyDisk = Assets.LoadEffect( CopyPath );
	C.Check( OriginalDisk.IsValid() && CopyDisk.IsValid(), "preset原本とSave As copyを再生用にdiskから読む" );
	if ( !OriginalDisk.IsValid() || !CopyDisk.IsValid() ) return;
	// 材質付きTree Lightningを実simulationで再生する。
	// diskから読んだpreset原本のTree emitter。
	const FParticleEmitterDefinition Tree = OriginalDisk->GetDefinition().Emitters[0];
	// 再生中にアセット再読が起きても不変な定義の所有コピー。
	const TSharedPtr<FParticleEffectDefinition> Definition = MakeShared<FParticleEffectDefinition>( OriginalDisk->GetDefinition() );
	FEffectPlayParams Params;
	Params.Seed = 918;
	TSharedPtr<IEffectPlayback> Playback = NEffect::CreateParticlePlayback( Definition, &Assets, Params, FTransform::Identity, "tree-lightning-playback" );
	C.Check( Playback.IsValid(), "Tree Lightningを既存Playback窓口から作成する" );
	if ( !Playback.IsValid() ) return;
	Playback->Update( 0.01f );
	Playback->Update( 0.1f );
	const FDrawBuffer EarlyBuffer = RecordTreePlayback( Playback );
	const FDrawBuffer EarlyAgainBuffer = RecordTreePlayback( Playback );
	const TArray<FTreeRibbonCommand> Early = ReadTreeRibbons( EarlyBuffer );
	const TArray<FTreeRibbonCommand> EarlyAgain = ReadTreeRibbons( EarlyAgainBuffer );
	C.Check( !Early.IsEmpty() && EarlyBuffer.Words == EarlyAgainBuffer.Words, "実particleから成長中の材質Tree帯を記録し、再記録で命令を再現する" );
	C.Check( Early.Num() == EarlyAgain.Num() && !Early.IsEmpty() && Early[0].Points[0].Path.Pos[1] == EarlyAgain[0].Points[0].Path.Pos[1], "同一Ageの再記録は先端位置と命令数を変えない" );
	if ( !Early.IsEmpty() )
	{
		const FTreeRibbonCommand& First = Early[0];
		// Age .1秒、寿命2秒における独立な線形消失期待値。
		// Age .1秒、寿命2秒における独立な線形消失期待値。
		const float32 ExpectedDissolve = 0.2f + ( 0.8f - 0.2f ) * ( 0.1f / 2.0f );
		C.Check( First.Header.Material.bDissolve == 0 && First.Header.Ribbon.Texture >= 0 && First.Points.Num() >= 2, "材質設定とTexture付き点列が所有payloadへ記録される" );
		C.Check( NMath::Abs( First.Points[0].Dissolve - ExpectedDissolve ) < 1.0e-4f && NMath::Abs( First.Points[0].Emission - ( 1.0f + 0.5f * ( 0.1f / 2.0f ) ) ) < 1.0e-4f && NMath::Abs( First.Points[0].UVOffset[0] - 0.025f ) < 1.0e-4f && NMath::Abs( First.Points[0].UVOffset[1] + 0.0125f ) < 1.0e-4f, "点ごとの消失・発光・UV移動は粒子Ageから独立した値として記録される" );
	}
	// Alpha/Addのblend命令と材質Ribbonの生色・alphaを個別に比較する。
	for ( int32 BlendIndex = 0; BlendIndex < 2; ++BlendIndex )
	{
		// 一つの合成方式だけ指定するTree emitter。
		FParticleEmitterDefinition BlendEmitter = Tree;
		BlendEmitter.Path.Tree.GrowthSeconds = 0.0f;
		BlendEmitter.Path.Tree.BranchDelaySeconds = 0.0f;
		BlendEmitter.Path.Tree.StrokeCount = 1;
		BlendEmitter.Path.Tree.StrokeInterval = 1.0f;
		BlendEmitter.Path.Tree.StrokeSeconds = 0.5f;
		BlendEmitter.Blend = BlendIndex == 0 ? EParticleBlend::Alpha : EParticleBlend::Add;
		TSharedPtr<IEffectPlayback> BlendPlayback = NEffect::CreateParticlePlayback( MakeEffect( { BlendEmitter }, 0.0f, true, 524 ), &Assets, Params, FTransform::Identity, BlendIndex == 0 ? "tree-lightning-alpha" : "tree-lightning-add" );
		C.Check( BlendPlayback.IsValid(), "Tree Alpha/Add Playbackを作成する" );
		if ( !BlendPlayback.IsValid() ) continue;
		BlendPlayback->Update( 0.01f );
		BlendPlayback->Update( 0.1f );
		const FDrawBuffer BlendBuffer = RecordTreePlayback( BlendPlayback );
		const TArray<FTreeRibbonCommand> BlendRibbons = ReadTreeRibbons( BlendBuffer );
		EDrawBlend BlendMode = EDrawBlend::Alpha;
		const bool bFoundBlend = ReadTreeBlendMode( BlendBuffer, BlendMode );
		// 最初の材質帯の生点色。帯が空でも不正参照しない。
		const uint32 FirstColor = !BlendRibbons.IsEmpty() && !BlendRibbons[0].Points.IsEmpty() ? BlendRibbons[0].Points[0].Path.Color : 0u;
		// FDrawRibbonDataの下位24bitへ格納されるRGB期待値。
		// FDrawRibbonDataの下位24bitへ格納されるRGB期待値。
		const uint32 ExpectedRgb = 0x00FFF0DCu;
		C.Check( bFoundBlend && BlendMode == ( BlendIndex == 0 ? EDrawBlend::Alpha : EDrawBlend::Add ) && FirstColor != 0u && ( FirstColor & 0x00FFFFFFu ) == ExpectedRgb && ( FirstColor >> 24 ) > 0, "Alpha/Addは対応modeを送り、材質帯の生RGBとalphaを保持する (mode=%d count=%d color=%08X)", static_cast<int32>( BlendMode ), BlendRibbons.Num(), FirstColor );
	}
	// 成長専用fixtureはseed依存の完成時刻に十分な長さのpulseを使う。
	Playback->Update( 0.2f );
	const TArray<FTreeRibbonCommand> Mid = ReadTreeRibbons( RecordTreePlayback( Playback ) );
	Playback->Update( 0.4f );
	const TArray<FTreeRibbonCommand> Full = ReadTreeRibbons( RecordTreePlayback( Playback ) );
	C.Check( !Mid.IsEmpty() && !Full.IsEmpty() && Full.Num() >= Mid.Num(), "再生Ageを進めると親から枝が追加され幹は残る" );
	// .7秒、寿命2秒の全枝で共通となる点材質の独立期待値。
	const float32 ExpectedFullDissolve = 0.2f + ( 0.8f - 0.2f ) * 0.35f;
	// .7秒の発光倍率。
	const float32 ExpectedFullEmission = 1.0f + 0.5f * 0.35f;
	// .7秒のsource UV移動量。
	const float32 ExpectedFullUvX = 0.25f * 0.7f;
	// .7秒の縦UV移動量。
	const float32 ExpectedFullUvY = -0.125f * 0.7f;
	// 全strokeの点材質がroot particleのAgeで評価されたか。
	bool bFullPointValuesMatch = !Full.IsEmpty();
	// 材質付き幹・枝strokeの全14命令がpayloadを持つか。
	bool bFullPayloadPresent = Full.Num() == 14;
	for ( const FTreeRibbonCommand& Ribbon : Full )
	{
		bFullPayloadPresent = bFullPayloadPresent && !Ribbon.Points.IsEmpty();
		for ( const FDrawMaterialRibbonData::FPoint& Point : Ribbon.Points )
		{
			bFullPointValuesMatch = bFullPointValuesMatch && NMath::Abs( Point.Dissolve - ExpectedFullDissolve ) < 1.0e-4f && NMath::Abs( Point.Emission - ExpectedFullEmission ) < 1.0e-4f && NMath::Abs( Point.UVOffset[0] - ExpectedFullUvX ) < 1.0e-4f && NMath::Abs( Point.UVOffset[1] - ExpectedFullUvY ) < 1.0e-4f;
		}
	}
	C.Check( bFullPayloadPresent, "完成形は14本の幹・枝material strokeが点payloadを持つ (commands=%d)", Full.Num() );
	C.Check( bFullPointValuesMatch, "完成形の全stroke点へ元particle Age由来のDissolve・Emission・UV移動を転送する" );
	// loopを一周した後の粒子seedが形状を変える独立Playback fixture。
	FParticleEmitterDefinition LoopShapeEmitter = MakeTreeEmitter( true );
	LoopShapeEmitter.Path.Segments = 24;
	LoopShapeEmitter.Path.Branches = 2;
	LoopShapeEmitter.Path.Jitter = 0.2f;
	LoopShapeEmitter.Path.BranchLength = 0.55f;
	LoopShapeEmitter.Path.Tree.Depth = 2;
	LoopShapeEmitter.Path.Tree.ChildBranches = 2;
	LoopShapeEmitter.Path.Tree.GrowthSeconds = 0.0f;
	LoopShapeEmitter.Path.Tree.BranchDelaySeconds = 0.0f;
	LoopShapeEmitter.Path.Tree.StrokeCount = 1;
	LoopShapeEmitter.Path.Tree.StrokeInterval = 1.0f;
	LoopShapeEmitter.Path.Tree.StrokeSeconds = 0.125f;
	LoopShapeEmitter.Lifetime = { 0.375f, 0.375f };
	LoopShapeEmitter.Speed = { 0.0f, 0.0f };
	LoopShapeEmitter.Gravity = FVector3::Zero;
	LoopShapeEmitter.Shape = EParticleShape::Cone;
	LoopShapeEmitter.ConeAngle = 0.0f;
	LoopShapeEmitter.ShapeRadius = 0.0f;
	LoopShapeEmitter.MaxParticles = 1;
	LoopShapeEmitter.Bursts.Reset();
	LoopShapeEmitter.Bursts.Add( Burst( 0.0f, 1 ) );
	// 半秒ごとに一粒子だけ作る再生窓。
	const TSharedPtr<FParticleEffectDefinition> LoopShapeDefinition = MakeEffect( { LoopShapeEmitter }, 0.5f, true, 1199u );
	// serial間の形状差を記録するPlayback。
	TSharedPtr<IEffectPlayback> LoopShapePlayback = NEffect::CreateParticlePlayback( LoopShapeDefinition, &Assets, Params, FTransform::Identity, "tree-lightning-loop-shape" );
	C.Check( LoopShapePlayback.IsValid(), "形状変化確認用の短いloop Playbackを作成する" );
	if ( LoopShapePlayback.IsValid() )
	{
		LoopShapePlayback->Update( 0.01f );
		// 実装playbackの内部simulationから発生粒子のserialを読む。
		const FParticleSimulation& SerialZeroSimulation = static_cast<const FParticlePlayback*>( LoopShapePlayback.Get() )->GetSimulation();
		// 初回のloop前は一粒子だけが生存しserialが0であることを安全に確認する。
		const bool bFirstSerialIsZero = LoopShapePlayback->GetNumParticles() == 1 && SerialZeroSimulation.Emitters.Num() == 1 && SerialZeroSimulation.Emitters[0].Particles.Num() == 1 && SerialZeroSimulation.Emitters[0].Particles[0].Serial == 0u;
		C.Check( bFirstSerialIsZero, "loop形状fixtureの最初の生存particleはserial 0" );
		// serial 0のAge 0 geometryと同一Age再記録。
		const FDrawBuffer SerialZeroBuffer = RecordTreePlayback( LoopShapePlayback );
		// 同一状態を再記録した比較用buffer。
		const FDrawBuffer SerialZeroAgainBuffer = RecordTreePlayback( LoopShapePlayback );
		// serial 0の材質帯を所有コピーする。
		const TArray<FTreeRibbonCommand> SerialZero = ReadTreeRibbons( SerialZeroBuffer );
		// serial 1の材質帯を後で所有コピーする領域。
		TArray<FTreeRibbonCommand> SerialOne;
		// 二度の同じ粒子記録が同じ完全命令列になることを確認する。
		C.Check( SerialZero.Num() == 14 && SerialZeroBuffer.Words == SerialZeroAgainBuffer.Words, "最初の放電は14本の非空帯を持ち、同Age再記録はbyte一致する (commands=%d)", SerialZero.Num() );
		// serial 0の各命令が空でないpoint列を持つか。
		bool bSerialZeroHasPoints = SerialZero.Num() == 14;
		// 各材質命令のpoint列を走査する。
		for ( const FTreeRibbonCommand& Ribbon : SerialZero ) bSerialZeroHasPoints = bSerialZeroHasPoints && !Ribbon.Points.IsEmpty();
		C.Check( bSerialZeroHasPoints, "serial 0の幹・枝14命令が全て点列を所有する" );
		// 半秒のloop boundaryで次particle serialが生成される。
		LoopShapePlayback->Update( 0.5f );
		// loop後に作られた粒子の内部serialを再取得する。
		const FParticleSimulation& SerialOneSimulation = static_cast<const FParticlePlayback*>( LoopShapePlayback.Get() )->GetSimulation();
		// 前粒子の寿命後に一粒子だけが生存しserialが1になったか。
		const bool bSecondSerialIsOne = LoopShapePlayback->GetNumParticles() == 1 && SerialOneSimulation.Emitters.Num() == 1 && SerialOneSimulation.Emitters[0].Particles.Num() == 1 && SerialOneSimulation.Emitters[0].Particles[0].Serial == 1u;
		C.Check( bSecondSerialIsOne, "次のloopで新しく生まれた生存particleはserial 1" );
		// 一周後に新serial粒子だけが残る記録。
		const FDrawBuffer SerialOneBuffer = RecordTreePlayback( LoopShapePlayback );
		SerialOne = ReadTreeRibbons( SerialOneBuffer );
		// serial 1の各命令が空でないpoint列を持つか。
		bool bSerialOneHasPoints = SerialOne.Num() == 14;
		// 各材質命令のpoint列を走査する。
		for ( const FTreeRibbonCommand& Ribbon : SerialOne ) bSerialOneHasPoints = bSerialOneHasPoints && !Ribbon.Points.IsEmpty();
		C.Check( bSerialOneHasPoints, "loop後のserial 1も14本の非空幹・枝帯を所有する (commands=%d)", SerialOne.Num() );
		if ( bFirstSerialIsZero && bSecondSerialIsOne && bSerialZeroHasPoints && bSerialOneHasPoints )
		{
			// trunkを区間数が最大のstrokeとして選ぶ。
			int32 TrunkIndex = 0;
			for ( int32 Index = 1; Index < SerialZero.Num(); ++Index ) if ( SerialZero[Index].Points.Num() > SerialZero[TrunkIndex].Points.Num() ) TrunkIndex = Index;
			// serial 0とserial 1で比較する主幹点列。
			const TArray<FDrawMaterialRibbonData::FPoint>& TrunkZero = SerialZero[TrunkIndex].Points;
			// 対応するserial 1の主幹点列。
			const TArray<FDrawMaterialRibbonData::FPoint>& TrunkOne = SerialOne[TrunkIndex].Points;
			// 始点から終点までの固定chord。
			const FVector3 Chord( TrunkZero[TrunkZero.Num() - 1].Path.Pos[0] - TrunkZero[0].Path.Pos[0], TrunkZero[TrunkZero.Num() - 1].Path.Pos[1] - TrunkZero[0].Path.Pos[1], TrunkZero[TrunkZero.Num() - 1].Path.Pos[2] - TrunkZero[0].Path.Pos[2] );
			// chordの二乗長さ。
			const float32 ChordSquared = Chord.x * Chord.x + Chord.y * Chord.y + Chord.z * Chord.z;
			// chordに対して測った最大の横方向ずれ。
			float32 MaxLateralDifference = 0.0f;
			if ( TrunkZero.Num() == TrunkOne.Num() && ChordSquared > 0.0f )
			{
				for ( int32 PointIndex = 0; PointIndex < TrunkZero.Num(); ++PointIndex )
				{
					// 対応点間の位置差。
					const FVector3 Delta( TrunkOne[PointIndex].Path.Pos[0] - TrunkZero[PointIndex].Path.Pos[0], TrunkOne[PointIndex].Path.Pos[1] - TrunkZero[PointIndex].Path.Pos[1], TrunkOne[PointIndex].Path.Pos[2] - TrunkZero[PointIndex].Path.Pos[2] );
					// 差分からchord方向の移動だけを除いた横ずれ。
					const float32 Along = ( Delta.x * Chord.x + Delta.y * Chord.y + Delta.z * Chord.z ) / ChordSquared;
					const FVector3 Lateral( Delta.x - Chord.x * Along, Delta.y - Chord.y * Along, Delta.z - Chord.z * Along );
					MaxLateralDifference = NMath::Max( MaxLateralDifference, Lateral.Length() );
				}
			}
			// 同じ始終点を保ちつつseed由来の幹屈曲が十分変わるか。
			const bool bSameEndpoints = TrunkZero.Num() == TrunkOne.Num() && NMath::Abs( TrunkZero[0].Path.Pos[0] - TrunkOne[0].Path.Pos[0] ) < 1.0e-5f && NMath::Abs( TrunkZero[0].Path.Pos[1] - TrunkOne[0].Path.Pos[1] ) < 1.0e-5f && NMath::Abs( TrunkZero[0].Path.Pos[2] - TrunkOne[0].Path.Pos[2] ) < 1.0e-5f && NMath::Abs( TrunkZero[TrunkZero.Num() - 1].Path.Pos[0] - TrunkOne[TrunkOne.Num() - 1].Path.Pos[0] ) < 1.0e-5f && NMath::Abs( TrunkZero[TrunkZero.Num() - 1].Path.Pos[1] - TrunkOne[TrunkOne.Num() - 1].Path.Pos[1] ) < 1.0e-5f && NMath::Abs( TrunkZero[TrunkZero.Num() - 1].Path.Pos[2] - TrunkOne[TrunkOne.Num() - 1].Path.Pos[2] ) < 1.0e-5f;
			C.Check( bSameEndpoints && MaxLateralDifference > Chord.Length() * 0.01f, "loopでserialが変わると端点を保ったままseed依存の主幹形状が変わる (lateral=%.4f chord=%.4f)", MaxLateralDifference, Chord.Length() );
		}
	}
	// Save As copy側の変更をfresh readした再生形状へ反映する。
	TSharedPtr<FParticleEffectDefinition> CopyDefinition = MakeShared<FParticleEffectDefinition>( CopyDisk->GetDefinition() );
	// Depthを浅くしたcopy定義を試し再生する窓口。
	TSharedPtr<IEffectPlayback> CopyPlayback = NEffect::CreateParticlePlayback( CopyDefinition, &Assets, Params, FTransform::Identity, "tree-lightning-copy-playback" );
	C.Check( CopyPlayback.IsValid() && CopyDefinition->Emitters[0].Path.Tree.Depth == 1, "fresh copyのDepth変更をPlayback定義へ渡す" );
	if ( !CopyPlayback.IsValid() ) return;
	CopyPlayback->Update( 0.01f );
	CopyPlayback->Update( 0.7f );
	const TArray<FTreeRibbonCommand> CopyRibbons = ReadTreeRibbons( RecordTreePlayback( CopyPlayback ) );
	C.Check( !CopyRibbons.IsEmpty() && CopyRibbons.Num() < Full.Num(), "copyで保存した浅い枝設定から非空だが少ない帯を再生する (original=%d copy=%d)", Full.Num(), CopyRibbons.Num() );
	// GrowthEndの揺れに依存しない、別の固定pulse fixtureで表示窓を調べる。
	FParticleEmitterDefinition PulseEmitter = MakeTreeEmitter( true );
	PulseEmitter.Path.Tree.GrowthSeconds = 0.0f;
	PulseEmitter.Path.Tree.BranchDelaySeconds = 0.0f;
	PulseEmitter.Path.Tree.StrokeCount = 2;
	PulseEmitter.Path.Tree.StrokeInterval = 1.0f;
	PulseEmitter.Path.Tree.StrokeSeconds = 0.5f;
	TSharedPtr<IEffectPlayback> PulsePlayback = NEffect::CreateParticlePlayback( MakeEffect( { PulseEmitter }, 0.0f, true, 522 ), &Assets, Params, FTransform::Identity, "tree-lightning-pulse" );
	C.Check( PulsePlayback.IsValid(), "固定pulse確認用Playbackを作成する" );
	if ( !PulsePlayback.IsValid() ) return;
	PulsePlayback->Update( 0.01f );
	PulsePlayback->Update( 0.24f );
	const TArray<FTreeRibbonCommand> PulseOn = ReadTreeRibbons( RecordTreePlayback( PulsePlayback ) );
	PulsePlayback->Update( 0.5f );
	const TArray<FTreeRibbonCommand> PulseGap = ReadTreeRibbons( RecordTreePlayback( PulsePlayback ) );
	C.Check( !PulseOn.IsEmpty() && PulseGap.IsEmpty(), "GrowthEnd 0の別fixtureはpulse中だけ所有帯を記録する (on=%d gap=%d)", PulseOn.Num(), PulseGap.Num() );
	// 材質を切った同じkindでも既存Ribbon命令を使う。
	const FParticleEmitterDefinition LegacyTree = MakeTreeEmitter( false );
	TSharedPtr<IEffectPlayback> PlainPlayback = NEffect::CreateParticlePlayback( MakeEffect( { LegacyTree }, 0.0f, true, 521 ), &Assets, Params, FTransform::Identity, "tree-lightning-plain" );
	C.Check( PlainPlayback.IsValid(), "材質OFF Tree Playbackを作成する" );
	if ( !PlainPlayback.IsValid() ) return;
	PlainPlayback->Update( 0.01f );
	PlainPlayback->Update( 0.1f );
	const FDrawBuffer PlainBuffer = RecordTreePlayback( PlainPlayback );
	int32 PlainRibbonCount = 0;
	int32 PlainRibbonPoints = 0;
	CountLegacyRibbonPayload( PlainBuffer, PlainRibbonCount, PlainRibbonPoints );
	C.Check( PlainRibbonCount > 0 && PlainRibbonPoints >= PlainRibbonCount * 2, "材質OFFのTreeは実点列を持つ従来Ribbon命令を使う" );
	// 旧LightningはTree設定を持っていても従来形状・命令byte列を変えない。
	FParticleEmitterDefinition OldLightning = MakeTreeEmitter( false );
	OldLightning.Path.Kind = FEffectPathDefinition::EKind::Lightning;
	OldLightning.Path.Tree = FEffectLightningTreeDefinition();
	TSharedPtr<IEffectPlayback> OldA = NEffect::CreateParticlePlayback( MakeEffect( { OldLightning }, 0.0f, true, 777 ), &Assets, Params, FTransform::Identity, "legacy-lightning-a" );
	OldLightning.Path.Tree.Depth = 1;
	TSharedPtr<IEffectPlayback> OldB = NEffect::CreateParticlePlayback( MakeEffect( { OldLightning }, 0.0f, true, 777 ), &Assets, Params, FTransform::Identity, "legacy-lightning-b" );
	C.Check( OldA.IsValid() && OldB.IsValid(), "旧Lightning比較用Playbackを2つ作成する" );
	if ( !OldA.IsValid() || !OldB.IsValid() ) return;
	OldA->Update( 0.01f ); OldB->Update( 0.01f );
	const FDrawBuffer OldBufferA = RecordTreePlayback( OldA );
	const FDrawBuffer OldBufferB = RecordTreePlayback( OldB );
	int32 OldRibbonCommands = 0;
	int32 OldRibbonPoints = 0;
	CountLegacyRibbonPayload( OldBufferA, OldRibbonCommands, OldRibbonPoints );
	C.Check( OldRibbonCommands > 0 && OldRibbonPoints >= OldRibbonCommands * 2 && OldBufferA.Words == OldBufferB.Words, "旧Lightningは非空point payloadを保ち、未使用のTree設定でbyte列を変えない" );
	// 複数emitterを合算した上限4粒子を確認する定義。
	FParticleEmitterDefinition BudgetA = MakeTreeEmitter( true );
	BudgetA.Path.Tree.Depth = 0; BudgetA.Path.Tree.ChildBranches = 0; BudgetA.Path.Tree.GrowthSeconds = 0.0f; BudgetA.Path.Tree.BranchDelaySeconds = 0.0f; BudgetA.Path.Tree.StrokeCount = 1; BudgetA.Path.Tree.StrokeInterval = 1.0f; BudgetA.Path.Tree.StrokeSeconds = 0.5f; BudgetA.Path.Branches = 0; BudgetA.Bursts[0].Count = 3;
	FParticleEmitterDefinition BudgetB = BudgetA; BudgetB.Bursts[0].Count = 3;
	TSharedPtr<IEffectPlayback> BudgetPlayback = NEffect::CreateParticlePlayback( MakeEffect( { BudgetA, BudgetB } ), &Assets, Params, FTransform::Identity, "tree-lightning-budget" );
	C.Check( BudgetPlayback.IsValid(), "粒子上限確認用Playbackを作成する" );
	if ( !BudgetPlayback.IsValid() ) return;
	BudgetPlayback->Update( 0.01f );
	const FDrawBuffer BudgetBuffer = RecordTreePlayback( BudgetPlayback );
	const TArray<FTreeRibbonCommand> BudgetCommands = ReadTreeRibbons( BudgetBuffer );
	bool bBudgetPointsOwned = true;
	for ( const FTreeRibbonCommand& Command : BudgetCommands ) bBudgetPointsOwned = bBudgetPointsOwned && Command.Points.Num() >= 2;
	C.Check( BudgetPlayback->GetNumParticles() == 6 && BudgetCommands.Num() == 8 && bBudgetPointsOwned, "2 emitterの6粒子から先頭4粒子だけが各2本の非空所有帯を出す (commands=%d)", BudgetCommands.Num() );
	// 先頭4粒子が透明なら、後続2粒子へ描画枠を繰り上げない。
	FParticleEmitterDefinition TransparentBudget = BudgetA;
	TransparentBudget.StartColor.a = TransparentBudget.EndColor.a = 0;
	TransparentBudget.Bursts[0].Count = 4;
	FParticleEmitterDefinition VisibleAfterBudget = BudgetB;
	VisibleAfterBudget.Bursts[0].Count = 2;
	TSharedPtr<IEffectPlayback> NoPromotion = NEffect::CreateParticlePlayback( MakeEffect( { TransparentBudget, VisibleAfterBudget } ), &Assets, Params, FTransform::Identity, "tree-lightning-fixed-budget" );
	C.Check( NoPromotion.IsValid(), "固定描画枠のalpha確認用Playbackを作成する" );
	if ( !NoPromotion.IsValid() ) return;
	NoPromotion->Update( 0.01f );
	const TArray<FTreeRibbonCommand> NoPromotionCommands = ReadTreeRibbons( RecordTreePlayback( NoPromotion ) );
	C.Check( NoPromotion->GetNumParticles() == 6 && NoPromotionCommands.IsEmpty(), "先頭4件のalpha skipで5・6番目が描画枠へ繰り上がらない" );
}
