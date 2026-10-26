// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"

#include <cmath>
#include <vector>

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Effect/ParticleEffectBackend.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Platform/FileSystem.h"

namespace
{
	// DxLibが同期読込できる白い4x4のTGA画像を作る。
	std::vector<uint8> MakeDistortionFixtureTga()
	{
		// TGA headerと4x4 RGBA画素を保持するbyte列。
		std::vector<uint8> Bytes = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 4, 0, 32, 0x28 };
		for ( int32 Index = 0; Index < 16; ++Index ) { Bytes.push_back( 128 ); Bytes.push_back( 128 ); Bytes.push_back( 255 ); Bytes.push_back( 255 ); }
		return Bytes;
	}

	// Playbackが記録した四角形材質payloadを所有値として読み取る。
	TArray<FDrawDistortedMaterialQuadData> ReadDistortedQuads( const FDrawBuffer& Buffer )
	{
		// 記録順に複写したゆがみ付き材質設定。
		TArray<FDrawDistortedMaterialQuadData> Result;
		// commandの型と固定長が一致するものだけ値として読む処理。
		const auto ReadCommand = [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::DistortedMaterialQuad3D && Command.NumDataWords == static_cast<int32>( sizeof( FDrawDistortedMaterialQuadData ) / 4 ) ) Result.Add( Command.Read<FDrawDistortedMaterialQuadData>() ); };
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, ReadCommand );
		return Result;
	}

	// 再生状態を変えず、現在の描画命令列を所有bufferへ記録する。
	FDrawBuffer RecordDistortionPlayback( const TSharedPtr<IEffectPlayback>& Playback )
	{
		// 一回のRecordが所有するcommand列。
		FDrawBuffer Buffer;
		// 粒子描画をcommand列へ記録する窓口。
		FDrawContext Draw( Buffer );
		if ( Playback.IsValid() ) Playback->Record( Draw );
		return Buffer;
	}
}

