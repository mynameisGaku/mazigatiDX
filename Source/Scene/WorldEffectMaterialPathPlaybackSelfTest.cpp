// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/DebugUI/EffectEditor.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Effect/ParticleEffectBackend.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Json/JsonObject.h"

namespace
{
	// 記録された材質付き帯と、その所有コピーを一緒に持つ。
	struct FMaterialRibbonCommand
	{
		// 帯全体で共有する画像・材質の設定。
		FDrawMaterialRibbonData Header;
		// 命令bufferの寿命から独立した点列。
		TArray<FDrawMaterialRibbonData::FPoint> Points;
	};

	// Playbackのbufferから材質付き帯命令だけを安全に複写する。
	TArray<FMaterialRibbonCommand> ReadMaterialRibbons( const FDrawBuffer& Buffer )
	{
		// 読み取った材質付き帯。
		TArray<FMaterialRibbonCommand> Result;
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type != EDrawCommand::MaterialRibbon3D || Command.NumDataWords * 4 < static_cast<int32>( sizeof( FDrawMaterialRibbonData ) ) ) return;
			// 可変長点列の固定header。
			const FDrawMaterialRibbonData Header = Command.Read<FDrawMaterialRibbonData>();
			if ( Header.Ribbon.NumPoints < 2 || Header.Ribbon.NumPoints > FDrawRibbonData::MaxPoints ) return;
			// 安全に読むためのpayload長。
			const usize Required = sizeof( Header ) + static_cast<usize>( Header.Ribbon.NumPoints ) * sizeof( FDrawMaterialRibbonData::FPoint );
			if ( Required > static_cast<usize>( Command.NumDataWords ) * 4 ) return;
			// 一命令を所有する結果。
			FMaterialRibbonCommand Item;
			Item.Header = Header;
			Item.Points.SetNum( Header.Ribbon.NumPoints );
			std::memcpy( Item.Points.GetData(), reinterpret_cast<const uint8*>( Command.Data ) + sizeof( Header ), Item.Points.Num() * sizeof( FDrawMaterialRibbonData::FPoint ) );
			Result.Add( MoveTemp( Item ) );
		} );
		return Result;
	}

	// 再生命令だけを作り、更新やGPU描画を行わない。
	FDrawBuffer Record( const TSharedPtr<IEffectPlayback>& Playback )
	{
		// この記録結果の所有buffer。
		FDrawBuffer Buffer;
		// 描画命令の記録窓口。
		FDrawContext Draw( Buffer );
		if ( Playback.IsValid() ) Playback->Record( Draw );
		return Buffer;
	}

	// 曲線を寿命割合の端点だけで定義する。
	void SetLinearCurve( FEffectScalarCurve& Curve, float32 Start, float32 End )
	{
		Curve = FEffectScalarCurve();
		Curve.Interpolation = FEffectScalarCurve::EInterpolation::Linear;
		Curve.Keys.Add( { 0.0f, Start } );
		Curve.Keys.Add( { 1.0f, End } );
	}
}

