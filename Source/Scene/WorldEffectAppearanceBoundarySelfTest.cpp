// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/DebugUI/EffectEditor.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Effect/ParticleEffectBackend.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Json/JsonObject.h"

namespace
{
	// 命令bufferの可変payloadを安全に所有して比較する帯。
	struct FRecordedRibbon
	{
		// 旧形式と材質形式に共通する帯header。
		FDrawRibbonData Header;
		// 描画点をbuffer寿命から独立して保持する。
		TArray<FDrawRibbonData::FPoint> Points;
		// 材質形式なら40byte全体も保持する。
		TArray<FDrawMaterialRibbonData::FPoint> MaterialPoints;
	};

	// 旧Ribbon命令と材質Ribbon命令を、共通の形状点として読み取る。
	TArray<FRecordedRibbon> ReadRibbons( const FDrawBuffer& Buffer )
	{
		// 読み取った帯命令。
		TArray<FRecordedRibbon> Result;
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type == EDrawCommand::MaterialRibbon3D )
			{
				if ( Command.NumDataWords * 4 < static_cast<int32>( sizeof( FDrawMaterialRibbonData ) ) ) return;
				// 材質付き帯のfixed header。
				const FDrawMaterialRibbonData MaterialHeader = Command.Read<FDrawMaterialRibbonData>();
				if ( MaterialHeader.Ribbon.NumPoints < 2 || MaterialHeader.Ribbon.NumPoints > FDrawRibbonData::MaxPoints ) return;
				// 必要なデータのbyte数。
				const usize Required = sizeof( MaterialHeader ) + static_cast<usize>( MaterialHeader.Ribbon.NumPoints ) * sizeof( FDrawMaterialRibbonData::FPoint );
				if ( Required > static_cast<usize>( Command.NumDataWords ) * 4 ) return;
				// 安全に読む一命令分の形状。
				FRecordedRibbon Ribbon;
				Ribbon.Header = MaterialHeader.Ribbon;
				Ribbon.Points.SetNum( MaterialHeader.Ribbon.NumPoints );
				Ribbon.MaterialPoints.SetNum( MaterialHeader.Ribbon.NumPoints );
				for ( int32 Index = 0; Index < Ribbon.Points.Num(); ++Index )
				{
					// 材質pointの先頭にある共通形状。
					const usize Offset = sizeof( MaterialHeader ) + static_cast<usize>( Index ) * sizeof( FDrawMaterialRibbonData::FPoint );
					// 命令payload内の一点を読む参照。
					const auto* Point = reinterpret_cast<const FDrawMaterialRibbonData::FPoint*>( reinterpret_cast<const uint8*>( Command.Data ) + Offset );
					Ribbon.MaterialPoints[Index] = *Point;
					Ribbon.Points[Index] = Point->Path;
				}
				Result.Add( MoveTemp( Ribbon ) );
				return;
			}
			if ( Command.Type != EDrawCommand::Ribbon3D || Command.NumDataWords * 4 < static_cast<int32>( sizeof( FDrawRibbonData ) ) ) return;
			// 旧形式の帯header。
			const FDrawRibbonData Header = Command.Read<FDrawRibbonData>();
			if ( Header.NumPoints < 2 || Header.NumPoints > FDrawRibbonData::MaxPoints ) return;
			// 旧形式の必要byte数。
			const usize Required = sizeof( Header ) + static_cast<usize>( Header.NumPoints ) * sizeof( FDrawRibbonData::FPoint );
			if ( Required > static_cast<usize>( Command.NumDataWords ) * 4 ) return;
			// 安全に読む一命令分の形状。
			FRecordedRibbon Ribbon;
			Ribbon.Header = Header;
			Ribbon.Points.SetNum( Header.NumPoints );
			std::memcpy( Ribbon.Points.GetData(), reinterpret_cast<const uint8*>( Command.Data ) + sizeof( Header ), static_cast<usize>( Ribbon.Points.Num() ) * sizeof( FDrawRibbonData::FPoint ) );
			Result.Add( MoveTemp( Ribbon ) );
		} );
		return Result;
	}

	// Playbackの記録済み描画命令を独立bufferへ複写する。
	FDrawBuffer RecordPlayback( const TSharedPtr<IEffectPlayback>& Playback )
	{
		// この記録結果の所有buffer。
		FDrawBuffer Buffer;
		// 描画命令の記録窓口。
		FDrawContext Draw( Buffer );
		if ( Playback.IsValid() ) Playback->Record( Draw );
		return Buffer;
	}

	// 2点の線形補間曲線を上書きする。
	void SetScalarCurve( FEffectScalarCurve& Curve, float32 Start, float32 End )
	{
		Curve = FEffectScalarCurve();
		Curve.Keys.Add( { 0.0f, Start } );
		Curve.Keys.Add( { 1.0f, End } );
	}

	// 指定した端点のRGBA線形曲線を上書きする。
	void SetColorCurve( FEffectColorCurve& Curve, const FColor& Start, const FColor& End )
	{
		Curve = FEffectColorCurve();
		Curve.Keys.Add( { 0.0f, Start } );
		Curve.Keys.Add( { 1.0f, End } );
	}

	// 非負RGBA値を既存byte化と同じ四捨五入で範囲内へ丸める。
	uint8 ExpectedByte( float32 Value )
	{
		return static_cast<uint8>( NMath::Clamp( static_cast<int32>( std::floor( NMath::Clamp( Value, 0.0f, 255.0f ) + 0.5f ) ), 0, 255 ) );
	}

	// packed RGBAの各成分がbyte丸め差以内で一致するかを比べる。
	bool PackedColorNear( uint32 Actual, uint32 Expected, uint8 Tolerance )
	{
		for ( int32 Channel = 0; Channel < 4; ++Channel )
		{
			// 今回比較する8bit成分。
			const int32 Shift = Channel * 8;
			// 実際に記録された成分。
			const int32 ActualValue = static_cast<int32>( ( Actual >> Shift ) & 0xFFu );
			// 独立式で求めた成分。
			const int32 ExpectedValue = static_cast<int32>( ( Expected >> Shift ) & 0xFFu );
			if ( NMath::Abs( ActualValue - ExpectedValue ) > Tolerance ) return false;
		}
		return true;
	}

	// world座標の一点を帯の描画位置へ設定して一刻進める。
	void AdvanceAt( TSharedPtr<IEffectPlayback>& Playback, float32 X, float32 DeltaTime )
	{
		Playback->SetTransform( FTransform( FVector3( X, 0.0f, 0.0f ) ) );
		Playback->Update( DeltaTime );
	}

	// 寿命割合0.25の一点だけを0へ落とすStep曲線を作る。
	void SetMiddleZeroCurve( FEffectScalarCurve& Curve )
	{
		Curve = FEffectScalarCurve();
		Curve.Interpolation = FEffectScalarCurve::EInterpolation::Step;
		Curve.Keys.Add( { 0.0f, 1.0f } );
		Curve.Keys.Add( { 0.25f, 0.0f } );
		Curve.Keys.Add( { 0.375f, 1.0f } );
		Curve.Keys.Add( { 1.0f, 1.0f } );
	}

	// 途中の寿命位置でalphaだけを0へ落とすStep曲線を作る。
	void SetMiddleTransparentCurve( FEffectColorCurve& Curve )
	{
		Curve = FEffectColorCurve();
		Curve.Interpolation = FEffectScalarCurve::EInterpolation::Step;
		Curve.Keys.Add( { 0.0f, FColor::White } );
		Curve.Keys.Add( { 0.25f, FColor::Transparent } );
		Curve.Keys.Add( { 0.375f, FColor::White } );
		Curve.Keys.Add( { 1.0f, FColor::White } );
	}

	// 粒子齢に沿った途中0点が実際の帯分割になることをON/OFFで確かめる。
	void CheckMiddleCut( NAssetTest::FChecker& C, CAssetManager& Assets, bool bMaterialEnabled, bool bAlphaCut )
	{
		// 分割位置だけが異なるエミッター設定。
		FParticleEmitterDefinition Emitter = NEffectTest::QuietEmitter();
		Emitter.Path.Kind = FEffectPathDefinition::EKind::Ribbon;
		Emitter.Path.BreakDistance = 100.0f;
		Emitter.Rate = 4.0f;
		Emitter.MaxParticles = 16;
		Emitter.Lifetime = { 2.0f, 2.0f };
		Emitter.Speed = { 0.0f, 0.0f };
		Emitter.StartSize = { 1.0f, 1.0f };
		Emitter.EndSize = { 1.0f, 1.0f };
		Emitter.StartColor = Emitter.EndColor = FColor32( 255, 255, 255, 255 );
		Emitter.Material.bEnabled = bMaterialEnabled;
		if ( bAlphaCut )
		{
			SetScalarCurve( Emitter.Appearance.Size, 1.0f, 1.0f );
			SetMiddleTransparentCurve( Emitter.Appearance.Color );
		}
		else
		{
			SetMiddleZeroCurve( Emitter.Appearance.Size );
			SetColorCurve( Emitter.Appearance.Color, FColor::White, FColor::White );
		}
		// 別々に記録する再生状態。
		TSharedPtr<IEffectPlayback> Playback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { Emitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "appearance-boundary-cut" );
		C.Check( Playback.IsValid(), "途中消失のRibbon playbackを作る" );
		if ( !Playback.IsValid() ) return;
		for ( int32 Index = 0; Index < 7; ++Index ) AdvanceAt( Playback, static_cast<float32>( Index ), 0.25f );
		// 分割後に記録された独立strip。
		const TArray<FRecordedRibbon> Ribbons = ReadRibbons( RecordPlayback( Playback ) );
		// 前後の端点がともに見え、消失点をつながない。
		const bool bSeparated = Ribbons.Num() == 2 && Ribbons[0].Points.Num() >= 2 && Ribbons[1].Points.Num() >= 2 && Ribbons[0].Header.JoinFlags == 0 && Ribbons[1].Header.JoinFlags == 0 && Ribbons[1].Points[0].Pos[0] - Ribbons[0].Points.Last().Pos[0] > 1.5f;
		C.Check( bSeparated, bMaterialEnabled ? ( bAlphaCut ? "材質Ribbonのalpha=0点で前後を別stripに分ける" : "材質Ribbonのsize=0点で前後を別stripに分ける" ) : ( bAlphaCut ? "従来Ribbonのalpha=0点で前後を別stripに分ける" : "従来Ribbonのsize=0点で前後を別stripに分ける" ) );
		Playback.Reset();
	}
}

