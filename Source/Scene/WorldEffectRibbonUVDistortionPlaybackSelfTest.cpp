// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"

#include <cmath>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <vector>

#include "Library/Core/Interfaces/Render/DrawDistortedMaterialRibbon.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Effect/ParticleEffectBackend.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Platform/FileSystem.h"

namespace
{
	// 記録bufferから固定headerと全点を範囲確認して複写する。
	struct FRecordedRibbon
	{
		// 画像・材質・点数を持つ帯設定。
		FDrawDistortedMaterialRibbonData Header;
		// command bufferから独立した全点。
		TArray<FDrawDistortedMaterialRibbonData::FPoint> Points;
	};

	// 同期読込できる最小のRGBA TGA fixtureを作る。
	std::vector<uint8> MakeRibbonDistortionTga()
	{
		// 4x4 true-color TGA headerとpixel bytes。
		std::vector<uint8> Bytes = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 4, 0, 32, 0x28 };
		for ( int32 Index = 0; Index < 16; ++Index ) { Bytes.push_back( 128 ); Bytes.push_back( 128 ); Bytes.push_back( 255 ); Bytes.push_back( 255 ); }
		return Bytes;
	}

	// 材質帯noise commandを長さ検査後に所有値へ読む。
	TArray<FRecordedRibbon> ReadDistortedRibbons( const FDrawBuffer& Buffer )
	{
		// 記録順に複写した帯command。
		TArray<FRecordedRibbon> Result;
		if ( Buffer.Words.empty() ) return Result;
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type != EDrawCommand::DistortedMaterialRibbon3D || Command.NumDataWords * 4 < static_cast<int32>( sizeof( FDrawDistortedMaterialRibbonData ) ) ) return;
			// headerの点数を可変部の範囲確認に使う。
			const FDrawDistortedMaterialRibbonData Header = Command.Read<FDrawDistortedMaterialRibbonData>();
			if ( Header.Material.Ribbon.NumPoints < 2 || Header.Material.Ribbon.NumPoints > FDrawRibbonData::MaxPoints ) return;
			// 固定部と全pointがcommandに収まる必要byte数。
			const usize RequiredBytes = sizeof( Header ) + static_cast<usize>( Header.Material.Ribbon.NumPoints ) * sizeof( FDrawDistortedMaterialRibbonData::FPoint );
			if ( RequiredBytes > static_cast<usize>( Command.NumDataWords ) * 4 ) return;
			// 検査済みcommandを所有する値。
			FRecordedRibbon Item;
			Item.Header = Header;
			Item.Points.SetNum( Header.Material.Ribbon.NumPoints );
			std::memcpy( Item.Points.GetData(), reinterpret_cast<const uint8*>( Command.Data ) + sizeof( Header ), static_cast<size_t>( Item.Points.Num() ) * sizeof( Item.Points[0] ) );
			Result.Add( std::move( Item ) );
		} );
		return Result;
	}

	// 記録列から指定command型の個数を数える。
	int32 CountRibbonCommands( const FDrawBuffer& Buffer, EDrawCommand Type )
	{
		// 指定型を数える累積値。
		int32 Count = 0;
		if ( Buffer.Words.empty() ) return Count;
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == Type ) ++Count; } );
		return Count;
	}

	// 既存main画像とnoise画像を使う材質付き帯emitterを作る。
	FParticleEmitterDefinition MakeRibbonEmitter( const FString& TexturePath, const FString& NoisePath )
	{
		// 空の既定設定から作った軌跡emitter。
		FParticleEmitterDefinition Emitter = NEffectTest::QuietEmitter();
		Emitter.Texture = TexturePath;
		Emitter.Path.Kind = FEffectPathDefinition::EKind::Ribbon;
		Emitter.Path.BreakDistance = 100.0f;
		Emitter.Lifetime = { 3.0f, 3.0f };
		Emitter.Speed = { 1.0f, 1.0f };
		Emitter.Direction = FVector3( 1.0f, 0.0f, 0.0f );
		Emitter.Gravity = FVector3::Zero;
		Emitter.StartSize = { 0.2f, 0.2f };
		Emitter.EndSize = { 0.2f, 0.2f };
		Emitter.StartColor = Emitter.EndColor = FColor32( 220, 230, 255, 255 );
		Emitter.Rate = 1100.0f;
		Emitter.MaxParticles = 1500;
		Emitter.Material.bEnabled = true;
		Emitter.Material.Distortion.bEnabled = true;
		Emitter.Material.Distortion.Texture = NoisePath;
		Emitter.Material.Distortion.UVScale = FVector2( 2.0f, 1.5f );
		Emitter.Material.Distortion.UVScroll = FVector2( 3.25f, -2.75f );
		Emitter.Material.Distortion.Strength.Interpolation = FEffectScalarCurve::EInterpolation::Linear;
		Emitter.Material.Distortion.Strength.Keys.Add( { 0.0f, 0.0f } );
		Emitter.Material.Distortion.Strength.Keys.Add( { 1.0f, 1.0f } );
		return Emitter;
	}

	// playbackの実出力から材質帯command列を得る。
	FDrawBuffer RecordRibbonPlayback( const TSharedPtr<IEffectPlayback>& Playback )
	{
		// 一度のRecordで所有する命令列。
		FDrawBuffer Buffer;
		// 既存記録APIへ渡す窓口。
		FDrawContext Draw( Buffer );
		if ( Playback.IsValid() ) Playback->Record( Draw );
		return Buffer;
	}

	// 出力位置と一致する実simulation粒子の秒齢を探す。
	bool FindParticleAge( const TArray<NParticle::FDrawParticle>& Particles, const FVector3& Position, float32& OutAge, float32& OutNormalizedAge )
	{
		for ( const NParticle::FDrawParticle& Particle : Particles )
		{
			if ( Particle.Emitter != 0 ) continue;
			if ( std::fabs( Particle.Position.x - Position.x ) > 1.0e-5f || std::fabs( Particle.Position.y - Position.y ) > 1.0e-5f || std::fabs( Particle.Position.z - Position.z ) > 1.0e-5f ) continue;
			OutAge = Particle.Age;
			OutNormalizedAge = Particle.NormalizedAge;
			return true;
		}
		return false;
	}

	// RecordRibbonと同じ粒子順から、元の累積距離Uを独立に求める。
	bool FindRibbonSourceU( const TArray<NParticle::FDrawParticle>& Particles, const FVector3& Position, float32& OutU )
	{
		// 直前までの粒子位置。
		FVector3 PreviousPosition = FVector3::Zero;
		// 先頭からの累積距離。
		float32 AccumulatedU = 0.0f;
		// 最初の対象点を特別扱いする状態。
		bool bHasPrevious = false;
		for ( const NParticle::FDrawParticle& Particle : Particles )
		{
			if ( Particle.Emitter != 0 ) continue;
			if ( bHasPrevious ) AccumulatedU += ( Particle.Position - PreviousPosition ).Length();
			PreviousPosition = Particle.Position;
			bHasPrevious = true;
			if ( std::fabs( Particle.Position.x - Position.x ) <= 1.0e-5f && std::fabs( Particle.Position.y - Position.y ) <= 1.0e-5f && std::fabs( Particle.Position.z - Position.z ) <= 1.0e-5f ) { OutU = AccumulatedU; return true; }
		}
		return false;
	}

	// 固定形状のLightningまたはTree Lightningをnoise付きで作る。
	FParticleEmitterDefinition MakeLightningEmitter( const FString& TexturePath, const FString& NoisePath, FEffectPathDefinition::EKind Kind )
	{
		// 同じmaterial条件を持つ一回放電emitter。
		FParticleEmitterDefinition Emitter = NEffectTest::QuietEmitter();
		Emitter.Texture = TexturePath;
		Emitter.Path.Kind = Kind;
		Emitter.Path.End = FVector3( 0.0f, 3.0f, 0.0f );
		Emitter.Path.Segments = 20;
		Emitter.Path.Branches = 2;
		Emitter.Path.Jitter = 0.2f;
		Emitter.Path.BranchLength = 0.55f;
		Emitter.Path.Tree.Depth = 2;
		Emitter.Path.Tree.ChildBranches = 2;
		Emitter.Path.Tree.GrowthSeconds = 0.2f;
		Emitter.Path.Tree.BranchDelaySeconds = 0.01f;
		Emitter.Path.Tree.StrokeCount = 1;
		Emitter.Path.Tree.StrokeInterval = 1.0f;
		Emitter.Path.Tree.StrokeSeconds = 1.0f;
		Emitter.Lifetime = { 2.0f, 2.0f };
		Emitter.StartSize = Emitter.EndSize = { 0.2f, 0.2f };
		Emitter.StartColor = Emitter.EndColor = FColor32( 220, 235, 255, 255 );
		Emitter.Bursts.Add( NEffectTest::Burst( 0.0f, 1 ) );
		Emitter.Material.bEnabled = true;
		Emitter.Material.Distortion.bEnabled = true;
		Emitter.Material.Distortion.Texture = NoisePath;
		Emitter.Material.Distortion.UVScale = FVector2( 1.75f, 0.9f );
		Emitter.Material.Distortion.UVScroll = FVector2( -3.5f, 2.25f );
		Emitter.Material.Distortion.Strength.Keys.Add( { 0.0f, 0.2f } );
		Emitter.Material.Distortion.Strength.Keys.Add( { 1.0f, 0.8f } );
		return Emitter;
	}
}