// 実Playbackが資源を開始時に保持し、粒子時計から歪み設定を記録することを確認する。
void NWorldTest::RunEffectUVDistortionPlaybackTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir )
{
	using namespace NEffectTest;
	C.Group = "effect-uv-distortion-playback";
	// 呼び出し元が使用していたasset rootを復元する値。
	const FString PreviousRoot = Assets.GetRootDirectory();
	Assets.SetRootDirectory( Dir );
	// TGA fixtureを置くroot直下の相対asset path。
	const FString TexturePath = "uv-distortion/noise.tga";
	// fixture用フォルダー作成と無圧縮画像保存の結果。
	const bool bFixtureSaved = NFileSystem::EnsureDirectory( Dir / "uv-distortion" ) && NAssetTest::WriteFileBytes( Dir / TexturePath, MakeDistortionFixtureTga() );
	C.Check( bFixtureSaved, "UVゆがみ画像をPlayback用sandboxへ保存する" );
	if ( !bFixtureSaved ) { Assets.SetRootDirectory( PreviousRoot ); return; }
	// 記録時刻と命令値を固定する単一粒子エミッター。
	FParticleEmitterDefinition Emitter = QuietEmitter();
	Emitter.Texture = TexturePath;
	Emitter.Lifetime = { 2.0f, 2.0f };
	Emitter.Bursts.Add( Burst( 0.0f, 1 ) );
	Emitter.Material.bEnabled = true;
	Emitter.Material.Distortion.bEnabled = true;
	Emitter.Material.Distortion.Texture = TexturePath;
	Emitter.Material.Distortion.UVScale = FVector2( 2.5f, 1.5f );
	Emitter.Material.Distortion.UVScroll = FVector2( -0.5f, 1.25f );
	Emitter.Material.Distortion.Strength.Interpolation = FEffectScalarCurve::EInterpolation::Linear;
	Emitter.Material.Distortion.Strength.Keys.Add( { 0.0f, 0.25f } );
	Emitter.Material.Distortion.Strength.Keys.Add( { 1.0f, 0.75f } );
	// factoryがdefinitionとtextureをsnapshotとして所有する再生。
	TSharedPtr<IEffectPlayback> Playback = NEffect::CreateParticlePlayback( MakeEffect( { Emitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-playback" );
	C.Check( Playback.IsValid(), "歪み設定から実Playbackを生成する" );
	if ( !Playback.IsValid() ) { Assets.SetRootDirectory( PreviousRoot ); return; }
	Playback->Update( 0.01f );
	Playback->Update( 1.0f );
	// 独立にsimulation粒子の秒齢を確認し、scroll期待値の基準を固定する。
	const FParticlePlayback* ConcretePlayback = static_cast<const FParticlePlayback*>( Playback.Get() );
	const bool bAgeIsOneSecond = ConcretePlayback->GetSimulation().Emitters.IsValidIndex( 0 ) && ConcretePlayback->GetSimulation().Emitters[0].Particles.Num() == 1 && NMath::Abs( ConcretePlayback->GetSimulation().Emitters[0].Particles[0].Age - 1.0f ) < 1.0e-5f;
	C.Check( bAgeIsOneSecond, "scroll検査対象の粒子は実simulationで秒齢1.0になる" );
	// 実際に記録したdataと同一Playbackでの再記録を比較する。
	const FDrawBuffer First = RecordDistortionPlayback( Playback );
	const FDrawBuffer Second = RecordDistortionPlayback( Playback );
	const TArray<FDrawDistortedMaterialQuadData> Commands = ReadDistortedQuads( First );
	C.Check( Commands.Num() == 1 && First.Words == Second.Words, "正の曲線値は再現可能なゆがみ付き命令を一つ記録する" );
	if ( Commands.Num() == 1 )
	{
		const FDrawDistortedMaterialQuadData& Data = Commands[0];
		C.Check( Data.DistortionTexture >= 0 && NMath::Abs( Data.Strength - 0.5f ) < 1.0e-5f && NMath::Abs( Data.UVScale[0] - 2.5f ) < 1.0e-5f && NMath::Abs( Data.UVScale[1] - 1.5f ) < 1.0e-5f, "材質曲線の中間強度とゆがみ倍率を命令へ渡す" );
		C.Check( NMath::Abs( Data.UVOffset[0] - 0.5f ) < 1.0e-4f && NMath::Abs( Data.UVOffset[1] - 0.25f ) < 1.0e-4f, "負の横scrollと縦scrollを粒子秒齢で折り返す" );
		C.Check( NDrawDistortedMaterialQuad::IsValid( Data ), "記録payloadが実行側の完全検証を通る" );
	}
	// 材質は有効でも強度0なら旧命令を使い、payload byteを変えない。
	FParticleEmitterDefinition ZeroEmitter = Emitter;
	ZeroEmitter.Material.Distortion.Strength.Keys[0].Value = 0.0f;
	ZeroEmitter.Material.Distortion.Strength.Keys[1].Value = 0.0f;
	TSharedPtr<IEffectPlayback> ZeroPlayback = NEffect::CreateParticlePlayback( MakeEffect( { ZeroEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-zero" );
	if ( ZeroPlayback.IsValid() ) ZeroPlayback->Update( 0.01f );
	// 強度0の材質命令数とゆがみ命令数を別々に数える。
	int32 ZeroMaterialCount = 0;
	int32 ZeroDistortedCount = 0;
	const FDrawBuffer ZeroBuffer = RecordDistortionPlayback( ZeroPlayback );
	const auto CountZeroCommands = [&]( const FDrawCommandView& Command ) { ZeroMaterialCount += Command.Type == EDrawCommand::MaterialQuad3D ? 1 : 0; ZeroDistortedCount += Command.Type == EDrawCommand::DistortedMaterialQuad3D ? 1 : 0; };
	ForEachDrawCommand( FDrawSpan{ ZeroBuffer.Words.data(), static_cast<int32>( ZeroBuffer.Words.size() ) }, CountZeroCommands );
	C.Check( ZeroPlayback.IsValid() && ZeroMaterialCount == 1 && ZeroDistortedCount == 0, "歪み強度0は旧MaterialQuad命令を保つ" );
	// 強度0の既存命令が設定なし材質の同Age記録とbyte単位で一致する。
	FParticleEmitterDefinition ZeroWithoutDistortion = ZeroEmitter;
	ZeroWithoutDistortion.Material.Distortion.bEnabled = false;
	TSharedPtr<IEffectPlayback> ZeroBaselinePlayback = NEffect::CreateParticlePlayback( MakeEffect( { ZeroWithoutDistortion } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-zero-baseline" );
	if ( ZeroBaselinePlayback.IsValid() ) ZeroBaselinePlayback->Update( 0.01f );
	C.Check( ZeroBuffer.Words.size() > 1 && ZeroBaselinePlayback.IsValid() && ZeroBuffer.Words == RecordDistortionPlayback( ZeroBaselinePlayback ).Words, "強度0命令は同Ageの変位なし材質と旧payload bytesが一致する" );
	// 負の微小scrollがdoubleからfloatへ丸められる1.0境界を確かめる。
	FParticleEmitterDefinition BoundaryEmitter = Emitter;
	BoundaryEmitter.Material.Distortion.UVScroll = FVector2( -1.0e-9f, 0.0f );
	TSharedPtr<IEffectPlayback> BoundaryPlayback = NEffect::CreateParticlePlayback( MakeEffect( { BoundaryEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-wrap-boundary" );
	if ( BoundaryPlayback.IsValid() ) { BoundaryPlayback->Update( 0.01f ); BoundaryPlayback->Update( 1.0f ); }
	const TArray<FDrawDistortedMaterialQuadData> BoundaryCommands = ReadDistortedQuads( RecordDistortionPlayback( BoundaryPlayback ) );
	C.Check( BoundaryCommands.Num() == 1 && BoundaryCommands[0].UVOffset[0] == 0.0f && NDrawDistortedMaterialQuad::IsValid( BoundaryCommands[0] ), "float丸めで1.0になる負の小移動は0へ折り返し命令を保つ" );
	// 材質を切った同じsimulationは従来Quad命令だけを使う。
	FParticleEmitterDefinition LegacyEmitter = Emitter;
	LegacyEmitter.Material.bEnabled = false;
	TSharedPtr<IEffectPlayback> LegacyPlayback = NEffect::CreateParticlePlayback( MakeEffect( { LegacyEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-disabled" );
	if ( LegacyPlayback.IsValid() ) LegacyPlayback->Update( 0.01f );
	// 材質OFF時に誤って新commandが出ないことを数える。
	int32 LegacyQuadCount = 0;
	int32 LegacyDistortedCount = 0;
	const FDrawBuffer LegacyBuffer = RecordDistortionPlayback( LegacyPlayback );
	const auto CountLegacyCommands = [&]( const FDrawCommandView& Command ) { LegacyQuadCount += Command.Type == EDrawCommand::Quad3D ? 1 : 0; LegacyDistortedCount += Command.Type == EDrawCommand::DistortedMaterialQuad3D ? 1 : 0; };
	ForEachDrawCommand( FDrawSpan{ LegacyBuffer.Words.data(), static_cast<int32>( LegacyBuffer.Words.size() ) }, CountLegacyCommands );
	C.Check( LegacyPlayback.IsValid() && LegacyQuadCount == 1 && LegacyDistortedCount == 0, "材質OFFは旧Quad命令を使う" );
	// 材質OFFでは変位設定の有無だけで旧command bytesが変わらない。
	FParticleEmitterDefinition LegacyWithoutDistortion = LegacyEmitter;
	LegacyWithoutDistortion.Material.Distortion.bEnabled = false;
	TSharedPtr<IEffectPlayback> LegacyPlainPlayback = NEffect::CreateParticlePlayback( MakeEffect( { LegacyWithoutDistortion } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-disabled-baseline" );
	if ( LegacyPlainPlayback.IsValid() ) LegacyPlainPlayback->Update( 0.01f );
	C.Check( LegacyPlayback.IsValid() && LegacyPlainPlayback.IsValid() && LegacyBuffer.Words == RecordDistortionPlayback( LegacyPlainPlayback ).Words, "材質OFFの変位設定は従来Quad bytesを変えない" );
	Assets.SetRootDirectory( PreviousRoot );
}
