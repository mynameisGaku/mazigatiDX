// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"

#include <set>
#include <vector>

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Effect/ParticleEffectBackend.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Core/Interfaces/Render/DrawDistortedMaterialQuad.h"
#include "Library/Platform/FileSystem.h"

namespace
{
	// fake backendが拡張子を必要としない画像fixtureを保存する。
	bool WriteTextureFixture( const FString& Dir, const FString& RelativePath )
	{
		// 既存asset test helperが作る小さな有効BMP。
		const std::vector<uint8> Image = NAssetTest::MakeSheetBmp();
		return NFileSystem::EnsureDirectory( Dir / "uv-distortion-resource" ) && NAssetTest::WriteFileBytes( Dir / RelativePath, Image );
	}

	// Playbackが記録したゆがみ材質commandを値として集める。
	TArray<FDrawDistortedMaterialQuadData> ReadDistortedQuads( const FDrawBuffer& Buffer )
	{
		// 記録順に複写したpayload。
		TArray<FDrawDistortedMaterialQuadData> Result;
		if ( Buffer.Words.empty() ) return Result;
		// 型とpayload長が一致するcommandだけを読む。
		const auto ReadCommand = [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::DistortedMaterialQuad3D && Command.NumDataWords == static_cast<int32>( sizeof( FDrawDistortedMaterialQuadData ) / 4 ) ) Result.Add( Command.Read<FDrawDistortedMaterialQuadData>() ); };
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, ReadCommand );
		return Result;
	}

	// command列に含まれる指定型の数を数える。
	int32 CountCommands( const FDrawBuffer& Buffer, EDrawCommand Type )
	{
		// 見つけたcommand数。
		int32 Count = 0;
		if ( Buffer.Words.empty() ) return Count;
		// command種別だけを調べる走査処理。
		const auto CountCommand = [&]( const FDrawCommandView& Command ) { if ( Command.Type == Type ) ++Count; };
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, CountCommand );
		return Count;
	}

	// UV歪み付き四角形を再生する小さなemitterを作る。
	FParticleEmitterDefinition MakeEmitter( const FString& MainTexture, const FString& DistortionTexture )
	{
		// 既定の速度・色を持つ単一burst emitter。
		FParticleEmitterDefinition Emitter = NEffectTest::QuietEmitter();
		Emitter.Texture = MainTexture;
		Emitter.Lifetime = { 2.0f, 2.0f };
		Emitter.Bursts.Add( NEffectTest::Burst( 0.0f, 1 ) );
		Emitter.Material.bEnabled = true;
		Emitter.Material.Distortion.bEnabled = true;
		Emitter.Material.Distortion.Texture = DistortionTexture;
		return Emitter;
	}

	// 既存のfake textureだけが残った状態か調べる。
	bool HasOnlyTextures( const NAssetTest::FFakeAssetBackend& Fake, const std::set<int32>& Initial )
	{
		for ( int32 Handle : Fake.Textures ) if ( Initial.count( Handle ) == 0 ) return false;
		return true;
	}

	// fake textureの遅延解放を完了させる。
	void CollectTextures( CAssetManager& Assets )
	{
		Assets.ReleaseUnused();
		Assets.Tick( 0.0f );
	}
}