// 実particleの連続秒齢、chunk ghost、Lightning系の元particle時刻を検証する。
void NWorldTest::RunEffectRibbonUVDistortionPlaybackTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir )
{
	using namespace NEffectTest;
	C.Group = "effect-ribbon-uv-distortion-playback";
	// 呼出し元が使っていたasset root。
	const FString PreviousRoot = Assets.GetRootDirectory();
	Assets.SetRootDirectory( Dir );
	// fixture TGAを置くasset相対path。
	const FString TexturePath = "ribbon-uv-distortion/texture.tga";
	// TGA fixture保存結果。
	const bool bFixtureSaved = NFileSystem::EnsureDirectory( Dir / "ribbon-uv-distortion" ) && NAssetTest::WriteFileBytes( Dir / TexturePath, MakeRibbonDistortionTga() );
	C.Check( bFixtureSaved, "材質帯fixtureをsandboxへ保存する" );
	if ( !bFixtureSaved ) { Assets.SetRootDirectory( PreviousRoot ); return; }

	// 連続して異なる年齢の点を作る実Ribbon emitter。
	FParticleEmitterDefinition RibbonEmitter = MakeRibbonEmitter( TexturePath, TexturePath );
	// 通常factoryで作る長いRibbon playback。
	const TSharedPtr<IEffectPlayback> RibbonPlayback = NEffect::CreateParticlePlayback( MakeEffect( { RibbonEmitter }, 0.0f, true, 711u ), &Assets, FEffectPlayParams(), FTransform::Identity, "ribbon-uv-distortion-long-strip" );
	C.Check( RibbonPlayback.IsValid(), "長い材質Ribbonを通常Playback factoryから作る" );
	if ( !RibbonPlayback.IsValid() ) { Assets.SetRootDirectory( PreviousRoot ); return; }
	// 1100回進めて上限超えの帯を作る。
	for ( int32 Step = 0; Step < 1100; ++Step ) RibbonPlayback->Update( 0.001f );
	// 描画に渡る点の実simulation参照。
	TArray<NParticle::FDrawParticle> SourceParticles;
	NParticle::CollectDrawParticles( static_cast<const FParticlePlayback*>( RibbonPlayback.Get() )->GetSimulation(), SourceParticles );
	// 同一時刻で記録した初回と再試行のcommand列。
	const FDrawBuffer RibbonBuffer = RecordRibbonPlayback( RibbonPlayback );
	const FDrawBuffer RibbonAgain = RecordRibbonPlayback( RibbonPlayback );
	// 固定header・点列を安全に複写した各chunk。
	const TArray<FRecordedRibbon> RibbonCommands = ReadDistortedRibbons( RibbonBuffer );
	C.Check( SourceParticles.Num() > FDrawRibbonData::MaxPoints && RibbonCommands.Num() >= 2 && RibbonBuffer.Words == RibbonAgain.Words, "実simulationの1024点超Ribbonは再現可能な複数noise chunkへ分割される" );
	C.Check( CountRibbonCommands( RibbonBuffer, EDrawCommand::MaterialRibbon3D ) == 0, "正のStrengthを含むRibbonは旧材質commandへ混在しない" );
	if ( !RibbonCommands.IsEmpty() )
	{
		// 最初の可視点に対応する実particle年齢。
		float32 ReferenceAge = 0.0f;
		float32 ReferenceNormalizedAge = 0.0f;
		// payload先頭をsource粒子列へ対応付けた結果。
		const bool bReferenceFound = FindParticleAge( SourceParticles, FVector3( RibbonCommands[0].Points[0].Material.Path.Pos[0], RibbonCommands[0].Points[0].Material.Path.Pos[1], RibbonCommands[0].Points[0].Material.Path.Pos[2] ), ReferenceAge, ReferenceNormalizedAge );
		C.Check( bReferenceFound && std::isfinite( ReferenceNormalizedAge ), "noise帯先頭点を実simulation粒子へ対応付ける" );
		if ( bReferenceFound )
		{
			// strip共通基準の横ノイズ位相。
			const double BaseU = std::fmod( static_cast<double>( ReferenceAge ) * RibbonEmitter.Material.Distortion.UVScroll.x, 1.0 );
			// strip共通基準の縦ノイズ位相。
			const double BaseV = std::fmod( static_cast<double>( ReferenceAge ) * RibbonEmitter.Material.Distortion.UVScroll.y, 1.0 );
			// 検査点の実simulation年齢を使う結果。
			bool bAgeAndPhaseMatch = true;
			// 元simulation順から独立算出した累積距離U。
			bool bSourceUKept = true;
			// 同じstrip内の点ごとの累積距離が戻らない結果。
			bool bMonotonicU = true;
			for ( const FRecordedRibbon& Command : RibbonCommands )
			{
				// このchunk内で前点のsource Uを追跡する基準。
				float32 PreviousU = -1.0f;
				for ( const FDrawDistortedMaterialRibbonData::FPoint& Point : Command.Points )
				{
					// payload位置に一致する元particleの秒齢と寿命割合。
					float32 PointAge = 0.0f;
					float32 PointNormalizedAge = 0.0f;
					// lookupへ渡すpayload位置。
					const FVector3 Position( Point.Material.Path.Pos[0], Point.Material.Path.Pos[1], Point.Material.Path.Pos[2] );
					// 粒子の実年齢と割合を見つけた結果。
					const bool bFound = FindParticleAge( SourceParticles, Position, PointAge, PointNormalizedAge );
					if ( !bFound ) { bAgeAndPhaseMatch = false; continue; }
					// geometryへ渡す前の粒子列から求めた元U。
					float32 SourceU = 0.0f;
					// 元simulation点から累積Uを独立算出した結果。
					const bool bFoundSourceU = FindRibbonSourceU( SourceParticles, Position, SourceU );
					bSourceUKept &= bFoundSourceU && std::fabs( Point.Material.Path.U - SourceU ) < 1.0e-5f;
					// 点自身の寿命割合で評価するnoise強度。
					const float32 ExpectedStrength = RibbonEmitter.Material.Distortion.Strength.Evaluate( PointNormalizedAge, 0.05f );
					// 基準からの連続秒齢差。点ごとのfmodは行わない。
					const double AgeDelta = static_cast<double>( PointAge ) - ReferenceAge;
					// U方向の期待noise位相。
					const double ExpectedU = BaseU + AgeDelta * RibbonEmitter.Material.Distortion.UVScroll.x;
					// V方向の期待noise位相。
					const double ExpectedV = BaseV + AgeDelta * RibbonEmitter.Material.Distortion.UVScroll.y;
					bAgeAndPhaseMatch &= std::fabs( Point.Strength - ExpectedStrength ) < 1.0e-5f && std::fabs( Point.UVOffset[0] - ExpectedU ) < 2.0e-4 && std::fabs( Point.UVOffset[1] - ExpectedV ) < 2.0e-4;
					bMonotonicU &= PreviousU < 0.0f || Point.Material.Path.U >= PreviousU;
					PreviousU = Point.Material.Path.U;
				}
			}
			C.Check( bAgeAndPhaseMatch, "Ribbonの各noise値は元particleの寿命割合と共通age基準+未剰余age差に一致する" );
			C.Check( bSourceUKept, "Ribbonの累積Uはdistortion追加前の粒子列から再計算した値を保つ" );
			C.Check( bMonotonicU, "Ribbonのnoise追加後も元の累積距離Uを維持する" );
		}
		// 先頭・末尾・中間chunkの接線用ghost flagが位置に合う。
		bool bGhostFlagsMatch = RibbonCommands.Num() >= 2 && RibbonCommands[0].Header.Material.Ribbon.JoinFlags == FDrawRibbonData::NextPoint && RibbonCommands.Last().Header.Material.Ribbon.JoinFlags == FDrawRibbonData::PreviousPoint;
		for ( int32 Index = 1; Index + 1 < RibbonCommands.Num(); ++Index ) bGhostFlagsMatch &= RibbonCommands[Index].Header.Material.Ribbon.JoinFlags == ( FDrawRibbonData::PreviousPoint | FDrawRibbonData::NextPoint );
		C.Check( bGhostFlagsMatch, "先頭・中間・末尾のRibbon chunkへ正しいghost flagを渡す" );
		// 隣接chunkの前後3点は40byte材質値を含む全52byteで一致する。
		bool bGhostsMatch = RibbonCommands.Num() >= 2;
		for ( int32 Index = 1; Index < RibbonCommands.Num(); ++Index )
		{
			const FRecordedRibbon& Previous = RibbonCommands[Index - 1];
			const FRecordedRibbon& Current = RibbonCommands[Index];
			if ( Previous.Points.Num() < 3 || Current.Points.Num() < 3 ) { bGhostsMatch = false; continue; }
			for ( int32 Overlap = 0; Overlap < 3; ++Overlap )
			{
				const auto& PreviousPoint = Previous.Points[Previous.Points.Num() - 3 + Overlap];
				const auto& CurrentPoint = Current.Points[Overlap];
				bGhostsMatch &= std::memcmp( &PreviousPoint, &CurrentPoint, sizeof( PreviousPoint ) ) == 0;
			}
		}
		C.Check( bGhostsMatch, "長いRibbonの隣接chunkはghostとnoise値を同じまま共有する" );
	}

	// 全曲線点が0ならmissing noiseでも旧payloadを完全に保つ。
	FParticleEmitterDefinition ZeroEmitter = MakeRibbonEmitter( TexturePath, "ribbon-uv-distortion/missing-zero.tga" );
	ZeroEmitter.Material.Distortion.Strength.Keys[0].Value = 0.0f;
	ZeroEmitter.Material.Distortion.Strength.Keys[1].Value = 0.0f;
		// distortion offのbyte完全一致control emitter。
		FParticleEmitterDefinition PlainEmitter = ZeroEmitter;
		PlainEmitter.Material.Distortion.bEnabled = false;
		// missing noiseでも旧材質へ流れるallzero playback。
		const TSharedPtr<IEffectPlayback> ZeroPlayback = NEffect::CreateParticlePlayback( MakeEffect( { ZeroEmitter }, 0.0f, true, 712u ), &Assets, FEffectPlayParams(), FTransform::Identity, "ribbon-uv-distortion-all-zero" );
		// distortionをoffにした旧挙動playback。
		const TSharedPtr<IEffectPlayback> PlainPlayback = NEffect::CreateParticlePlayback( MakeEffect( { PlainEmitter }, 0.0f, true, 712u ), &Assets, FEffectPlayParams(), FTransform::Identity, "ribbon-uv-distortion-legacy-control" );
	if ( ZeroPlayback.IsValid() ) { ZeroPlayback->Update( 0.005f ); ZeroPlayback->Update( 0.005f ); ZeroPlayback->Update( 0.005f ); }
	if ( PlainPlayback.IsValid() ) { PlainPlayback->Update( 0.005f ); PlainPlayback->Update( 0.005f ); PlainPlayback->Update( 0.005f ); }
	// allzero playbackの旧command payload。
	const FDrawBuffer ZeroBuffer = RecordRibbonPlayback( ZeroPlayback );
	// distortion off controlの旧command payload。
	const FDrawBuffer PlainBuffer = RecordRibbonPlayback( PlainPlayback );
	C.Check( ZeroPlayback.IsValid() && PlainPlayback.IsValid() && CountRibbonCommands( ZeroBuffer, EDrawCommand::MaterialRibbon3D ) > 0 && CountRibbonCommands( ZeroBuffer, EDrawCommand::DistortedMaterialRibbon3D ) == 0 && ZeroBuffer.Words == PlainBuffer.Words, "全zero Strengthはmissing noiseを読まず旧MaterialRibbon全byteを保つ" );

	// 後半の区間数が上限を越えるstripは先行chunkも残さない。
	FParticleEmitterDefinition InvalidLaterEmitter = MakeRibbonEmitter( TexturePath, TexturePath );
	InvalidLaterEmitter.Speed = { 100000.0f, 100000.0f };
	InvalidLaterEmitter.Path.UVRepeat = 128.0f;
	InvalidLaterEmitter.Path.BreakDistance = 1000000.0f;
	InvalidLaterEmitter.Rate = 1100.0f;
	InvalidLaterEmitter.MaxParticles = 64;
	// 極端な経路反復数で後半を無効にするplayback。
	const TSharedPtr<IEffectPlayback> InvalidLaterPlayback = NEffect::CreateParticlePlayback( MakeEffect( { InvalidLaterEmitter }, 0.0f, true, 714u ), &Assets, FEffectPlayParams(), FTransform::Identity, "ribbon-uv-distortion-late-validation" );
	if ( InvalidLaterPlayback.IsValid() ) { InvalidLaterPlayback->Update( 0.01f ); InvalidLaterPlayback->Update( 0.01f ); }
	// invalid stripから記録された全command。
	const FDrawBuffer InvalidLaterBuffer = RecordRibbonPlayback( InvalidLaterPlayback );
	C.Check( InvalidLaterPlayback.IsValid() && CountRibbonCommands( InvalidLaterBuffer, EDrawCommand::DistortedMaterialRibbon3D ) == 0 && CountRibbonCommands( InvalidLaterBuffer, EDrawCommand::MaterialRibbon3D ) == 0, "後半区間の画像反復上限超過で先行chunkを部分記録しない" );

	// LightningとTreeは元particleの一つの時刻を全幹・枝へ共有する。
	for ( const FEffectPathDefinition::EKind Kind : { FEffectPathDefinition::EKind::Lightning, FEffectPathDefinition::EKind::TreeLightning } )
	{
		// 検査対象の固定形状emitter。
		FParticleEmitterDefinition Emitter = MakeLightningEmitter( TexturePath, TexturePath, Kind );
		// 全項目の有効性を調べる定義。
		const TSharedPtr<FParticleEffectDefinition> Definition = MakeEffect( { Emitter }, 0.0f, true, 713u );
		C.Check( Definition.IsValid() && !FParticleEffectDefinition::HasErrors( Definition->Validate() ), "Lightning系Playback fixture定義が有効 (kind=%d)", static_cast<int32>( Kind ) );
		// 一意のseedで開始する実particle playback。
		const TSharedPtr<IEffectPlayback> Playback = NEffect::CreateParticlePlayback( Definition, &Assets, FEffectPlayParams(), FTransform::Identity, Kind == FEffectPathDefinition::EKind::Lightning ? "uv-distortion-lightning" : "uv-distortion-tree" );
		if ( Playback.IsValid() )
		{
			Playback->Update( 0.01f );
			Playback->Update( 0.1f );
			if ( Kind == FEffectPathDefinition::EKind::TreeLightning ) Playback->Update( 0.6f );
		}
		// 記録したすべての枝・主幹。
		const TArray<FRecordedRibbon> Commands = ReadDistortedRibbons( RecordRibbonPlayback( Playback ) );
		// 元particleの秒齢と寿命割合。
		float32 ParticleAge = 0.0f;
		float32 ParticleNormalizedAge = 0.0f;
		// source simulationを参照するconcrete playback。
		const FParticlePlayback* Concrete = Playback.IsValid() ? static_cast<const FParticlePlayback*>( Playback.Get() ) : nullptr;
		// 一つの元粒子を取得できるか。
		const bool bParticleFound = Concrete != nullptr && Concrete->GetSimulation().Emitters.IsValidIndex( 0 ) && Concrete->GetSimulation().Emitters[0].Particles.Num() == 1;
		if ( bParticleFound ) { ParticleAge = Concrete->GetSimulation().Emitters[0].Particles[0].Age; ParticleNormalizedAge = ParticleAge / Concrete->GetSimulation().Emitters[0].Particles[0].Lifetime; }
		// 同一particle時計・元材質設定のまま記録した結果。
		bool bSourceClockKept = bParticleFound && !Commands.IsEmpty();
		// 不一致になった点の数を診断に使う。
		int32 SourceClockMismatchCount = 0;
		// 最初に不一致になった点の累積距離U。
		float32 FirstMismatchU = 0.0f;
		// 最初に不一致になった点の実Strength。
		float32 FirstMismatchStrength = 0.0f;
		// 最初に不一致になった点で期待するStrength。
		float32 FirstExpectedStrength = 0.0f;
		// 最初に不一致になった点の実U位相。
		float32 FirstMismatchOffsetU = 0.0f;
		// 最初に不一致になった点で期待するU位相。
		float32 FirstExpectedOffsetU = 0.0f;
		for ( const FRecordedRibbon& Command : Commands )
		{
			// 全幹枝で共有する期待U/Vノイズ位相。
			const double ExpectedU = std::fmod( static_cast<double>( ParticleAge ) * Emitter.Material.Distortion.UVScroll.x, 1.0 );
			const double ExpectedV = std::fmod( static_cast<double>( ParticleAge ) * Emitter.Material.Distortion.UVScroll.y, 1.0 );
			// 各strip内で累積Uが戻らないか調べる基準。
			float32 PreviousU = -1.0f;
			for ( const FDrawDistortedMaterialRibbonData::FPoint& Point : Command.Points )
			{
				// 元particleの寿命割合で求めた期待強度。
				const float32 ExpectedStrength = Emitter.Material.Distortion.Strength.Evaluate( ParticleNormalizedAge, 0.05f );
				// 時刻・noise・strip内Uがすべて一致する点。
				const bool bPointMatches = std::fabs( Point.Strength - ExpectedStrength ) < 1.0e-5f && std::fabs( Point.UVOffset[0] - ExpectedU ) < 1.0e-5 && std::fabs( Point.UVOffset[1] - ExpectedV ) < 1.0e-5 && ( PreviousU < 0.0f || Point.Material.Path.U >= PreviousU );
				if ( !bPointMatches && SourceClockMismatchCount == 0 )
				{
					FirstMismatchU = Point.Material.Path.U;
					FirstMismatchStrength = Point.Strength;
					FirstExpectedStrength = ExpectedStrength;
					FirstMismatchOffsetU = Point.UVOffset[0];
					FirstExpectedOffsetU = static_cast<float32>( ExpectedU );
				}
				if ( !bPointMatches ) ++SourceClockMismatchCount;
				bSourceClockKept &= bPointMatches;
				PreviousU = Point.Material.Path.U;
			}
		}
		C.Check( bSourceClockKept, Kind == FEffectPathDefinition::EKind::Lightning ? "Lightningの幹・枝は元particle ageと累積Uを維持する (kind=%d age=%.3f commands=%d mismatches=%d U=%.5f strength=%.5f/%.5f phaseU=%.5f/%.5f)" : "Tree Lightningの幹・枝は元particle ageと累積Uを維持する (kind=%d age=%.3f commands=%d mismatches=%d U=%.5f strength=%.5f/%.5f phaseU=%.5f/%.5f)", static_cast<int32>( Kind ), ParticleAge, Commands.Num(), SourceClockMismatchCount, FirstMismatchU, FirstMismatchStrength, FirstExpectedStrength, FirstMismatchOffsetU, FirstExpectedOffsetU );
		// outerとcoreは同じGeometryから作り、幅・発光だけを変える対応層。
		bool bLayerCoordinatesMatch = !Commands.IsEmpty() && Commands.Num() % 2 == 0;
		// 対応する層間で一致しない点の数。
		int32 LayerMismatchCount = 0;
		// 最初にずれた外側のU値。
		float32 FirstOuterU = 0.0f;
		// 最初にずれた芯のU値。
		float32 FirstCoreU = 0.0f;
		// 最初にずれた外側のx座標。
		float32 FirstOuterX = 0.0f;
		// 最初にずれた芯のx座標。
		float32 FirstCoreX = 0.0f;
		if ( bLayerCoordinatesMatch )
		{
			// 外側と芯の対応chunk数。
			const int32 LayerCount = Commands.Num() / 2;
			for ( int32 Layer = 0; Layer < LayerCount; ++Layer )
			{
				// 比較する外側層と対応する芯層。
				const FRecordedRibbon& Outer = Commands[Layer];
				const FRecordedRibbon& Core = Commands[Layer + LayerCount];
				bLayerCoordinatesMatch &= Outer.Points.Num() == Core.Points.Num();
				if ( Outer.Points.Num() != Core.Points.Num() ) continue;
				for ( int32 PointIndex = 0; PointIndex < Outer.Points.Num(); ++PointIndex )
				{
					// 同じ経路位置にある外側点と芯点。
					const auto& OuterPoint = Outer.Points[PointIndex];
					const auto& CorePoint = Core.Points[PointIndex];
					// 対応点の位置・U・noiseが一致するかを調べる結果。
					const bool bPointsMatch = OuterPoint.Material.Path.Pos[0] == CorePoint.Material.Path.Pos[0] && OuterPoint.Material.Path.Pos[1] == CorePoint.Material.Path.Pos[1] && OuterPoint.Material.Path.Pos[2] == CorePoint.Material.Path.Pos[2] && OuterPoint.Material.Path.U == CorePoint.Material.Path.U && OuterPoint.Strength == CorePoint.Strength && OuterPoint.UVOffset[0] == CorePoint.UVOffset[0] && OuterPoint.UVOffset[1] == CorePoint.UVOffset[1];
					if ( !bPointsMatch && LayerMismatchCount == 0 ) { FirstOuterU = OuterPoint.Material.Path.U; FirstCoreU = CorePoint.Material.Path.U; FirstOuterX = OuterPoint.Material.Path.Pos[0]; FirstCoreX = CorePoint.Material.Path.Pos[0]; }
					if ( !bPointsMatch ) ++LayerMismatchCount;
					bLayerCoordinatesMatch &= bPointsMatch;
				}
			}
		}
		C.Check( bLayerCoordinatesMatch, Kind == FEffectPathDefinition::EKind::Lightning ? "Lightningの外側core層は同じ幹枝位置・U・noise値を使う (kind=%d commands=%d layers=%d points=%d mismatches=%d x=%.5f/%.5f U=%.5f/%.5f)" : "Tree Lightningの外側core層は主幹と枝の位置・U・noise値を共有する (kind=%d commands=%d layers=%d points=%d mismatches=%d x=%.5f/%.5f U=%.5f/%.5f)", static_cast<int32>( Kind ), Commands.Num(), Commands.Num() / 2, Commands.IsEmpty() ? 0 : Commands[0].Points.Num(), LayerMismatchCount, FirstOuterX, FirstCoreX, FirstOuterU, FirstCoreU );
	}
	Assets.SetRootDirectory( PreviousRoot );
}