// 材質Ribbon/Lightningの点ごとの時間、分割、旧命令互換、保存と再生を確認する。
void NWorldTest::RunEffectMaterialPathPlaybackTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir )
{
	using namespace NEffectTest;
	C.Group = "effect-material-path-playback";
	Assets.SetRootDirectory( Dir );
	// 帯の点ごとに異なる寿命を持たせ、材質評価とUVの連続値を確認する。
	FParticleEmitterDefinition Ribbon = QuietEmitter();
	Ribbon.Path.Kind = FEffectPathDefinition::EKind::Ribbon;
	Ribbon.Path.BreakDistance = 2.0f;
	Ribbon.Path.UVRepeat = 1.25f;
	Ribbon.Rate = 10.0f;
	Ribbon.Lifetime = { 2.0f, 2.0f };
	Ribbon.StartColor = Ribbon.EndColor = FColor32( 90, 150, 210, 180 );
	Ribbon.Material.bEnabled = true;
	Ribbon.Material.bDissolve = true;
	Ribbon.Material.MaskTexture = "material-path-unique-mask.tga";
	Ribbon.Material.UVScale = FVector2( 1.5f, 0.75f );
	Ribbon.Material.UVScroll = FVector2( -4.0f, 0.5f );
	SetLinearCurve( Ribbon.Material.Dissolve, 0.0f, 1.0f );
	SetLinearCurve( Ribbon.Material.Emission, 1.0f, 5.0f );
	// このテストだけが読み込むmask fixture。
	TArray<uint8> MaskImage = NEffect::MakeSoftDotImage();
	// ファイル書き込みAPIへ渡す標準byte列。
	std::vector<uint8> MaskBytes;
	for ( const uint8 Byte : MaskImage ) MaskBytes.push_back( Byte );
	C.Check( NAssetTest::WriteFileBytes( Dir / Ribbon.Material.MaskTexture, MaskBytes ), "path材質mask fixtureをテストsandboxへ作る" );
	// 再生に共有保持させるmask。
	TSharedPtr<CTexture> MaskOwner = Assets.LoadTexture( Ribbon.Material.MaskTexture );
	// 再生終了時の解放を確認する弱参照。
	TWeakPtr<CTexture> MaskWeak = MaskOwner;
	// 命令へ渡す画像handle。
	const int32 MaskHandle = MaskOwner.IsValid() ? MaskOwner->GetHandle() : -1;
	// 再生開始時に複写する定義。
	const TSharedPtr<FParticleEffectDefinition> RibbonDefinition = MakeEffect( { Ribbon } );
	// 保存・open・copyを試すfixture path。
	const FString SavedPath = "material-path/material_ribbon_source.effect.json";
	C.Check( Assets.SaveEffect( SavedPath, *RibbonDefinition ), "材質Ribbon fixtureをJSONへ保存する" );
	// 保存内容を読み直した共有asset。
	const TSharedPtr<CEffectAsset> SavedAsset = Assets.LoadEffect( SavedPath );
	C.Check( SavedAsset.IsValid() && SavedAsset->GetDefinition().ComputeHash() == RibbonDefinition->ComputeHash(), "保存後に開いた材質Ribbonが元定義と一致する" );
	// 保存済み定義から独立copyを作るeditor状態。
	FEffectEditorState Editor;
	NDebugUI::NewEffect( Editor );
	C.Check( NDebugUI::NewEffectFromPreset( Editor, SavedPath ) && Editor.FilePath.IsEmpty() && Editor.Document.IsDirty(), "保存済み材質Ribbonを原本へ上書きしない編集copyとして開く" );
	if ( Editor.Document.Get().Emitters.IsValidIndex( 0 ) )
	{
		// 編集copyだけを変える値。
		FParticleEmitterDefinition Edited = Editor.Document.Get().Emitters[0];
		Edited.Material.Emission.Keys[1].Value = 4.125f;
		C.Check( NParticleEdit::SetEmitter( Editor.Document, 0, Edited ), "copy上のEmissionを編集履歴へ記録する" );
	}
	// 原本と異なる保存先。
	const FString CopyPath = "material-path/material_ribbon_copy.effect.json";
	C.Check( NDebugUI::SaveEffectFile( Editor, CopyPath ), "編集したRibbon copyを別pathへ保存する" );
	// 保存先の絶対path。
	const TOptional<FString> SavedFullPath = Assets.ResolvePath( SavedPath );
	// キャッシュを通さずdiskから読むJSON。
	NJson::CJsonObject FreshSavedJson;
	// diskから再構築した保存原本。
	FParticleEffectDefinition FreshSavedDefinition;
	// 保存直後のファイル内容を読む。
	const bool bFreshSavedRead = SavedFullPath.IsSet() && FreshSavedJson.LoadFromFile( SavedFullPath.GetValue() ) && FParticleEffectDefinition::FromJson( FreshSavedJson.GetRoot(), FreshSavedDefinition );
	C.Check( bFreshSavedRead && FreshSavedDefinition.ComputeHash() == RibbonDefinition->ComputeHash(), "保存した原本をdiskから再読込してhash一致を確認する" );
	// 編集済みcopyを開き直したasset。
	const TSharedPtr<CEffectAsset> CopyAsset = Assets.LoadEffect( CopyPath );
	C.Check( SavedAsset.IsValid() && SavedAsset->GetDefinition().ComputeHash() == RibbonDefinition->ComputeHash() && CopyAsset.IsValid() && CopyAsset->GetDefinition().Emitters[0].Material.Emission.Keys[1].Value == 4.125f, "別名保存後も原本を保ち材質曲線の精度を読み直す" );
	// 開き直したcopyから再生する設定。
	FParticleEmitterDefinition PlaybackRibbon = CopyAsset.IsValid() ? CopyAsset->GetDefinition().Emitters[0] : Ribbon;
	PlaybackRibbon.Rate = 10.0f;
	// 点列生成用の再生定義。
	const TSharedPtr<FParticleEffectDefinition> PlaybackDefinition = MakeEffect( { PlaybackRibbon } );
	// 帯材質を再生するowner。
	TSharedPtr<IEffectPlayback> Trail = NEffect::CreateParticlePlayback( PlaybackDefinition, &Assets, FEffectPlayParams(), FTransform::Identity, "material-path-ribbon" );
	for ( int32 Index = 0; Index < 4; ++Index )
	{
		Trail->SetTransform( FTransform( FVector3( static_cast<float32>( Index ), 0.0f, 0.0f ) ) );
		Trail->Update( 0.1f );
	}
	// 帯材質の再生命令。
	const FDrawBuffer RibbonBuffer = Record( Trail );
	// 可変長命令を所有コピー。
	const TArray<FMaterialRibbonCommand> RibbonCommands = ReadMaterialRibbons( RibbonBuffer );
	C.Check( RibbonCommands.Num() == 1 && RibbonCommands[0].Points.Num() == 4 && RibbonCommands[0].Header.Material.MaskTexture == MaskHandle, "有効なRibbonは専用材質命令でmask handleと4点を記録する" );
	if ( RibbonCommands.Num() == 1 && RibbonCommands[0].Points.Num() == 4 )
	{
		// 4点の材質Ribbon命令。
		const FMaterialRibbonCommand& Command = RibbonCommands[0];
		// 点ごとの寿命割合が異なるか。
		bool bAgesVary = false;
		// 点の曲線評価値が寿命割合と合うか。
		bool bCurveValuesMatch = true;
		// 負のスクロールphaseが剰余ジャンプせず連続するか。
		bool bScrollContinuous = true;
		for ( int32 Index = 0; Index < Command.Points.Num(); ++Index )
		{
			// 比較する一点。
			const FDrawMaterialRibbonData::FPoint& Point = Command.Points[Index];
			bAgesVary = bAgesVary || Point.Dissolve > 0.0f;
			bCurveValuesMatch = bCurveValuesMatch && NMath::IsFinite( Point.Dissolve ) && NMath::IsFinite( Point.Emission ) && NMath::Abs( Point.Emission - ( 1.0f + 3.125f * Point.Dissolve ) ) < 1.0e-4f;
			// 最初の点の秒齢を曲線値から戻す。
			const double ReferenceAge = static_cast<double>( Command.Points[0].Dissolve ) * 2.0;
			// signed fmod の共通基準位相。
			const double ExpectedBase = std::fmod( ReferenceAge * -4.0, 1.0 );
			// 点間の秒齢差だけを足す。
			const double ExpectedU = ExpectedBase + ( static_cast<double>( Point.Dissolve ) - Command.Points[0].Dissolve ) * 2.0 * -4.0;
			bScrollContinuous = bScrollContinuous && NMath::Abs( Point.UVOffset[0] - ExpectedU ) < 0.02f;
		}
		C.Check( bAgesVary && bCurveValuesMatch && Command.Points[0].Path.Color == 0xB4D2965Au, "材質点は点ごとの曲線値と元の未加工RGBAを記録する" );
		C.Check( bScrollContinuous && Command.Header.Ribbon.UVRepeat == Ribbon.Path.UVRepeat && Command.Header.Material.UVScale[0] == Ribbon.Material.UVScale.x && Command.Header.Material.UVScale[1] == Ribbon.Material.UVScale.y, "負のUV移動を経過秒差として連続記録し、距離Uと倍率を保つ" );
		// 一点目の秒齢。
		const double ReferenceAge = static_cast<double>( Command.Points[0].Dissolve ) * 2.0;
		// 負数を正規化しない共通基準。
		const double ExpectedSignedBase = std::fmod( ReferenceAge * -4.0, 1.0 );
		C.Check( ExpectedSignedBase < 0.0 && NMath::Abs( Command.Points[0].UVOffset[0] - ExpectedSignedBase ) < 0.02, "負のUV基準位相はsigned fmodの値をそのまま記録する" );
	}
	MaskOwner.Reset();
	C.Check( MaskWeak.IsValid(), "材質Ribbon再生中はmask画像をshared保持する" );
	// 1000点上限を越えた分割でも、共有ghost点と材質位相を揃える。
	FParticleEmitterDefinition LongRibbon = Ribbon;
	LongRibbon.Material.MaskTexture.Reset();
	LongRibbon.MaxParticles = 1200;
	LongRibbon.Lifetime = { 200.0f, 200.0f };
	LongRibbon.Rate = 10.0f;
	TSharedPtr<IEffectPlayback> LongTrail = NEffect::CreateParticlePlayback( MakeEffect( { LongRibbon } ), &Assets, FEffectPlayParams(), FTransform::Identity, "material-path-long-ribbon" );
	for ( int32 Index = 0; Index < 1030; ++Index )
	{
		LongTrail->SetTransform( FTransform( FVector3( Index * 0.013f, 0.0f, 0.0f ) ) );
		LongTrail->Update( 0.1f );
	}
	// 分割した材質帯の各命令。
	const TArray<FMaterialRibbonCommand> LongCommands = ReadMaterialRibbons( Record( LongTrail ) );
	C.Check( LongCommands.Num() == 2 && LongCommands[0].Header.Ribbon.JoinFlags == FDrawRibbonData::NextPoint && LongCommands[1].Header.Ribbon.JoinFlags == FDrawRibbonData::PreviousPoint && LongCommands[0].Points.Num() == 1024, "1024点を越える帯は前後ghostを含め2つの材質命令へ分割する" );
	if ( LongCommands.Num() == 2 && LongCommands[0].Points.Num() == 1024 && LongCommands[1].Points.Num() > 2 )
	{
		// 先行chunkの可視終点。
		const FDrawMaterialRibbonData::FPoint& End = LongCommands[0].Points[LongCommands[0].Points.Num() - 2];
		// 先行chunkの後側ghost。
		const FDrawMaterialRibbonData::FPoint& NextGhost = LongCommands[0].Points.Last();
		// 次chunkの共有visible endpoint。
		const FDrawMaterialRibbonData::FPoint& Shared = LongCommands[1].Points[1];
		// 次chunkの前側ghost。
		const FDrawMaterialRibbonData::FPoint& Previous = LongCommands[1].Points[0];
		// 先行chunkの同じ前側ghost。
		const FDrawMaterialRibbonData::FPoint& PreviousShared = LongCommands[0].Points[LongCommands[0].Points.Num() - 3];
		// 次chunkに複写された後側ghost。
		const FDrawMaterialRibbonData::FPoint& NextShared = LongCommands[1].Points[2];
		C.Check( std::memcmp( &End, &Shared, sizeof( End ) ) == 0 && std::memcmp( &Previous, &PreviousShared, sizeof( Previous ) ) == 0 && std::memcmp( &NextGhost, &NextShared, sizeof( NextGhost ) ) == 0, "chunk共有点と前後ghostの40byte全payloadが完全一致する" );
	}
	// 最終chunkだけで上限を越える直接定義でも、先行chunkを途中記録しない。
	// 最終chunkでだけUV上限を越える直接定義。
	FParticleEmitterDefinition LateOffsetRibbon = LongRibbon;
	LateOffsetRibbon.Material.UVScroll = FVector2( -9750.0f, 0.0f );
	// 後半で無効になる帯。
	TSharedPtr<IEffectPlayback> LateOffsetTrail = NEffect::CreateParticlePlayback( MakeEffect( { LateOffsetRibbon } ), &Assets, FEffectPlayParams(), FTransform::Identity, "material-path-late-offset" );
	for ( int32 Index = 0; Index < 1030; ++Index )
	{
		LateOffsetTrail->SetTransform( FTransform( FVector3( Index * 0.013f, 0.0f, 0.0f ) ) );
		LateOffsetTrail->Update( 0.1f );
	}
	C.Check( ReadMaterialRibbons( Record( LateOffsetTrail ) ).IsEmpty(), "全Stripを事前検証し後半UV offset超過時に先行chunkも記録しない" );
	// 秒齢が大きくても各点の差が短いRibbonは小さな位相差として保持する。
	// 長い秒齢でも粒子間の差が短い定義。
	FParticleEmitterDefinition LongAgeRibbon = Ribbon;
	LongAgeRibbon.MaxParticles = 4;
	LongAgeRibbon.Lifetime = { 30000.0f, 30000.0f };
	LongAgeRibbon.Rate = 10.0f;
	LongAgeRibbon.Material.UVScroll = FVector2( 0.3f, -0.2f );
	// 20000秒後の位相を確認する再生。
	TSharedPtr<IEffectPlayback> LongAgeTrail = NEffect::CreateParticlePlayback( MakeEffect( { LongAgeRibbon } ), &Assets, FEffectPlayParams(), FTransform::Identity, "material-path-long-age" );
	for ( int32 Index = 0; Index < 4; ++Index )
	{
		LongAgeTrail->SetTransform( FTransform( FVector3( Index * 0.1f, 0.0f, 0.0f ) ) );
		LongAgeTrail->Update( 0.1f );
	}
	LongAgeTrail->Update( 20000.0f );
	// 長い秒齢での連続位相。
	const TArray<FMaterialRibbonCommand> LongAgeCommands = ReadMaterialRibbons( Record( LongAgeTrail ) );
	// 再生から取り出した実際の粒子秒齢。
	TArray<NParticle::FDrawParticle> LongAgeParticles;
	if ( LongAgeTrail.IsValid() ) NParticle::CollectDrawParticles( static_cast<const FParticlePlayback*>( LongAgeTrail.Get() )->GetSimulation(), LongAgeParticles );
	// 実際の粒子順で比較できるか。
	bool bLongAgeOffsetMatches = LongAgeCommands.Num() == 1 && LongAgeCommands[0].Points.Num() == 4 && LongAgeParticles.Num() == 4;
	// 近接点の小さな移動差が残るか。
	bool bLongAgeHasPointPhaseDifference = false;
	if ( bLongAgeOffsetMatches )
	{
		// 同時刻帯の一命令。
		const FMaterialRibbonCommand& Command = LongAgeCommands[0];
		// 実際の先頭粒子秒齢。
		const double ReferenceAge = LongAgeParticles[0].Age;
		// 符号付きの共通横位相。
		const double BaseU = std::fmod( ReferenceAge * LongAgeRibbon.Material.UVScroll.x, 1.0 );
		// 符号付きの共通縦位相。
		const double BaseV = std::fmod( ReferenceAge * LongAgeRibbon.Material.UVScroll.y, 1.0 );
		for ( int32 Index = 0; Index < Command.Points.Num(); ++Index )
		{
			// 記録された一つの帯点。
			const FDrawMaterialRibbonData::FPoint& Point = Command.Points[Index];
			// 実測秒齢から計算した連続U。
			const double ExpectedU = BaseU + ( static_cast<double>( LongAgeParticles[Index].Age ) - ReferenceAge ) * LongAgeRibbon.Material.UVScroll.x;
			// 実測秒齢から計算した連続V。
			const double ExpectedV = BaseV + ( static_cast<double>( LongAgeParticles[Index].Age ) - ReferenceAge ) * LongAgeRibbon.Material.UVScroll.y;
			// 大きな絶対秒齢を小さい位相差として保持する。
			bLongAgeOffsetMatches = bLongAgeOffsetMatches && NMath::Abs( Point.UVOffset[0] - ExpectedU ) < 1.0e-3 && NMath::Abs( Point.UVOffset[1] - ExpectedV ) < 1.0e-3 && NMath::Abs( Point.UVOffset[0] ) < 0.2f && NMath::Abs( Point.UVOffset[1] ) < 0.2f;
		}
		bLongAgeHasPointPhaseDifference = NMath::Abs( Command.Points.Last().UVOffset[0] - Command.Points[0].UVOffset[0] ) > 0.02f || NMath::Abs( Command.Points.Last().UVOffset[1] - Command.Points[0].UVOffset[1] ) > 0.02f;
	}
	C.Check( bLongAgeOffsetMatches && bLongAgeHasPointPhaseDifference, "秒齢20000秒付近でも実際の近接粒子ageから求めたsigned fmod位相と差を保つ" );
	// 材質を外した場合は同じRibbon命令と同じ点列だけを記録する。
	FParticleEmitterDefinition LegacyRibbon = PlaybackRibbon;
	LegacyRibbon.Material = FEffectMaterialDefinition();
	TSharedPtr<IEffectPlayback> LegacyTrail = NEffect::CreateParticlePlayback( MakeEffect( { LegacyRibbon } ), &Assets, FEffectPlayParams(), FTransform::Identity, "material-path-legacy-ribbon" );
	// 無効のまま材質値だけを変えた定義。
	FParticleEmitterDefinition ChangedOffRibbon = LegacyRibbon;
	ChangedOffRibbon.Material.bDissolve = true;
	ChangedOffRibbon.Material.MaskTexture.Reset();
	ChangedOffRibbon.Material.UVScale = FVector2( 2.5f, 3.25f );
	ChangedOffRibbon.Material.UVScroll = FVector2( -7.5f, 5.125f );
	ChangedOffRibbon.Material.MaskScale = 4.5f;
	SetLinearCurve( ChangedOffRibbon.Material.Dissolve, 0.125f, 0.875f );
	SetLinearCurve( ChangedOffRibbon.Material.Emission, 2.25f, 7.75f );
	ChangedOffRibbon.Material.EdgeWidth = 0.7f;
	ChangedOffRibbon.Material.EdgeColor = FColor32( 12, 34, 56, 78 );
	ChangedOffRibbon.Material.EdgeEmission = 9.5f;
	// 無効材質値の影響がないか比べる再生。
	TSharedPtr<IEffectPlayback> ChangedOffTrail = NEffect::CreateParticlePlayback( MakeEffect( { ChangedOffRibbon } ), &Assets, FEffectPlayParams(), FTransform::Identity, "material-path-off-values" );
	for ( int32 Index = 0; Index < 4; ++Index )
	{
		// 両方の再生で共通の位置。
		const FTransform Position( FVector3( static_cast<float32>( Index ), 0.0f, 0.0f ) );
		LegacyTrail->SetTransform( Position );
		ChangedOffTrail->SetTransform( Position );
		LegacyTrail->Update( 0.1f );
		ChangedOffTrail->Update( 0.1f );
	}
	// 旧材質なしRibbon命令列。
	const FDrawBuffer LegacyRibbonBuffer = Record( LegacyTrail );
	// 無効材質値を変えたRibbon命令列。
	const FDrawBuffer ChangedOffRibbonBuffer = Record( ChangedOffTrail );
	// 従来の命令が使われたか。
	bool bOldRibbon = false;
	ForEachDrawCommand( FDrawSpan{ LegacyRibbonBuffer.Words.data(), static_cast<int32>( LegacyRibbonBuffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { bOldRibbon = bOldRibbon || Command.Type == EDrawCommand::Ribbon3D; } );
	C.Check( bOldRibbon && ReadMaterialRibbons( LegacyRibbonBuffer ).IsEmpty() && LegacyRibbonBuffer.Words == ChangedOffRibbonBuffer.Words && LegacyRibbonBuffer.Words != RibbonBuffer.Words, "材質無効時は各材質値を変えても旧Ribbon命令のbufferがbyte一致する" );
	// 同じ位置へ異なる寿命割合の粒子が来たとき、後から来た点の全見た目を使う。
	// 同位置の二粒子を試す材質設定。
	FParticleEmitterDefinition DedupRibbon = Ribbon;
	DedupRibbon.Rate = 10.0f;
	DedupRibbon.MaxParticles = 4;
	DedupRibbon.Lifetime = { 2.0f, 2.0f };
	DedupRibbon.StartSize = { 1.0f, 1.0f };
	DedupRibbon.EndSize = { 3.0f, 3.0f };
	DedupRibbon.StartColor = FColor32( 100, 80, 60, 200 );
	DedupRibbon.EndColor = FColor32( 20, 180, 240, 80 );
	DedupRibbon.Material.UVScroll = FVector2( 0.75f, -0.5f );
	SetLinearCurve( DedupRibbon.Material.Dissolve, 0.0f, 1.0f );
	SetLinearCurve( DedupRibbon.Material.Emission, 1.0f, 3.0f );
	// 同一点を更新する再生。
	TSharedPtr<IEffectPlayback> DedupTrail = NEffect::CreateParticlePlayback( MakeEffect( { DedupRibbon } ), &Assets, FEffectPlayParams(), FTransform::Identity, "material-path-dedup" );
	DedupTrail->SetTransform( FTransform( FVector3( 0.0f, 0.0f, 0.0f ) ) );
	DedupTrail->Update( 0.1f );
	DedupTrail->SetTransform( FTransform( FVector3( 0.0f, 0.0f, 0.0f ) ) );
	DedupTrail->Update( 0.1f );
	DedupTrail->SetTransform( FTransform( FVector3( 1.0f, 0.0f, 0.0f ) ) );
	DedupTrail->Update( 0.1f );
	// 再生に残る粒子の実値。
	TArray<NParticle::FDrawParticle> DedupParticles;
	NParticle::CollectDrawParticles( static_cast<const FParticlePlayback*>( DedupTrail.Get() )->GetSimulation(), DedupParticles );
	// 同一点統合後の描画点。
	const TArray<FMaterialRibbonCommand> DedupCommands = ReadMaterialRibbons( Record( DedupTrail ) );
	// 比較できる二点帯か。
	bool bDedupLatestValue = DedupCommands.Num() == 1 && DedupCommands[0].Points.Num() == 2 && DedupParticles.Num() == 3;
	if ( bDedupLatestValue )
	{
		// 後着点で更新された共有位置。
		const FDrawMaterialRibbonData::FPoint& MergedPoint = DedupCommands[0].Points[0];
		// 同位置へ最後に生まれた粒子。
		const NParticle::FDrawParticle& LatestParticle = DedupParticles[1];
		// 最新粒子のsigned秒齢phase。
		const double ExpectedOffsetU = std::fmod( static_cast<double>( LatestParticle.Age ) * DedupRibbon.Material.UVScroll.x, 1.0 );
		// 最新粒子のsigned秒齢phase。
		const double ExpectedOffsetV = std::fmod( static_cast<double>( LatestParticle.Age ) * DedupRibbon.Material.UVScroll.y, 1.0 );
		// 最新色のpacked RGBA。
		const uint32 ExpectedColor = static_cast<uint32>( LatestParticle.Color.r ) | ( static_cast<uint32>( LatestParticle.Color.g ) << 8 ) | ( static_cast<uint32>( LatestParticle.Color.b ) << 16 ) | ( static_cast<uint32>( LatestParticle.Color.a ) << 24 );
		bDedupLatestValue = NMath::Abs( MergedPoint.Dissolve - LatestParticle.NormalizedAge ) < 1.0e-4f && NMath::Abs( MergedPoint.Emission - DedupRibbon.Material.Emission.Evaluate( LatestParticle.NormalizedAge, 1.0f ) ) < 1.0e-4f && NMath::Abs( MergedPoint.UVOffset[0] - ExpectedOffsetU ) < 1.0e-4 && NMath::Abs( MergedPoint.UVOffset[1] - ExpectedOffsetV ) < 1.0e-4 && NMath::Abs( MergedPoint.Path.Width - LatestParticle.Size ) < 1.0e-4f && MergedPoint.Path.Color == ExpectedColor;
	}
	C.Check( bDedupLatestValue, "同位置を統合したRibbon点は最新粒子のDissolve・Emission・phase・幅・色へ更新する" );
	// Lightningの芯と外側は同じ粒子齢を使い、境界発光は外側だけへ渡す。
	FParticleEmitterDefinition Lightning = QuietEmitter();
	Lightning.Path.Kind = FEffectPathDefinition::EKind::Lightning;
	Lightning.Path.Branches = 3;
	Lightning.Path.End = FVector3( 0.0f, 3.0f, 0.0f );
	Lightning.Lifetime = { 2.0f, 2.0f };
	Lightning.Bursts.Add( Burst( 0.0f, 1 ) );
	Lightning.Material = Ribbon.Material;
	Lightning.Material.MaskTexture.Reset();
	Lightning.Material.UVScroll = FVector2( -0.2f, 0.3f );
	SetLinearCurve( Lightning.Material.Dissolve, 0.0f, 1.0f );
	SetLinearCurve( Lightning.Material.Emission, 2.0f, 6.0f );
	TSharedPtr<IEffectPlayback> Bolt = NEffect::CreateParticlePlayback( MakeEffect( { Lightning } ), &Assets, FEffectPlayParams(), FTransform::Identity, "material-path-lightning" );
	Bolt->Update( 0.01f );
	Bolt->Update( 0.4f );
	// 主幹・枝のcore/halo命令。
	const TArray<FMaterialRibbonCommand> BoltCommands = ReadMaterialRibbons( Record( Bolt ) );
	// 境界発光を持たない芯の命令。
	bool bCore = false;
	// 境界発光を持つ外側の命令。
	bool bHalo = false;
	// すべての雷strokeが同じ粒子齢か。
	bool bBoltAgesMatch = BoltCommands.Num() >= 2;
	for ( int32 Index = 0; Index < BoltCommands.Num(); ++Index )
	{
		// 主幹または枝の一描画。
		const FMaterialRibbonCommand& Command = BoltCommands[Index];
		bCore = bCore || Command.Header.Material.EdgeEmission == 0.0f;
		bHalo = bHalo || Command.Header.Material.EdgeEmission == Lightning.Material.EdgeEmission;
		for ( const FDrawMaterialRibbonData::FPoint& Point : Command.Points ) bBoltAgesMatch = bBoltAgesMatch && NMath::Abs( Point.Dissolve - 0.2f ) < 0.03f && NMath::Abs( Point.Emission - 2.8f ) < 0.2f && NMath::IsFinite( Point.UVOffset[0] ) && NMath::IsFinite( Point.UVOffset[1] );
	}
	C.Check( BoltCommands.Num() >= 8 && bCore && bHalo && bBoltAgesMatch, "Lightning主幹と枝の芯・外側は同じ寿命曲線を使い、境界発光だけ外側へ一度渡す" );
	// 同じstrip順でhaloとcoreを対応できるか。
	bool bHaloCoreMatch = BoltCommands.Num() >= 8 && BoltCommands.Num() % 2 == 0;
	for ( int32 Index = 0; bHaloCoreMatch && Index < BoltCommands.Num() / 2; ++Index )
	{
		// 外側のstroke。
		const FMaterialRibbonCommand& Halo = BoltCommands[Index];
		// 対応する芯のstroke。
		const FMaterialRibbonCommand& Core = BoltCommands[Index + BoltCommands.Num() / 2];
		bHaloCoreMatch = Halo.Points.Num() == Core.Points.Num() && Halo.Points.Num() > 0;
		for ( int32 PointIndex = 0; bHaloCoreMatch && PointIndex < Halo.Points.Num(); ++PointIndex )
		{
			// 外側の一点。
			const FDrawMaterialRibbonData::FPoint& HaloPoint = Halo.Points[PointIndex];
			// 対応する芯の一点。
			const FDrawMaterialRibbonData::FPoint& CorePoint = Core.Points[PointIndex];
			// 外側へ渡した生RGBと弱めたalpha。
			const uint32 HaloColor = HaloPoint.Path.Color;
			// 芯へ渡した元のRGBA。
			const uint32 CoreColor = CorePoint.Path.Color;
			// 旧外側幅に使うalpha倍率。
			const uint8 ExpectedHaloAlpha = static_cast<uint8>( ( CoreColor >> 24 ) * 0.18f );
			bHaloCoreMatch = HaloPoint.Path.Width == CorePoint.Path.Width * 3.0f && HaloPoint.Path.U == CorePoint.Path.U && HaloPoint.Dissolve == CorePoint.Dissolve && HaloPoint.Emission == CorePoint.Emission && HaloPoint.UVOffset[0] == CorePoint.UVOffset[0] && HaloPoint.UVOffset[1] == CorePoint.UVOffset[1] && ( HaloColor & 0x00FFFFFFu ) == ( CoreColor & 0x00FFFFFFu ) && static_cast<uint8>( HaloColor >> 24 ) == ExpectedHaloAlpha;
		}
	}
	C.Check( bHaloCoreMatch, "Lightningの対応するhalo/coreは幅3倍・同じ曲線とUV・RGB一致・alpha0.18倍を保つ" );
	NDebugUI::ReleaseEffectEditor( Editor );
	Trail.Reset();
	LegacyTrail.Reset();
	ChangedOffTrail.Reset();
	DedupTrail.Reset();
	LongTrail.Reset();
	LateOffsetTrail.Reset();
	LongAgeTrail.Reset();
	Bolt.Reset();
	MaskOwner.Reset();
	C.Check( !MaskWeak.IsValid(), "最後のRibbon再生を解放すると専用mask fixtureも解放する" );
}