// ゆがみ画像を再生開始時に固定し、共有寿命と非対応経路を確認する。
void NWorldTest::RunEffectUVDistortionResourceTests( NAssetTest::FChecker& C, CAssetManager& Assets, NAssetTest::FFakeAssetBackend& Fake, const FString& Dir )
{
	C.Group = "effect-uv-distortion-resources";
	// 呼び出し元が使っていたasset root。
	const FString PreviousRoot = Assets.GetRootDirectory();
	Assets.SetRootDirectory( Dir );
	// main画像とnoise画像のroot相対path。
	const FString MainPath = "uv-distortion-resource/main.bmp";
	const FString NoisePath = "uv-distortion-resource/noise.bmp";
	// 二つの画像fixtureを保存できたか。
	const bool bFixturesSaved = WriteTextureFixture( Dir, MainPath ) && WriteTextureFixture( Dir, NoisePath );
	C.Check( bFixturesSaved, "main/noise画像をsandboxへ保存する" );
	if ( !bFixturesSaved ) { Assets.SetRootDirectory( PreviousRoot ); return; }
	// test開始時に既に所有されていたfake texture handle。
	const std::set<int32> InitialTextures = Fake.Textures;

	// pending noiseは生成時に固定拒否され、後でreadyになっても既存再生へ戻らない。
	// 非同期読込中のnoise画像。
	TSharedPtr<CTexture> PendingTexture = Assets.LoadTexture( NoisePath, true );
	C.Check( PendingTexture.IsValid() && !PendingTexture->IsReady(), "再生前のnoise画像をpending状態にする" );
	// pending noiseのbackend handle。
	const int32 PendingHandle = PendingTexture.IsValid() ? PendingTexture->GetHandle() : -1;
	// Playback所有後も生存を観測する弱参照。
	TWeakPtr<CTexture> PendingWeak = PendingTexture;
	// pending noiseで作る再生。
	TSharedPtr<IEffectPlayback> PendingPlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { MakeEmitter( MainPath, NoisePath ) } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-pending" );
	C.Check( PendingPlayback.IsValid(), "pending noiseでも通常Playbackを作る" );
	if ( PendingPlayback.IsValid() ) PendingPlayback->Update( 0.01f );
	// Record直前のready問い合わせ数。
	const int32 PendingQueriesBeforeRecord = Fake.NumLoadingQueries;
	// Record直前の生存texture handle数。
	const usize PendingHandlesBeforeRecord = Fake.Textures.size();
	// pending画像を記録した出力。
	FDrawBuffer PendingBuffer;
	// pending出力へrecordする描画窓口。
	FDrawContext PendingDraw( PendingBuffer );
	if ( PendingPlayback.IsValid() ) PendingPlayback->Record( PendingDraw );
	C.Check( PendingPlayback.IsValid() && CountCommands( PendingBuffer, EDrawCommand::DistortedMaterialQuad3D ) == 0 && CountCommands( PendingBuffer, EDrawCommand::MaterialQuad3D ) == 0, "再生開始時にpendingだったnoiseは通常画像へ戻さず描画しない" );
	C.Check( Fake.NumLoadingQueries == PendingQueriesBeforeRecord && Fake.Textures.size() == PendingHandlesBeforeRecord, "Recordは画像readyを再照会せずresourceも作らない" );
	Fake.Loading.erase( PendingHandle );
	// 後からreadyになったnoiseを既存再生へ渡さない出力。
	FDrawBuffer StillPendingBuffer;
	// ready後の既存再生を記録する窓口。
	FDrawContext StillPendingDraw( StillPendingBuffer );
	if ( PendingPlayback.IsValid() ) PendingPlayback->Record( StillPendingDraw );
	C.Check( CountCommands( StillPendingBuffer, EDrawCommand::DistortedMaterialQuad3D ) == 0, "後からreadyになったnoiseも既存Playbackでは採用しない" );
	PendingPlayback.Reset();
	PendingTexture.Reset();
	CollectTextures( Assets );
	C.Check( !PendingWeak.Pin().IsValid(), "pending拒否後にnoiseの外部ownerが無ければ解放する" );

	// 公開7引数constructorもpending画像を再生開始時点で固定拒否する。
	// direct constructor用noise画像path。
	const FString DirectNoisePath = "uv-distortion-resource/direct-pending.bmp";
	// noise fixtureを保存できたか。
	const bool bDirectFixtureSaved = WriteTextureFixture( Dir, DirectNoisePath );
	C.Check( bDirectFixtureSaved, "direct constructor用noise画像fixtureを保存する" );
	// 再生本体に渡す同期済み画像。
	TSharedPtr<CTexture> DirectMainTexture = Assets.LoadTexture( MainPath, false );
	// 再生開始前に読み込み中のnoise画像。
	TSharedPtr<CTexture> DirectPendingTexture = Assets.LoadTexture( DirectNoisePath, true );
	// constructorが所有を残さないことを調べる弱参照。
	TWeakPtr<CTexture> DirectPendingWeak = DirectPendingTexture;
	// loading集合から消すnoise handle。
	const int32 DirectPendingHandle = DirectPendingTexture.IsValid() ? DirectPendingTexture->GetHandle() : -1;
	// constructorへ渡すmain画像配列。
	TArray<TSharedPtr<CTexture>> DirectTextures;
	DirectTextures.Add( DirectMainTexture );
	// constructorへ渡す空の材質mask配列。
	TArray<TSharedPtr<CTexture>> DirectMasks;
	DirectMasks.Add( TSharedPtr<CTexture>() );
	// constructorへ渡す空のflipbook配列。
	TArray<FEffectFlipbookFrames> DirectFrames;
	DirectFrames.Add( FEffectFlipbookFrames() );
	// constructorへ渡すpending noise配列。
	TArray<TSharedPtr<CTexture>> DirectDistortionTextures;
	DirectDistortionTextures.Add( DirectPendingTexture );
	// direct constructorで使う粒子定義。
	TSharedPtr<FParticleEffectDefinition> DirectDefinition = NEffectTest::MakeEffect( { MakeEmitter( MainPath, DirectNoisePath ) } );
	// public constructorを直接呼ぶ再生。
	TSharedPtr<FParticlePlayback> DirectPlayback;
	if ( DirectDefinition.IsValid() && DirectMainTexture.IsValid() && DirectPendingTexture.IsValid() ) DirectPlayback = MakeShared<FParticlePlayback>( DirectDefinition, DirectTextures, FEffectPlayParams(), FTransform::Identity, DirectMasks, DirectFrames, DirectDistortionTextures );
	DirectDistortionTextures.Reset();
	DirectPendingTexture.Reset();
	Fake.Loading.erase( DirectPendingHandle );
	C.Check( DirectPlayback.IsValid() && !DirectPendingWeak.Pin().IsValid(), "7引数constructorはpending noiseを保持せず拒否する" );
	if ( DirectPlayback.IsValid() ) DirectPlayback->Update( 0.01f );
	// pending除外後に既存再生が記録するcommand列。
	FDrawBuffer DirectPendingBuffer;
	// direct constructor出力を受け取る描画窓口。
	FDrawContext DirectPendingDraw( DirectPendingBuffer );
	if ( DirectPlayback.IsValid() ) DirectPlayback->Record( DirectPendingDraw );
	C.Check( CountCommands( DirectPendingBuffer, EDrawCommand::DistortedMaterialQuad3D ) == 0 && CountCommands( DirectPendingBuffer, EDrawCommand::MaterialQuad3D ) == 0, "constructor後にreadyになったnoiseを既存Playbackへ採用しない" );
	DirectPlayback.Reset();
	DirectMainTexture.Reset();
	DirectDefinition.Reset();
	// constructor fixtureのmain画像配列に残った強参照を解放する。
	DirectTextures.Reset();
	CollectTextures( Assets );
	C.Check( !DirectPendingWeak.Pin().IsValid(), "direct constructorのpending拒否後にnoiseを解放する" );

	// ready noiseは複数再生に共有され、片方停止後ももう片方が描く。
	// cacheから共有する同期済みnoise画像。
	TSharedPtr<CTexture> SharedTexture = Assets.LoadTexture( NoisePath, false );
	// owner解放後の生存を調べる弱参照。
	TWeakPtr<CTexture> SharedWeak = SharedTexture;
	// 二つの再生へ渡すnoise handle。
	const int32 SharedHandle = SharedTexture.IsValid() ? SharedTexture->GetHandle() : -1;
	// 最初の独立再生。
	TSharedPtr<IEffectPlayback> FirstPlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { MakeEmitter( MainPath, NoisePath ) } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-shared-first" );
	// 二つ目の独立再生。
	TSharedPtr<IEffectPlayback> SecondPlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { MakeEmitter( MainPath, NoisePath ) } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-shared-second" );
	C.Check( FirstPlayback.IsValid() && SecondPlayback.IsValid() && SharedHandle >= 0, "ready noiseを二つの独立Playbackへ共有する" );
	if ( FirstPlayback.IsValid() ) FirstPlayback->Update( 0.01f );
	if ( SecondPlayback.IsValid() ) SecondPlayback->Update( 0.01f );
	SharedTexture.Reset();
	// ready resourceを作らず記録したか調べる基準handle数。
	const usize ReadyHandlesBeforeRecord = Fake.Textures.size();
	// 最初のPlayback command列。
	FDrawBuffer FirstBuffer;
	// 最初のcommand列を受け取る描画窓口。
	FDrawContext FirstDraw( FirstBuffer );
	if ( FirstPlayback.IsValid() ) FirstPlayback->Record( FirstDraw );
	// 最初の記録から読んだゆがみ材質payload。
	const TArray<FDrawDistortedMaterialQuadData> FirstCommands = ReadDistortedQuads( FirstBuffer );
	C.Check( FirstCommands.Num() == 1 && FirstCommands[0].DistortionTexture == SharedHandle && SharedWeak.Pin().IsValid(), "再生が共有noise handleを保持してゆがみ命令を記録する" );
	C.Check( Fake.Textures.size() == ReadyHandlesBeforeRecord, "ready時のRecordもresource handleを作らない" );
	if ( FirstPlayback.IsValid() ) FirstPlayback->Stop( true );
	FirstPlayback.Reset();
	// 一つ目を停止後に記録する二つ目のcommand列。
	FDrawBuffer SecondBuffer;
	// 二つ目のcommand列を受け取る描画窓口。
	FDrawContext SecondDraw( SecondBuffer );
	if ( SecondPlayback.IsValid() ) SecondPlayback->Record( SecondDraw );
	// 二つ目の記録から読んだゆがみ材質payload。
	const TArray<FDrawDistortedMaterialQuadData> SecondCommands = ReadDistortedQuads( SecondBuffer );
	C.Check( SecondCommands.Num() == 1 && SecondCommands[0].DistortionTexture == SharedHandle && SharedWeak.Pin().IsValid(), "別Playbackを停止・解放しても共有noiseの再生とresourceを維持する" );
	SecondPlayback.Reset();
	CollectTextures( Assets );
	C.Check( !SharedWeak.Pin().IsValid() && Fake.Textures.count( SharedHandle ) == 0, "最後のPlayback解放後に共有noise handleを解放する" );

	// noiseが欠落した正強度材質は描画を飛ばし、zero強度は通常材質を保つ。
	// missing noiseを参照する正強度再生。
	TSharedPtr<IEffectPlayback> MissingPlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { MakeEmitter( MainPath, "uv-distortion-resource/missing.bmp" ) } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-missing" );
	C.Check( MissingPlayback.IsValid(), "noise欠落時にもPlayback objectを作る" );
	if ( MissingPlayback.IsValid() ) MissingPlayback->Update( 0.01f );
	// missing noise時のrecord結果。
	FDrawBuffer MissingBuffer;
	// missing出力へ記録する描画窓口。
	FDrawContext MissingDraw( MissingBuffer );
	if ( MissingPlayback.IsValid() ) MissingPlayback->Record( MissingDraw );
	C.Check( CountCommands( MissingBuffer, EDrawCommand::DistortedMaterialQuad3D ) == 0 && CountCommands( MissingBuffer, EDrawCommand::MaterialQuad3D ) == 0, "正強度でnoise欠落時に通常画像へ置き換えず描画を飛ばす" );
	MissingPlayback.Reset();
	// backendが実在画像でもInvalidHandleを返すときのmain画像path。
	const FString FailedMainPath = "uv-distortion-resource/failing-main.bmp";
	// backend失敗fixtureを保存できたか。
	const bool bFailedFixtureSaved = WriteTextureFixture( Dir, FailedMainPath );
	C.Check( bFailedFixtureSaved, "backend失敗用main画像fixtureを保存する" );
	// 呼び出し元が指定していたtexture failure番号。
	const int32 PreviousTextureFailure = Fake.FailTextureLoadCall;
	Fake.FailTextureLoadCall = Fake.NumTextureLoads + 2;
	// noise handleだけを読み込み失敗させる再生。
	TSharedPtr<IEffectPlayback> InvalidHandlePlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { MakeEmitter( FailedMainPath, NoisePath ) } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-invalid-handle" );
	Fake.FailTextureLoadCall = PreviousTextureFailure;
	C.Check( bFailedFixtureSaved && InvalidHandlePlayback.IsValid(), "noiseのInvalidHandleでも通常Playbackを作る" );
	if ( InvalidHandlePlayback.IsValid() ) InvalidHandlePlayback->Update( 0.01f );
	// InvalidHandle時に記録されたcommand列。
	FDrawBuffer InvalidHandleBuffer;
	// InvalidHandle出力を受け取る描画窓口。
	FDrawContext InvalidHandleDraw( InvalidHandleBuffer );
	if ( InvalidHandlePlayback.IsValid() ) InvalidHandlePlayback->Record( InvalidHandleDraw );
	C.Check( CountCommands( InvalidHandleBuffer, EDrawCommand::DistortedMaterialQuad3D ) == 0 && CountCommands( InvalidHandleBuffer, EDrawCommand::MaterialQuad3D ) == 0, "backendのInvalidHandleを返すnoiseでも正強度粒子を描かない" );
	InvalidHandlePlayback.Reset();
	// 強度0では不要なnoiseを読まないemitter。
	FParticleEmitterDefinition ZeroEmitter = MakeEmitter( MainPath, "uv-distortion-resource/missing-zero.bmp" );
	ZeroEmitter.Material.Distortion.Strength.Keys.Add( { 0.0f, 0.0f } );
	// noise無しで従来材質を使う再生。
	TSharedPtr<IEffectPlayback> ZeroPlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { ZeroEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-zero" );
	C.Check( ZeroPlayback.IsValid(), "zero強度の欠落画像設定でもPlaybackを作る" );
	if ( ZeroPlayback.IsValid() ) ZeroPlayback->Update( 0.01f );
	// zero強度時に記録される従来材質命令。
	FDrawBuffer ZeroBuffer;
	// zero出力を受け取る描画窓口。
	FDrawContext ZeroDraw( ZeroBuffer );
	if ( ZeroPlayback.IsValid() ) ZeroPlayback->Record( ZeroDraw );
	C.Check( CountCommands( ZeroBuffer, EDrawCommand::MaterialQuad3D ) == 1 && CountCommands( ZeroBuffer, EDrawCommand::DistortedMaterialQuad3D ) == 0, "Strengthが全て0ならnoise無しの旧MaterialQuadを記録する" );
	ZeroPlayback.Reset();

	// disabledまたは帯経路ではnoiseを読み込まない。
	// disabled専用main画像path。
	const FString DisabledMainPath = "uv-distortion-resource/disabled-main.bmp";
	// disabled専用画像を用意できたか。
	const bool bDisabledFixtureSaved = WriteTextureFixture( Dir, DisabledMainPath );
	C.Check( bDisabledFixtureSaved, "disabled用main画像fixtureを保存する" );
	// disabled設定より前にbackendが読んだtexture数。
	const int32 LoadsBeforeDisabled = Fake.NumTextureLoads;
	// 材質は有効だが歪みだけを無効にしたemitter。
	FParticleEmitterDefinition DisabledEmitter = MakeEmitter( DisabledMainPath, NoisePath );
	DisabledEmitter.Material.Distortion.bEnabled = false;
	// noiseを読まずに開始する再生。
	TSharedPtr<IEffectPlayback> DisabledPlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { DisabledEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-disabled" );
	C.Check( DisabledPlayback.IsValid() && Fake.NumTextureLoads == LoadsBeforeDisabled + 1, "Distortion disabledではmain画像だけを読み込む" );
	DisabledPlayback.Reset();
	CollectTextures( Assets );
	const FString RibbonMainPath = "uv-distortion-resource/ribbon.bmp";
	// ribbon用画像を用意できたか。
	const bool bRibbonFixtureSaved = WriteTextureFixture( Dir, RibbonMainPath );
	C.Check( bRibbonFixtureSaved, "ribbon用main画像fixtureを保存する" );
	// ribbon設定より前にbackendが読んだtexture数。
	const int32 LoadsBeforeRibbon = Fake.NumTextureLoads;
	// 歪みが有効でも四角形でないemitter。
	FParticleEmitterDefinition RibbonEmitter = MakeEmitter( RibbonMainPath, NoisePath );
	RibbonEmitter.Path.Kind = FEffectPathDefinition::EKind::Ribbon;
	// 歪みnoiseを読まずに帯を開始する再生。
	TSharedPtr<IEffectPlayback> RibbonPlayback = NEffect::CreateParticlePlayback( NEffectTest::MakeEffect( { RibbonEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "uv-distortion-ribbon" );
	C.Check( RibbonPlayback.IsValid() && Fake.NumTextureLoads == LoadsBeforeRibbon + 1, "非四角形経路ではDistortion noiseを読み込まない" );
	RibbonPlayback.Reset();
	CollectTextures( Assets );
	C.Check( HasOnlyTextures( Fake, InitialTextures ), "UV distortion resource tests後に生成textureを残さない" );
	Assets.SetRootDirectory( PreviousRoot );
}
