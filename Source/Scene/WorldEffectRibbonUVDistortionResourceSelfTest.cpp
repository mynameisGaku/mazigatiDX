// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"

#include <set>
#include <vector>

#include "Library/Core/Interfaces/Render/DrawDistortedMaterialRibbon.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Effect/ParticleEffectBackend.h"
#include "Library/Framework/Effect/EffectPathGeometry.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Platform/FileSystem.h"
#include "AssetSelfTestSupport.h"

namespace
{
	// 材質付き経路用のmain画像とnoise画像をfake backendが読める形で保存する。
	bool WriteRibbonTextureFixture( const FString& Dir, const FString& RelativePath )
	{
		// 既存asset test helperが作る小さな有効BMP。
		const std::vector<uint8> Image = NAssetTest::MakeSheetBmp();
		return NFileSystem::EnsureDirectory( Dir / "ribbon-uv-resource" ) && NAssetTest::WriteFileBytes( Dir / RelativePath, Image );
	}

	// 指定kindと有効なnoise設定を持つ経路emitterを作る。
	FParticleEmitterDefinition MakeRibbonResourceEmitter( const FString& MainPath, const FString& NoisePath, FEffectPathDefinition::EKind Kind, bool bPositiveStrength )
	{
		// 既定の粒子設定に経路固有値を加えるemitter。
		FParticleEmitterDefinition Emitter = NEffectTest::QuietEmitter();
		Emitter.Texture = MainPath;
		Emitter.Path.Kind = Kind;
		Emitter.Path.End = FVector3( 0.0f, 2.0f, 0.0f );
		Emitter.Path.Segments = 12;
		Emitter.Path.Branches = 1;
		Emitter.Path.Jitter = 0.2f;
		Emitter.Path.BranchLength = 0.5f;
		Emitter.Path.Tree.Depth = 1;
		Emitter.Path.Tree.ChildBranches = 2;
		Emitter.Path.Tree.GrowthSeconds = 0.1f;
		Emitter.Path.Tree.StrokeCount = 1;
		Emitter.Path.Tree.StrokeInterval = 1.0f;
		Emitter.Path.Tree.StrokeSeconds = 0.8f;
		Emitter.Lifetime = { 2.0f, 2.0f };
		Emitter.Speed = { 1.0f, 1.0f };
		Emitter.Direction = FVector3( 1.0f, 0.0f, 0.0f );
		Emitter.Gravity = FVector3::Zero;
		Emitter.StartSize = Emitter.EndSize = { 0.2f, 0.2f };
		Emitter.StartColor = Emitter.EndColor = FColor32( 220, 230, 255, 255 );
		Emitter.MaxParticles = 64;
		Emitter.Rate = Kind == FEffectPathDefinition::EKind::Ribbon ? 10.0f : 0.0f;
		if ( Kind != FEffectPathDefinition::EKind::Ribbon ) Emitter.Bursts.Add( NEffectTest::Burst( 0.0f, 1 ) );
		Emitter.Material.bEnabled = true;
		Emitter.Material.Distortion.bEnabled = true;
		Emitter.Material.Distortion.Texture = NoisePath;
		Emitter.Material.Distortion.UVScale = FVector2( 1.5f, 0.75f );
		Emitter.Material.Distortion.UVScroll = FVector2( 0.4f, -0.25f );
		Emitter.Material.Distortion.Strength.Keys.Add( { 0.0f, bPositiveStrength ? 0.3f : 0.0f } );
		Emitter.Material.Distortion.Strength.Keys.Add( { 1.0f, bPositiveStrength ? 0.8f : 0.0f } );
		return Emitter;
	}

	// command列にある指定型を数える。
	int32 CountRibbonResourceCommands( const FDrawBuffer& Buffer, EDrawCommand Type )
	{
		// command型の出現数。
		int32 Count = 0;
		if ( Buffer.Words.empty() ) return Count;
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == Type ) ++Count; } );
		return Count;
	}

	// playbackのRecord窓口で実際のcommand列を作る。
	FDrawBuffer RecordRibbonResourcePlayback( const TSharedPtr<IEffectPlayback>& Playback )
	{
		// 今回の記録だけが所有するbuffer。
		FDrawBuffer Buffer;
		// 既存command記録API。
		FDrawContext Draw( Buffer );
		if ( Playback.IsValid() ) Playback->Record( Draw );
		return Buffer;
	}

	// 遅延解放されたfake textureを回収する。
	void CollectRibbonTextures( CAssetManager& Assets )
	{
		Assets.ReleaseUnused();
		Assets.Tick( 0.0f );
	}
}