// Appearanceの保存・編集copy・chunk共有・同位置統合・途中分割を実Playbackで確認する。
void NWorldTest::RunEffectAppearanceBoundaryTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir )
{
	using namespace NEffectTest;
	C.Group = "effect-appearance-boundaries";
	Assets.SetRootDirectory( Dir );
	// 保存・別名copy・再生を試す小さなRibbon定義。
	FParticleEmitterDefinition SourceEmitter = QuietEmitter();
	SourceEmitter.Path.Kind = FEffectPathDefinition::EKind::Ribbon;
	SourceEmitter.Path.BreakDistance = 2.0f;
	SourceEmitter.Rate = 4.0f;
	SourceEmitter.Lifetime = { 2.0f, 2.0f };
	SourceEmitter.StartSize = { 1.0f, 1.0f };
	SourceEmitter.EndSize = { 3.0f, 3.0f };
	SourceEmitter.StartColor = FColor32( 10, 20, 40, 200 );
	SourceEmitter.EndColor = FColor32( 140, 180, 120, 40 );
	SourceEmitter.Material.bEnabled = true;
	SourceEmitter.Material.bDissolve = true;
	SetScalarCurve( SourceEmitter.Appearance.Size, 1.125f, 3.0f );
	SourceEmitter.Appearance.Size.Keys[1].Time = 0.25f;
	SourceEmitter.Appearance.Size.Keys[1].Value = 2.0f;
	SourceEmitter.Appearance.Size.Keys.Add( { 1.0f, 3.0f } );
	SetColorCurve( SourceEmitter.Appearance.Color, FColor( 0.1f, 0.1f, 0.1f, 0.8f ), FColor( 0.9f, 0.9f, 0.9f, 0.2f ) );
	SourceEmitter.Appearance.Color.Keys[1].Time = 0.25f;
	SourceEmitter.Appearance.Color.Keys[1].Value = FColor( 0.5f, 0.25f, 0.75f, 0.5f );
	SourceEmitter.Appearance.Color.Keys.Add( { 1.0f, FColor( 0.9f, 0.9f, 0.9f, 0.2f ) } );
	// 書き出す未変更の原本定義。
	const TSharedPtr<FParticleEffectDefinition> SourceDefinition = MakeEffect( { SourceEmitter } );
	// 保存原本に使うsandbox相対path。
	const FString SourcePath = "appearance-boundary/source.effect.json";
	C.Check( Assets.SaveEffect( SourcePath, *SourceDefinition ), "Appearance付きRibbon原本をsandboxへ保存する" );
	// 原本を未保存編集copyとして開く状態。
	FEffectEditorState Editor;
	NDebugUI::NewEffect( Editor );
	// 原本を未保存copyへ複製できたか。
	const bool bOpenedCopy = NDebugUI::NewEffectFromPreset( Editor, SourcePath ) && Editor.FilePath.IsEmpty() && Editor.Document.IsDirty();
	C.Check( bOpenedCopy, "保存原本から未保存の編集copyを作る" );
	// copy内で編集対象となる両曲線の2番目のkeyが存在するか。
	const bool bEditableAppearanceKeys = bOpenedCopy && Editor.Document.Get().Emitters.IsValidIndex( 0 ) && Editor.Document.Get().Emitters[0].Appearance.Size.Keys.IsValidIndex( 1 ) && Editor.Document.Get().Emitters[0].Appearance.Color.Keys.IsValidIndex( 1 );
	C.Check( bEditableAppearanceKeys, "編集copyにSizeとColorの編集対象keyがある" );
	if ( bEditableAppearanceKeys )
	{
		// 編集copyだけを変えるエミッター値。
		FParticleEmitterDefinition Edited = Editor.Document.Get().Emitters[0];
		Edited.Appearance.Size.Keys[1].Value = 2.234567f;
		Edited.Appearance.Color.Keys[1].Value.a = 0.456789f;
		C.Check( NParticleEdit::SetEmitter( Editor.Document, 0, Edited ), "編集copyでSizeとalphaの小数値を履歴へ設定する" );
	}
	// 別名保存先に使う相対path。
	const FString CopyPath = "appearance-boundary/copy.effect.json";
	C.Check( bOpenedCopy && NDebugUI::SaveEffectFile( Editor, CopyPath ), "編集copyを別pathへ保存する" );
	// cacheを通さず原本とcopyをdiskから読み直す定義。
	FParticleEffectDefinition FreshSource;
	// diskから復元する編集copy定義。
	FParticleEffectDefinition FreshCopy;
	// 原本JSONのfresh reader。
	NJson::CJsonObject FreshSourceJson;
	// copy JSONのfresh reader。
	NJson::CJsonObject FreshCopyJson;
	// 保存原本の絶対path。
	const TOptional<FString> SourceFullPath = Assets.ResolvePath( SourcePath );
	// 保存copyの絶対path。
	const TOptional<FString> CopyFullPath = Assets.ResolvePath( CopyPath );
	// 保存原本をcacheなしで解析する結果。
	const bool bReadSource = SourceFullPath.IsSet() && FreshSourceJson.LoadFromFile( SourceFullPath.GetValue() ) && FParticleEffectDefinition::FromJson( FreshSourceJson.GetRoot(), FreshSource );
	// 保存copyをcacheなしで解析する結果。
	const bool bReadCopy = CopyFullPath.IsSet() && FreshCopyJson.LoadFromFile( CopyFullPath.GetValue() ) && FParticleEffectDefinition::FromJson( FreshCopyJson.GetRoot(), FreshCopy );
	C.Check( bReadSource && FreshSource.ComputeHash() == SourceDefinition->ComputeHash(), "別名保存後もdisk上の原本hashを保つ" );
	// fresh disk copyで検査する両曲線のkeyが存在するか。
	const bool bFreshCopyHasAppearanceKeys = bReadCopy && FreshCopy.Emitters.Num() == 1 && FreshCopy.Emitters[0].Appearance.Size.Keys.IsValidIndex( 1 ) && FreshCopy.Emitters[0].Appearance.Color.Keys.IsValidIndex( 1 );
	C.Check( bFreshCopyHasAppearanceKeys, "fresh disk copyにSizeとColorの検査対象keyがある" );
	C.Check( bFreshCopyHasAppearanceKeys && FreshCopy.ComputeHash() == Editor.Document.Get().ComputeHash() && FreshCopy.Emitters[0].Appearance.Size.Keys[1].Value == 2.234567f && FreshCopy.Emitters[0].Appearance.Color.Keys[1].Value.a == 0.456789f, "fresh disk copyのAppearance hashとfloat精度を保つ" );
	// fresh copyから再生する独立定義。
	// fresh read成功時のみ一件目を参照する。
	const TSharedPtr<FParticleEffectDefinition> PlaybackDefinition = bReadCopy && FreshCopy.Emitters.Num() == 1 ? MakeEffect( { FreshCopy.Emitters[0] } ) : MakeEffect( { SourceEmitter } );
	// 保存copyを再生する帯。
	TSharedPtr<IEffectPlayback> SavedTrail = NEffect::CreateParticlePlayback( PlaybackDefinition, &Assets, FEffectPlayParams(), FTransform::Identity, "appearance-boundary-saved-copy" );
	C.Check( SavedTrail.IsValid(), "fresh disk copyから再生を作る" );
	if ( SavedTrail.IsValid() )
	{
		AdvanceAt( SavedTrail, 0.0f, 0.25f );
		AdvanceAt( SavedTrail, 0.5f, 0.25f );
		AdvanceAt( SavedTrail, 1.0f, 0.25f );
		// 保存copyから記録した材質Ribbon。
		const TArray<FRecordedRibbon> SavedRibbons = ReadRibbons( RecordPlayback( SavedTrail ) );
		// 寿命割合0.25地点の独立した大きさ・RGBA期待値。
		const uint32 ExpectedColor = 73u << 24 | 45u << 16 | 15u << 8 | 21u;
		// 基準size1.5へ保存後のSize倍率を掛けた値。
		const float32 ExpectedWidth = 1.5f * 2.234567f;
		// 原点の点に期待する幅とrawRGBAがあるか。
		bool bSavedValues = SavedRibbons.Num() == 1;
		if ( bSavedValues )
		{
			// 原点に生まれたAge=0.5の最初の粒子。
			const auto& Points = SavedRibbons[0].Points;
			// 原点の粒子を示す点番号。
			int32 OriginIndex = INDEX_NONE;
			for ( int32 Index = 0; Index < Points.Num(); ++Index ) if ( NMath::Abs( Points[Index].Pos[0] ) < 1.0e-5f ) OriginIndex = Index;
			bSavedValues = OriginIndex != INDEX_NONE && NMath::Abs( Points[OriginIndex].Width - ExpectedWidth ) < 1.0e-4f && Points[OriginIndex].Color == ExpectedColor;
		}
		C.Check( bSavedValues, "saved-copy再生が寿命割合0.25の独立式から幅とRGBAを記録する" );
	}
	NDebugUI::ReleaseEffectEditor( Editor );
	SavedTrail.Reset();

	// 1030点で2chunkへ分かれる有効Appearance Ribbon。
	FParticleEmitterDefinition LongEmitter = QuietEmitter();
	LongEmitter.Path.Kind = FEffectPathDefinition::EKind::Ribbon;
	LongEmitter.Path.BreakDistance = 2.0f;
	LongEmitter.Rate = 8.0f;
	LongEmitter.MaxParticles = 1200;
	LongEmitter.Lifetime = { 256.0f, 256.0f };
	LongEmitter.Speed = { 0.0f, 0.0f };
	LongEmitter.StartSize = { 1.0f, 1.0f };
	LongEmitter.EndSize = { 3.0f, 3.0f };
	LongEmitter.StartColor = FColor32( 10, 20, 30, 200 );
	LongEmitter.EndColor = FColor32( 110, 120, 130, 100 );
	LongEmitter.Material.bEnabled = true;
	LongEmitter.Material.bDissolve = true;
	SetScalarCurve( LongEmitter.Appearance.Size, 0.5f, 1.5f );
	SetColorCurve( LongEmitter.Appearance.Color, FColor( 0.2f, 0.4f, 0.6f, 0.8f ), FColor( 0.8f, 0.6f, 0.4f, 0.2f ) );
	// 上限を越える点列の長期再生。
	TSharedPtr<IEffectPlayback> LongTrail = NEffect::CreateParticlePlayback( MakeEffect( { LongEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "appearance-boundary-long" );
	C.Check( LongTrail.IsValid(), "長いAppearance Ribbon再生を作る" );
	if ( LongTrail.IsValid() )
	{
		for ( int32 Index = 0; Index < 1030; ++Index ) AdvanceAt( LongTrail, static_cast<float32>( Index ) * 0.013f, 0.125f );
		// 分割後の帯命令を所有して読む。
		const TArray<FRecordedRibbon> Chunks = ReadRibbons( RecordPlayback( LongTrail ) );
		// 独立寿命式に渡す各粒子の実際の秒齢。
		TArray<NParticle::FDrawParticle> LongParticles;
		// 元のSimulationから秒齢を読む再生状態。
		const FParticlePlayback* LongState = static_cast<const FParticlePlayback*>( LongTrail.Get() );
		NParticle::CollectDrawParticles( LongState->GetSimulation(), LongParticles );
		// 2chunkの点数と材質点配列が一致するか。
		const bool bChunkMaterialPointsValid = Chunks.Num() == 2 && Chunks[0].MaterialPoints.Num() == Chunks[0].Points.Num() && Chunks[1].MaterialPoints.Num() == Chunks[1].Points.Num();
		// 失敗時にも安全に出せるchunk数と各点数。
		const int32 ChunkCount = Chunks.Num();
		// 先頭chunkがなければ0を示す診断値。
		const int32 FirstChunkPointCount = ChunkCount > 0 ? Chunks[0].Points.Num() : 0;
		// 2番目chunkがなければ0を示す診断値。
		const int32 SecondChunkPointCount = ChunkCount > 1 ? Chunks[1].Points.Num() : 0;
		C.Check( Chunks.Num() == 2 && Chunks[0].Points.Num() == 1024 && Chunks[0].Header.JoinFlags == FDrawRibbonData::NextPoint && Chunks[1].Header.JoinFlags == FDrawRibbonData::PreviousPoint && Chunks[1].Points.Num() == 9 && LongParticles.Num() == 1030, "1030点をghost付き1024点上限の2命令へ分ける (particles=%d chunks=%d first=%d second=%d)", LongParticles.Num(), ChunkCount, FirstChunkPointCount, SecondChunkPointCount );
		C.Check( bChunkMaterialPointsValid, "各chunkで材質点配列が形状点配列と同じ長さを持つ (chunks=%d firstMaterial=%d firstShape=%d secondMaterial=%d secondShape=%d)", ChunkCount, ChunkCount > 0 ? Chunks[0].MaterialPoints.Num() : 0, FirstChunkPointCount, ChunkCount > 1 ? Chunks[1].MaterialPoints.Num() : 0, SecondChunkPointCount );
		if ( bChunkMaterialPointsValid && Chunks[0].Points.Num() == 1024 && Chunks[1].Points.Num() == 9 && LongParticles.Num() == 1030 )
		{
			// 先行chunkの可視終端。
			const FDrawRibbonData::FPoint& FirstEnd = Chunks[0].Points[1022];
			// 次chunkで同じ可視終端を複写した点。
			const FDrawRibbonData::FPoint& SecondStart = Chunks[1].Points[1];
			// 先行chunkの後側ghost。
			const FDrawRibbonData::FPoint& FirstNextGhost = Chunks[0].Points[1023];
			// 次chunkで同じ後側ghostを複写した点。
			const FDrawRibbonData::FPoint& SecondNextGhost = Chunks[1].Points[2];
			// 次chunkの前側ghost。
			const FDrawRibbonData::FPoint& SecondPreviousGhost = Chunks[1].Points[0];
			// 先行chunkで同じ前側ghostを複写した点。
			const FDrawRibbonData::FPoint& FirstPreviousGhost = Chunks[0].Points[1021];
			// 材質点40byteの可視終端を全体比較する。
			const bool bEndpointBytesMatch = std::memcmp( &Chunks[0].MaterialPoints[1022], &Chunks[1].MaterialPoints[1], sizeof( FDrawMaterialRibbonData::FPoint ) ) == 0;
			// 材質点40byteの後側ghostを全体比較する。
			const bool bNextGhostBytesMatch = std::memcmp( &Chunks[0].MaterialPoints[1023], &Chunks[1].MaterialPoints[2], sizeof( FDrawMaterialRibbonData::FPoint ) ) == 0;
			// 材質点40byteの前側ghostを全体比較する。
			const bool bPreviousGhostBytesMatch = std::memcmp( &Chunks[1].MaterialPoints[0], &Chunks[0].MaterialPoints[1021], sizeof( FDrawMaterialRibbonData::FPoint ) ) == 0;
			C.Check( std::memcmp( &FirstEnd, &SecondStart, sizeof( FirstEnd ) ) == 0 && std::memcmp( &FirstNextGhost, &SecondNextGhost, sizeof( FirstNextGhost ) ) == 0 && std::memcmp( &SecondPreviousGhost, &FirstPreviousGhost, sizeof( SecondPreviousGhost ) ) == 0 && bEndpointBytesMatch && bNextGhostBytesMatch && bPreviousGhostBytesMatch, "chunk境界のvisible点と前後ghostで全40byteを一致させる" );
			// 独立式で確認する点の添字。
			const int32 SampleIndices[] = { 2, 517, 1022 };
			// 選んだ全サンプルが寿命割合・大きさ・色の固定式と合う。
			bool bIndependentValues = true;
			bool bIndependentAges = true;
			for ( const int32 Index : SampleIndices )
			{
				// 1030回の正確な二進刻みから独立に求める秒齢。
				const double ExpectedAge = static_cast<double>( 1029 - Index ) * 0.125;
				// 実際の粒子秒齢が固定したstep式と一致するか。
				bIndependentAges = bIndependentAges && NMath::Abs( LongParticles[Index].Age - ExpectedAge ) < 1.0e-5;
				// 独立式の秒齢から寿命割合を求める。
				const double T = ExpectedAge / 256.0;
				// 始終端の粒子サイズを補間し独立倍率を掛けた期待幅。
				const double ExpectedSampleWidth = ( 1.0 + 2.0 * T ) * ( 0.5 + T );
				// 元色の赤成分へAppearance赤倍率を掛けた期待値。
				const uint8 ExpectedRed = ExpectedByte( static_cast<float32>( ( 10.0 + 100.0 * T ) * ( 0.2 + 0.6 * T ) ) );
				// 元色の緑成分へAppearance緑倍率を掛けた期待値。
				const uint8 ExpectedGreen = ExpectedByte( static_cast<float32>( ( 20.0 + 100.0 * T ) * ( 0.4 + 0.2 * T ) ) );
				// 元色の青成分へAppearance青倍率を掛けた期待値。
				const uint8 ExpectedBlue = ExpectedByte( static_cast<float32>( ( 30.0 + 100.0 * T ) * ( 0.6 - 0.2 * T ) ) );
				// 元色のalpha成分へAppearance alpha倍率を掛けた期待値。
				const uint8 ExpectedAlpha = ExpectedByte( static_cast<float32>( ( 200.0 - 100.0 * T ) * ( 0.8 - 0.6 * T ) ) );
				// RGBAをGPU命令のlittle-endian順へまとめる値。
				const uint32 ExpectedPacked = static_cast<uint32>( ExpectedRed ) | ( static_cast<uint32>( ExpectedGreen ) << 8 ) | ( static_cast<uint32>( ExpectedBlue ) << 16 ) | ( static_cast<uint32>( ExpectedAlpha ) << 24 );
				bIndependentValues = bIndependentValues && NMath::Abs( Chunks[0].Points[Index].Width - ExpectedSampleWidth ) < 2.0e-3 && PackedColorNear( Chunks[0].Points[Index].Color, ExpectedPacked, 1 );
			}
			C.Check( bIndependentAges, "長いRibbonのサンプル粒子齢が正確な0.125秒step式に一致する" );
			C.Check( bIndependentValues, "長いRibbonの離れた3点で独立寿命式から幅と未premultiply RGBAを確認する" );
		}
	}
	LongTrail.Reset();

	// 同位置統合時に新しいAgeとAppearanceを選ぶRibbon。
	FParticleEmitterDefinition DedupEmitter = QuietEmitter();
	DedupEmitter.Path.Kind = FEffectPathDefinition::EKind::Ribbon;
	DedupEmitter.Path.BreakDistance = 2.0f;
	DedupEmitter.Rate = 4.0f;
	DedupEmitter.MaxParticles = 4;
	DedupEmitter.Lifetime = { 2.0f, 2.0f };
	DedupEmitter.Speed = { 0.0f, 0.0f };
	DedupEmitter.StartSize = { 1.0f, 1.0f };
	DedupEmitter.EndSize = { 3.0f, 3.0f };
	DedupEmitter.StartColor = FColor32( 10, 20, 40, 200 );
	DedupEmitter.EndColor = FColor32( 140, 180, 120, 40 );
	DedupEmitter.Material.bEnabled = true;
	SetScalarCurve( DedupEmitter.Appearance.Size, 1.0f, 2.0f );
	DedupEmitter.Appearance.Size.Keys.Insert( { 0.25f, 2.0f }, 1 );
	SetColorCurve( DedupEmitter.Appearance.Color, FColor::White, FColor::White );
	DedupEmitter.Appearance.Color.Keys[1].Value = FColor( 0.5f, 0.25f, 0.75f, 0.5f );
	DedupEmitter.Appearance.Color.Keys.Insert( { 0.25f, FColor( 0.5f, 0.25f, 0.75f, 0.5f ) }, 1 );
	// 同じ位置の二粒子を後着順でまとめる再生。
	TSharedPtr<IEffectPlayback> DedupTrail = NEffect::CreateParticlePlayback( MakeEffect( { DedupEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "appearance-boundary-dedup" );
	C.Check( DedupTrail.IsValid(), "同位置統合のRibbon playbackを作る" );
	if ( DedupTrail.IsValid() )
	{
		AdvanceAt( DedupTrail, 0.0f, 0.25f );
		AdvanceAt( DedupTrail, 0.0f, 0.25f );
		AdvanceAt( DedupTrail, 1.0f, 0.25f );
	}
	// 同位置を統合した結果と元粒子の値。
	const TArray<FRecordedRibbon> DedupRibbons = DedupTrail.IsValid() ? ReadRibbons( RecordPlayback( DedupTrail ) ) : TArray<FRecordedRibbon>();
	// 統合前の粒子値を確認する配列。
	TArray<NParticle::FDrawParticle> DedupParticles;
	if ( DedupTrail.IsValid() ) NParticle::CollectDrawParticles( static_cast<const FParticlePlayback*>( DedupTrail.Get() )->GetSimulation(), DedupParticles );
	bool bDedupLatest = DedupRibbons.Num() == 1 && DedupRibbons[0].Points.Num() == 2 && DedupParticles.Num() == 3;
	if ( bDedupLatest )
	{
		// 同位置へ後から生まれたAge=0.25粒子の出力点。
		const FDrawRibbonData::FPoint& Merged = DedupRibbons[0].Points[0];
		// 同じ位置に先に生まれたAge=0.5粒子。
		const NParticle::FDrawParticle& OlderParticle = DedupParticles[0];
		// 統合後に選ばれるAge=0.25粒子。
		const NParticle::FDrawParticle& LatestParticle = DedupParticles[1];
		// 旧Age=0.5粒子の独立期待幅。
		const double OlderWidth = ( 1.0 + 2.0 * 0.25 ) * 2.0;
		// 新Age=0.25粒子の曲線倍率と基準size。
		const double NewWidth = ( 1.0 + 2.0 * 0.125 ) * 1.5;
		// 旧Age=0.5粒子の独立色期待値。
		const uint32 OlderColor = 80u << 24 | 45u << 16 | 15u << 8 | 21u;
		// 新Age=0.25粒子の独立色期待値。
		const uint32 NewColor = 135u << 24 | 44u << 16 | 25u << 8 | 20u;
		bDedupLatest = NMath::Abs( OlderWidth - 3.0 ) < 1.0e-6 && OlderParticle.Size == 3.0f && OlderParticle.Color.r == 21 && OlderParticle.Color.g == 15 && OlderParticle.Color.b == 45 && OlderParticle.Color.a == 80 && NMath::Abs( LatestParticle.Size - NewWidth ) < 1.0e-4 && LatestParticle.Color.r == 20 && LatestParticle.Color.g == 25 && LatestParticle.Color.b == 44 && LatestParticle.Color.a == 135 && NMath::Abs( Merged.Width - NewWidth ) < 1.0e-4 && Merged.Color == NewColor && OlderColor != NewColor;
	}
	C.Check( bDedupLatest, "dedup後はAge0.25の新粒子の幅1.875とRGBA(20,25,44,135)を選ぶ" );
	DedupTrail.Reset();

	CheckMiddleCut( C, Assets, true, false );
	CheckMiddleCut( C, Assets, false, false );
	CheckMiddleCut( C, Assets, true, true );
	CheckMiddleCut( C, Assets, false, true );
}