// Ribbon・Lightning・Treeでnoiseの準備時点とrecord中の資源非生成を確認する。
void NWorldTest::RunEffectRibbonUVDistortionResourceTests( NAssetTest::FChecker& C, CAssetManager& Assets, NAssetTest::FFakeAssetBackend& Fake, const FString& Dir )
{
	C.Group = "effect-ribbon-uv-distortion-resources";
	// 呼出し元が使用していたroot directory。
	const FString PreviousRoot = Assets.GetRootDirectory();
	Assets.SetRootDirectory( Dir );
	// kindごとにcacheが混ざらないmain画像とnoise画像のpath。
	const FString MainPaths[] = { "ribbon-uv-resource/main-ribbon.bmp", "ribbon-uv-resource/main-lightning.bmp", "ribbon-uv-resource/main-tree.bmp" };
	const FString NoisePaths[] = { "ribbon-uv-resource/noise-ribbon.bmp", "ribbon-uv-resource/noise-lightning.bmp", "ribbon-uv-resource/noise-tree.bmp" };
	// 3種類の全fixture保存状態。
	bool bFixturesSaved = true;
	for ( int32 Index = 0; Index < 3; ++Index ) bFixturesSaved &= WriteRibbonTextureFixture( Dir, MainPaths[Index] ) && WriteRibbonTextureFixture( Dir, NoisePaths[Index] );
	C.Check( bFixturesSaved, "Ribbon・Lightning・Tree用main/noise画像をsandboxへ保存する" );
	if ( !bFixturesSaved ) { Assets.SetRootDirectory( PreviousRoot ); return; }
	// 開始前から残っているfake texture handle。
	const std::set<int32> InitialTextures = Fake.Textures;
	// 経路kindの固定順。
	const FEffectPathDefinition::EKind Kinds[] = { FEffectPathDefinition::EKind::Ribbon, FEffectPathDefinition::EKind::Lightning, FEffectPathDefinition::EKind::TreeLightning };
	for ( int32 Index = 0; Index < 3; ++Index )
	{
		// 検査対象kindのnoise付きemitter。
		const FParticleEmitterDefinition Emitter = MakeRibbonResourceEmitter( MainPaths[Index], NoisePaths[Index], Kinds[Index], true );
		// fixtureの全項目をまとめた検証対象。
		const TSharedPtr<FParticleEffectDefinition> Definition = NEffectTest::MakeEffect( { Emitter }, 0.0f, true, 900u + static_cast<uint32>( Index ) );
		// factory開始前にbackendが読んだ画像数。
		const int32 LoadsBefore = Fake.NumTextureLoads;
		// ready noiseを開始時に固定する再生。
		TSharedPtr<IEffectPlayback> Playback = NEffect::CreateParticlePlayback( Definition, &Assets, FEffectPlayParams(), FTransform::Identity, "ribbon-uv-distortion-ready" );
		C.Check( Definition.IsValid() && !FParticleEffectDefinition::HasErrors( Definition->Validate() ), "ready経路のfixture定義が有効 (kind=%d)", static_cast<int32>( Kinds[Index] ) );
		if ( Kinds[Index] == FEffectPathDefinition::EKind::TreeLightning )
		{
			// 正しい放電時刻にTree幹と枝が生成されるか調べる形状。
			FEffectPathGeometry Geometry;
			NEffectPath::BuildTreeLightning( Emitter.Path, 900u, 0.2f, FVector3::Zero, Emitter.Path.End, 0.2f, Emitter.StartColor, Geometry );
			// 主幹と少なくとも一本の枝が持つstrip数。
			int32 NonEmptyStripCount = 0;
			for ( const FEffectPathGeometry::FStrip& Strip : Geometry.Strips ) if ( Strip.Points.Num() > 1 ) ++NonEmptyStripCount;
			C.Check( Emitter.Path.Tree.Validate().IsEmpty() && NonEmptyStripCount >= 2, "有効な放電時刻にTree主幹と枝を生成する (strips=%d)", NonEmptyStripCount );
		}
		C.Check( Playback.IsValid() && Fake.NumTextureLoads == LoadsBefore + 2, "材質付き経路はmainとready noiseを開始時に一度ずつ読む (kind=%d)", static_cast<int32>( Kinds[Index] ) );
		if ( Playback.IsValid() ) { Playback->Update( 0.1f ); Playback->Update( 0.1f ); }
		// Record前のreadiness確認回数と生存handle数。
		const int32 QueriesBeforeRecord = Fake.NumLoadingQueries;
		const usize HandlesBeforeRecord = Fake.Textures.size();
		// 実Playback出力。
		const FDrawBuffer Buffer = RecordRibbonResourcePlayback( Playback );
		// 記録された新commandの種別。
		const EDrawCommand ExpectedCommand = EDrawCommand::DistortedMaterialRibbon3D;
		// 期待commandが保持された数。
		const int32 DistortedCount = CountRibbonResourceCommands( Buffer, ExpectedCommand );
		// legacy材質commandが残った数。
		const int32 LegacyCount = CountRibbonResourceCommands( Buffer, EDrawCommand::MaterialRibbon3D );
		C.Check( DistortedCount > 0 && LegacyCount == 0, "ready noise経路が新帯commandだけを記録する (kind=%d distorted=%d legacy=%d)", static_cast<int32>( Kinds[Index] ), DistortedCount, LegacyCount );
		C.Check( Fake.NumLoadingQueries == QueriesBeforeRecord && Fake.Textures.size() == HandlesBeforeRecord, "Recordはready再照会・noise読込・handle作成をしない" );
		Playback.Reset();
		CollectRibbonTextures( Assets );
	}

	// distortion無効のRibbon・Lightning・Treeはnoiseを読まず既存材質へ流す。
	for ( int32 Index = 0; Index < 3; ++Index )
	{
		// noise設定だけを無効にした経路emitter。
		FParticleEmitterDefinition Emitter = MakeRibbonResourceEmitter( MainPaths[Index], "ribbon-uv-resource/unused-disabled-" + FString::FromInt( Index ) + ".bmp", Kinds[Index], true );
		Emitter.Material.Distortion.bEnabled = false;
		// disabled factory前のtexture load数。
		const int32 LoadsBefore = Fake.NumTextureLoads;
		// disabled設定で開始する再生。
		TSharedPtr<IEffectPlayback> Playback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { Emitter }, 0.0f, true, 915u + static_cast<uint32>( Index ) ), &Assets, FEffectPlayParams(), FTransform::Identity, "ribbon-uv-distortion-disabled" );
		C.Check( Playback.IsValid() && Fake.NumTextureLoads == LoadsBefore + 1, "disabled distortionは各経路でmain画像だけ読む" );
		if ( Playback.IsValid() ) { Playback->Update( 0.1f ); Playback->Update( 0.1f ); }
		// disabled再生の記録結果。
		const FDrawBuffer Buffer = RecordRibbonResourcePlayback( Playback );
		C.Check( CountRibbonResourceCommands( Buffer, EDrawCommand::DistortedMaterialRibbon3D ) == 0, "disabled distortionは新しい帯commandを出さない" );
		Playback.Reset();
		CollectRibbonTextures( Assets );
	}

	// missingまたはInvalidHandle noiseは正strength帯を全体skipする。
	// read failure fixtureで使うmainとnoiseのpath。
	const FString FailedMainPath = "ribbon-uv-resource/failing-main.bmp";
	const FString FailedNoisePath = "ribbon-uv-resource/failing-noise.bmp";
	// main/noise双方の失敗fixture保存状態。
	const bool bFailureFixtures = WriteRibbonTextureFixture( Dir, FailedMainPath ) && WriteRibbonTextureFixture( Dir, FailedNoisePath );
	C.Check( bFailureFixtures, "noise失敗test用fixtureを保存する" );
	if ( bFailureFixtures )
	{
		// noise画像だけをbackendで失敗させる呼出番号。
		const int32 PreviousTextureFailure = Fake.FailTextureLoadCall;
		Fake.FailTextureLoadCall = Fake.NumTextureLoads + 2;
		// noise handleを失敗させるRibbon設定。
		const FParticleEmitterDefinition FailedEmitter = MakeRibbonResourceEmitter( FailedMainPath, FailedNoisePath, FEffectPathDefinition::EKind::Ribbon, true );
		// noise読込失敗を保持した再生。
		TSharedPtr<IEffectPlayback> FailedPlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { FailedEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "ribbon-uv-distortion-invalid-handle" );
		Fake.FailTextureLoadCall = PreviousTextureFailure;
		C.Check( FailedPlayback.IsValid(), "noise handle失敗でもPlayback objectは作る" );
		if ( FailedPlayback.IsValid() ) { FailedPlayback->Update( 0.1f ); FailedPlayback->Update( 0.1f ); }
		// handle失敗Playbackが記録したcommand。
		const FDrawBuffer FailedBuffer = RecordRibbonResourcePlayback( FailedPlayback );
		C.Check( CountRibbonResourceCommands( FailedBuffer, EDrawCommand::DistortedMaterialRibbon3D ) == 0 && CountRibbonResourceCommands( FailedBuffer, EDrawCommand::MaterialRibbon3D ) == 0, "noise InvalidHandle時に正strengthの帯全体をskipする" );
		FailedPlayback.Reset();
		CollectRibbonTextures( Assets );
	}
	for ( int32 Index = 0; Index < 3; ++Index )
	{
		// filesystemに存在しないnoiseを使うkindごとのemitter。
		const FString MissingPath = "ribbon-uv-resource/missing-" + FString::FromInt( Index ) + ".bmp";
		// 実在mainと欠落noiseを組にした設定。
		const FParticleEmitterDefinition MissingEmitter = MakeRibbonResourceEmitter( MainPaths[Index], MissingPath, Kinds[Index], true );
		// missing noiseで作った再生。
		TSharedPtr<IEffectPlayback> MissingPlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { MissingEmitter }, 0.0f, true, 930u + static_cast<uint32>( Index ) ), &Assets, FEffectPlayParams(), FTransform::Identity, "ribbon-uv-distortion-missing" );
		if ( MissingPlayback.IsValid() ) { MissingPlayback->Update( 0.1f ); MissingPlayback->Update( 0.1f ); }
		// missing noise再生の記録結果。
		const FDrawBuffer MissingBuffer = RecordRibbonResourcePlayback( MissingPlayback );
		C.Check( MissingPlayback.IsValid() && CountRibbonResourceCommands( MissingBuffer, EDrawCommand::DistortedMaterialRibbon3D ) == 0 && CountRibbonResourceCommands( MissingBuffer, EDrawCommand::MaterialRibbon3D ) == 0, "missing noiseは各材質経路の帯全体をskipする" );
		MissingPlayback.Reset();
		CollectRibbonTextures( Assets );
	}

	// 3種類とも全zeroなら存在するnoise画像でもloadせず、旧材質経路を使う。
	for ( int32 Index = 0; Index < 3; ++Index )
	{
		// 誤ったnoise loadがbackendまで届くよう、実在する一意画像を保存するpath。
		const FString PresentZeroNoisePath = "ribbon-uv-resource/present-zero-" + FString::FromInt( Index ) + ".bmp";
		// zero-noise用fixtureの保存結果。
		const bool bPresentNoiseSaved = WriteRibbonTextureFixture( Dir, PresentZeroNoisePath );
		C.Check( bPresentNoiseSaved, "全zero用の実在noise画像を保存する (kind=%d)", static_cast<int32>( Kinds[Index] ) );
		if ( !bPresentNoiseSaved ) continue;
		// 実在noiseを参照する全zero emitter。
		FParticleEmitterDefinition Emitter = MakeRibbonResourceEmitter( MainPaths[Index], PresentZeroNoisePath, Kinds[Index], false );
		// factoryが始まる前の読み込み回数。
		const int32 LoadsBefore = Fake.NumTextureLoads;
		// zero強度ではnoiseを所有しないPlayback。
		TSharedPtr<IEffectPlayback> Playback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { Emitter }, 0.0f, true, 910u + static_cast<uint32>( Index ) ), &Assets, FEffectPlayParams(), FTransform::Identity, "ribbon-uv-distortion-zero" );
		C.Check( Playback.IsValid() && Fake.NumTextureLoads == LoadsBefore + 1, "実在noiseでも全zero経路はmainだけloadする (kind=%d loads=%d expected=%d)", static_cast<int32>( Kinds[Index] ), Fake.NumTextureLoads - LoadsBefore, 1 );
		if ( Playback.IsValid() ) { Playback->Update( 0.1f ); Playback->Update( 0.1f ); }
		// 全zero再生の旧command出力。
		const FDrawBuffer Buffer = RecordRibbonResourcePlayback( Playback );
		// distortedと旧材質commandの記録数。
		const int32 DistortedCount = CountRibbonResourceCommands( Buffer, EDrawCommand::DistortedMaterialRibbon3D );
		const int32 LegacyCount = CountRibbonResourceCommands( Buffer, EDrawCommand::MaterialRibbon3D );
		C.Check( DistortedCount == 0 && LegacyCount > 0, "allzero経路は旧MaterialRibbon命令を保つ (kind=%d distorted=%d legacy=%d)", static_cast<int32>( Kinds[Index] ), DistortedCount, LegacyCount );
		Playback.Reset();
		CollectRibbonTextures( Assets );
	}

	// noiseがpendingのまま開始した再生は、後からreadyになっても帯全体を記録しない。
	// async load fixtureのmain画像とnoise画像のpath。
	const FString PendingMainPath = "ribbon-uv-resource/pending-main.bmp";
	const FString PendingNoisePath = "ribbon-uv-resource/pending-noise.bmp";
	// pending動作確認用の画像保存状態。
	const bool bPendingFixtures = WriteRibbonTextureFixture( Dir, PendingMainPath ) && WriteRibbonTextureFixture( Dir, PendingNoisePath );
	C.Check( bPendingFixtures, "pending test用fixtureを保存する" );
	if ( bPendingFixtures )
	{
		// factory前に非同期loadし、readyでないnoise textureを作る。
		TSharedPtr<CTexture> PendingTexture = Assets.LoadTexture( PendingNoisePath, true );
		// ready判定と解放を追跡するnoise handle。
		const int32 PendingHandle = PendingTexture.IsValid() ? PendingTexture->GetHandle() : -1;
		// owner解放だけを調べるnoise弱参照。
		TWeakPtr<CTexture> PendingWeak = PendingTexture;
		// pending noiseを使うRibbon設定。
		const FParticleEmitterDefinition PendingEmitter = MakeRibbonResourceEmitter( PendingMainPath, PendingNoisePath, FEffectPathDefinition::EKind::Ribbon, true );
		// pending状態を固定するfactory再生。
		TSharedPtr<IEffectPlayback> PendingPlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { PendingEmitter }, 0.0f, true, 920u ), &Assets, FEffectPlayParams(), FTransform::Identity, "ribbon-uv-distortion-pending" );
		C.Check( PendingTexture.IsValid() && !PendingTexture->IsReady() && PendingPlayback.IsValid(), "pending noiseをready前に再生snapshotへ渡す" );
		if ( PendingPlayback.IsValid() ) { PendingPlayback->Update( 0.1f ); PendingPlayback->Update( 0.1f ); }
		// Record前のreadiness照会数とhandle数。
		const int32 QueriesBeforeRecord = Fake.NumLoadingQueries;
		const usize HandlesBeforeRecord = Fake.Textures.size();
		// pending状態で記録したcommand。
		const FDrawBuffer PendingBuffer = RecordRibbonResourcePlayback( PendingPlayback );
		C.Check( CountRibbonResourceCommands( PendingBuffer, EDrawCommand::DistortedMaterialRibbon3D ) == 0 && CountRibbonResourceCommands( PendingBuffer, EDrawCommand::MaterialRibbon3D ) == 0, "pending noiseは不完全な帯の部分記録も旧材質への置換もしない" );
		C.Check( Fake.NumLoadingQueries == QueriesBeforeRecord && Fake.Textures.size() == HandlesBeforeRecord, "pending帯のRecordはready再照会もresource作成もしない" );
		Fake.Loading.erase( PendingHandle );
		// pending状態のまま再記録した結果。
		const FDrawBuffer StillPendingBuffer = RecordRibbonResourcePlayback( PendingPlayback );
		C.Check( CountRibbonResourceCommands( StillPendingBuffer, EDrawCommand::DistortedMaterialRibbon3D ) == 0, "あとでnoiseがreadyになっても既存snapshotは採用しない" );
		PendingPlayback.Reset();
		PendingTexture.Reset();
		CollectRibbonTextures( Assets );
		C.Check( !PendingWeak.Pin().IsValid(), "pending noiseを使わないPlayback解放後にtexture ownerが残らない" );
	}
	for ( int32 Index = 1; Index < 3; ++Index )
	{
		// Lightning/Treeごとに分離したmain画像とnoise画像のpending path。
		const FString PendingMain = "ribbon-uv-resource/pending-main-" + FString::FromInt( Index ) + ".bmp";
		const FString PendingNoise = "ribbon-uv-resource/pending-noise-" + FString::FromInt( Index ) + ".bmp";
		// kind専用pending画像の保存状態。
		const bool bKindFixtures = WriteRibbonTextureFixture( Dir, PendingMain ) && WriteRibbonTextureFixture( Dir, PendingNoise );
		C.Check( bKindFixtures, "Lightning/Tree pending fixtureを保存する" );
		if ( !bKindFixtures ) continue;
		TSharedPtr<CTexture> PendingTexture = Assets.LoadTexture( PendingNoise, true );
		// pending状態を識別し、解放を確認するhandleとweak owner。
		const int32 PendingHandle = PendingTexture.IsValid() ? PendingTexture->GetHandle() : -1;
		TWeakPtr<CTexture> PendingWeak = PendingTexture;
		// kindごとのpending emitterと再生。
		const FParticleEmitterDefinition PendingEmitter = MakeRibbonResourceEmitter( PendingMain, PendingNoise, Kinds[Index], true );
		TSharedPtr<IEffectPlayback> PendingPlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { PendingEmitter }, 0.0f, true, 940u + static_cast<uint32>( Index ) ), &Assets, FEffectPlayParams(), FTransform::Identity, "ribbon-uv-distortion-pending-kind" );
		if ( PendingPlayback.IsValid() ) { PendingPlayback->Update( 0.1f ); PendingPlayback->Update( 0.1f ); }
		// kind別pending playbackの記録結果。
		const FDrawBuffer PendingBuffer = RecordRibbonResourcePlayback( PendingPlayback );
		C.Check( PendingTexture.IsValid() && !PendingTexture->IsReady() && PendingPlayback.IsValid() && CountRibbonResourceCommands( PendingBuffer, EDrawCommand::DistortedMaterialRibbon3D ) == 0 && CountRibbonResourceCommands( PendingBuffer, EDrawCommand::MaterialRibbon3D ) == 0, "pending noiseはLightning/Treeの帯全体もskipする" );
		PendingPlayback.Reset();
		Fake.Loading.erase( PendingHandle );
		PendingTexture.Reset();
		CollectRibbonTextures( Assets );
		C.Check( !PendingWeak.Pin().IsValid(), "Lightning/Tree pending resourceはPlayback解放で手放す" );
	}

	// 公開constructorもRibbon用pending noiseを採用せず、外部owner解放後に保持しない。
	// direct constructor fixtureのmain画像とpending noise path。
	const FString DirectMainPath = "ribbon-uv-resource/direct-main.bmp";
	const FString DirectNoisePath = "ribbon-uv-resource/direct-pending.bmp";
	// 直接constructor用画像の保存状態。
	const bool bDirectFixtures = WriteRibbonTextureFixture( Dir, DirectMainPath ) && WriteRibbonTextureFixture( Dir, DirectNoisePath );
	C.Check( bDirectFixtures, "direct constructor用Ribbon fixtureを保存する" );
	if ( bDirectFixtures )
	{
		// 直接constructorへ渡すmainとpending noise。
		TSharedPtr<CTexture> MainTexture = Assets.LoadTexture( DirectMainPath, false );
		TSharedPtr<CTexture> PendingTexture = Assets.LoadTexture( DirectNoisePath, true );
		// pending noiseのowner解放確認用weak参照。
		TWeakPtr<CTexture> PendingWeak = PendingTexture;
		// constructor後にpending状態を解除するhandle。
		const int32 PendingHandle = PendingTexture.IsValid() ? PendingTexture->GetHandle() : -1;
		// constructor引数にするmain画像配列。
		TArray<TSharedPtr<CTexture>> MainTextures;
		MainTextures.Add( MainTexture );
		// mask slotを空にした配列。
		TArray<TSharedPtr<CTexture>> Masks;
		Masks.Add( TSharedPtr<CTexture>() );
		// flipbook未使用を示す配列。
		TArray<FEffectFlipbookFrames> Frames;
		Frames.Add( FEffectFlipbookFrames() );
		// constructorへ渡すdistortion noise配列。
		TArray<TSharedPtr<CTexture>> DistortionTextures;
		DistortionTextures.Add( PendingTexture );
		// Ribbon設定を含むimmutable effect definition。
		const TSharedPtr<FParticleEffectDefinition> Definition = NEffectTest::MakeEffect( { MakeRibbonResourceEmitter( DirectMainPath, DirectNoisePath, FEffectPathDefinition::EKind::Ribbon, true ) } );
		// 公開7引数constructorの結果。
		TSharedPtr<FParticlePlayback> DirectPlayback;
		if ( Definition.IsValid() && MainTexture.IsValid() && PendingTexture.IsValid() ) DirectPlayback = MakeShared<FParticlePlayback>( Definition, MainTextures, FEffectPlayParams(), FTransform::Identity, Masks, Frames, DistortionTextures );
		DistortionTextures.Reset();
		PendingTexture.Reset();
		Fake.Loading.erase( PendingHandle );
		C.Check( DirectPlayback.IsValid() && !PendingWeak.Pin().IsValid(), "7引数constructorはRibbonのpending noiseを保持せず拒否する" );
		if ( DirectPlayback.IsValid() ) { DirectPlayback->Update( 0.1f ); DirectPlayback->Update( 0.1f ); }
		// direct constructor再生が記録したcommand。
		const FDrawBuffer DirectBuffer = RecordRibbonResourcePlayback( DirectPlayback );
		C.Check( CountRibbonResourceCommands( DirectBuffer, EDrawCommand::DistortedMaterialRibbon3D ) == 0 && CountRibbonResourceCommands( DirectBuffer, EDrawCommand::MaterialRibbon3D ) == 0, "direct constructorのpending Ribbonは通常材質へfallbackしない" );
		DirectPlayback.Reset();
		MainTexture.Reset();
		MainTextures.Reset();
		CollectRibbonTextures( Assets );
		C.Check( !PendingWeak.Pin().IsValid(), "direct Ribbon constructor解放後にpending noise ownerが残らない" );
	}
	for ( int32 Index = 1; Index < 3; ++Index )
	{
		// Lightning/Tree direct constructor用main画像とpending noise path。
		const FString DirectMainPath = "ribbon-uv-resource/direct-main-" + FString::FromInt( Index ) + ".bmp";
		const FString DirectNoisePath = "ribbon-uv-resource/direct-pending-" + FString::FromInt( Index ) + ".bmp";
		// kind別direct constructor fixtureの保存状態。
		const bool bKindFixtures = WriteRibbonTextureFixture( Dir, DirectMainPath ) && WriteRibbonTextureFixture( Dir, DirectNoisePath );
		C.Check( bKindFixtures, "Lightning/Tree direct fixtureを保存する" );
		if ( !bKindFixtures ) continue;
		// 直接constructorへ渡すLightning/Treeのmainとpending noise。
		TSharedPtr<CTexture> MainTexture = Assets.LoadTexture( DirectMainPath, false );
		TSharedPtr<CTexture> PendingTexture = Assets.LoadTexture( DirectNoisePath, true );
		// pending noiseのowner解放確認用weak参照。
		TWeakPtr<CTexture> PendingWeak = PendingTexture;
		// constructor後にpending状態を解除するhandle。
		const int32 PendingHandle = PendingTexture.IsValid() ? PendingTexture->GetHandle() : -1;
		// constructor引数にするmain画像配列。
		TArray<TSharedPtr<CTexture>> MainTextures;
		MainTextures.Add( MainTexture );
		// mask slotを空にした配列。
		TArray<TSharedPtr<CTexture>> Masks;
		Masks.Add( nullptr );
		// flipbook未使用を示す配列。
		TArray<FEffectFlipbookFrames> Frames;
		Frames.Add( FEffectFlipbookFrames() );
		// constructorへ渡すdistortion noise配列。
		TArray<TSharedPtr<CTexture>> DistortionTextures;
		DistortionTextures.Add( PendingTexture );
		// 対象kindを保持するimmutable effect definition。
		const TSharedPtr<FParticleEffectDefinition> Definition = NEffectTest::MakeEffect( { MakeRibbonResourceEmitter( DirectMainPath, DirectNoisePath, Kinds[Index], true ) } );
		// 公開7引数constructorのkind別結果。
		TSharedPtr<FParticlePlayback> DirectPlayback;
		if ( Definition.IsValid() && MainTexture.IsValid() && PendingTexture.IsValid() ) DirectPlayback = MakeShared<FParticlePlayback>( Definition, MainTextures, FEffectPlayParams(), FTransform::Identity, Masks, Frames, DistortionTextures );
		DistortionTextures.Reset();
		PendingTexture.Reset();
		Fake.Loading.erase( PendingHandle );
		C.Check( DirectPlayback.IsValid() && !PendingWeak.Pin().IsValid(), "7引数constructorはLightning/Treeのpending noiseも保持しない" );
		if ( DirectPlayback.IsValid() ) { DirectPlayback->Update( 0.1f ); DirectPlayback->Update( 0.1f ); }
		// kind別direct constructor再生の記録結果。
		const FDrawBuffer DirectBuffer = RecordRibbonResourcePlayback( DirectPlayback );
		C.Check( CountRibbonResourceCommands( DirectBuffer, EDrawCommand::DistortedMaterialRibbon3D ) == 0 && CountRibbonResourceCommands( DirectBuffer, EDrawCommand::MaterialRibbon3D ) == 0, "direct Lightning/Tree pending snapshotは旧材質へfallbackしない" );
		DirectPlayback.Reset();
		MainTexture.Reset();
		MainTextures.Reset();
		CollectRibbonTextures( Assets );
		C.Check( !PendingWeak.Pin().IsValid(), "direct Lightning/Tree pending ownerを解放する" );
	}

	// Ready noiseは複数経路Playbackで共有され、最後のownerまで残る。
	// 共有texture用main画像とnoise画像のpath。
	const FString SharedMainPath = "ribbon-uv-resource/shared-main.bmp";
	const FString SharedNoisePath = "ribbon-uv-resource/shared-noise.bmp";
	// shared ownerの生存試験に使う画像保存状態。
	const bool bSharedFixtures = WriteRibbonTextureFixture( Dir, SharedMainPath ) && WriteRibbonTextureFixture( Dir, SharedNoisePath );
	C.Check( bSharedFixtures, "shared noise test用fixtureを保存する" );
	if ( bSharedFixtures )
	{
		// managerから共有されるnoise参照と弱参照。
		TSharedPtr<CTexture> SharedTexture = Assets.LoadTexture( SharedNoisePath, false );
		TWeakPtr<CTexture> SharedWeak = SharedTexture;
		// playback間で共有するnoiseのhandle。
		const int32 SharedHandle = SharedTexture.IsValid() ? SharedTexture->GetHandle() : -1;
		// 共通のRibbon emitter設定。
		const FParticleEmitterDefinition SharedEmitter = MakeRibbonResourceEmitter( SharedMainPath, SharedNoisePath, FEffectPathDefinition::EKind::Ribbon, true );
		// 同一noiseを共有する先行Playback。
		TSharedPtr<IEffectPlayback> First = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { SharedEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "ribbon-uv-distortion-shared-a" );
		// 同一noiseを共有する後続Playback。
		TSharedPtr<IEffectPlayback> Second = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { SharedEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "ribbon-uv-distortion-shared-b" );
		C.Check( SharedHandle >= 0 && First.IsValid() && Second.IsValid(), "ready noiseを別の二Playbackへ共有する" );
		if ( First.IsValid() ) { First->Update( 0.1f ); First->Update( 0.1f ); }
		if ( Second.IsValid() ) { Second->Update( 0.1f ); Second->Update( 0.1f ); }
		SharedTexture.Reset();
		C.Check( CountRibbonResourceCommands( RecordRibbonResourcePlayback( First ), EDrawCommand::DistortedMaterialRibbon3D ) > 0 && SharedWeak.Pin().IsValid(), "最初のPlaybackが共有noiseを保持し帯を記録する" );
		if ( First.IsValid() ) First->Stop( true );
		First.Reset();
		C.Check( CountRibbonResourceCommands( RecordRibbonResourcePlayback( Second ), EDrawCommand::DistortedMaterialRibbon3D ) > 0 && SharedWeak.Pin().IsValid(), "一方を停止しても二番目のPlaybackとnoiseが有効" );
		Second.Reset();
		CollectRibbonTextures( Assets );
		C.Check( !SharedWeak.Pin().IsValid() && Fake.Textures.count( SharedHandle ) == 0, "最後の帯Playback終了で共有noise handleを解放" );
	}

	// test専用main/noise resourceがfake backendへ残らない。
	Assets.ReleaseUnused();
	Assets.Tick( 0.0f );
	// 初期状態に存在しなかったhandleが残らない結果。
	bool bNoNewTexturesRemain = true;
	for ( int32 Handle : Fake.Textures ) if ( InitialTextures.count( Handle ) == 0 ) bNoNewTexturesRemain = false;
	C.Check( bNoNewTexturesRemain, "Ribbon distortion resource tests後に新しいtexture handleを残さない" );
	Assets.SetRootDirectory( PreviousRoot );
}
