// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"

#include <cstring>
#include <cmath>
#include <vector>

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Effect/EffectFlipbookFrames.h"
#include "Library/Framework/Effect/ParticleEffectBackend.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Platform/FileSystem.h"

namespace
{
	// 3色を横に並べた小さなatlasを、DxLibが読める無圧縮TGAへ変換する。
	std::vector<uint8> MakeFlipbookAtlasTga()
	{
		// TGAヘッダーと12x4x32bitのpixelを組み立てるbyte列。
		std::vector<uint8> Bytes = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 0, 4, 0, 32, 0x28 };
		for ( int32 Y = 0; Y < 4; ++Y ) for ( int32 X = 0; X < 12; ++X )
		{
			// コマごとに識別できる不透明な赤・緑・青。
			const uint8 Colors[3][3] = { { 240, 24, 16 }, { 16, 232, 32 }, { 24, 48, 240 } };
			const int32 Frame = X / 4;
			Bytes.push_back( Colors[Frame][2] ); Bytes.push_back( Colors[Frame][1] ); Bytes.push_back( Colors[Frame][0] ); Bytes.push_back( 255 );
		}
		return Bytes;
	}

	// Playbackが書いたQuad系命令から、実際に選ばれた画像ハンドルを返す。
	int32 ReadRecordedTexture( const FDrawBuffer& Buffer, EDrawCommand Expected )
	{
		// 指定した命令種別で最初に見つかった四角形のハンドル。
		int32 Handle = -1;
		// 命令種別に応じたpayloadからtexture handleを読む処理。
		const auto ReadTexture = [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type == Expected && Expected == EDrawCommand::Quad3D ) Handle = Command.Read<FDrawQuad3DData>().Handle;
			if ( Command.Type == Expected && Expected == EDrawCommand::MaterialQuad3D ) Handle = Command.Read<FDrawMaterialQuadData>().Quad.Handle;
		};
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, ReadTexture );
		return Handle;
	}

	// legacy QuadのUV範囲をbufferから値として取り出す。
	FDrawQuad3DData ReadRecordedQuad( const FDrawBuffer& Buffer )
	{
		// 見つからなかった場合の無効handleを持つ値。
		FDrawQuad3DData Result;
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::Quad3D ) Result = Command.Read<FDrawQuad3DData>(); } );
		return Result;
	}

	// 再生を変えず、現在の描画命令だけを所有bufferへ記録する。
	FDrawBuffer RecordFlipbookPlayback( const TSharedPtr<IEffectPlayback>& Playback )
	{
		// Playback一回分の命令を保持するbuffer。
		FDrawBuffer Buffer;
		// bufferへ書き込む描画窓口。
		FDrawContext Draw( Buffer );
		if ( Playback.IsValid() ) Playback->Record( Draw );
		return Buffer;
	}
}

// 実Playbackが粒子時計に応じた派生画像を選び、sprite以外と従来命令を保つことを確認する。
void NWorldTest::RunEffectFlipbookPlaybackTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir )
{
	using namespace NEffectTest;
	C.Group = "effect-flipbook-playback";
	// 呼出し元が使っていたアセットrootをあとで戻す。
	const FString PreviousRoot = Assets.GetRootDirectory();
	Assets.SetRootDirectory( Dir );
	// atlasとsheet定義をテスト専用rootへ置く。
	const FString AtlasPath = "flipbook/atlas.tga";
	const FString SheetPath = "flipbook/atlas.sheet.json";
	// 画像の親フォルダーが作れたか。
	const bool bDirectoryReady = NFileSystem::EnsureDirectory( Dir / "flipbook" );
	// atlas画像のRGBをコマごとに分ける入力byte列。
	const std::vector<uint8> AtlasBytes = MakeFlipbookAtlasTga();
	C.Check( bDirectoryReady && NAssetTest::WriteFileBytes( Dir / AtlasPath, AtlasBytes ), "赤緑青のflipbook atlasをsandboxへ保存する" );
	// 4x4の3コマを横に置き、異なる時間とLoopを持つclipを作る。
	FSpriteSheetDefinition SheetDefinition;
	SheetDefinition.TexturePath = AtlasPath;
	SheetDefinition.Frames.Add( { 0, 0, 4, 4, 2.0f, 2.0f } );
	SheetDefinition.Frames.Add( { 4, 0, 4, 4, 2.0f, 2.0f } );
	SheetDefinition.Frames.Add( { 8, 0, 4, 4, 2.0f, 2.0f } );
	FSpriteClip Clip = FSpriteClip::FromFrames( "Cycle", { 0, 1, 2 }, 10.0f, EAnimLoopMode::Loop );
	Clip.Durations = { 0.1f, 0.2f, 0.3f };
	SheetDefinition.Clips.Add( Clip );
	C.Check( Assets.SaveSpriteSheetDefinition( SheetPath, SheetDefinition ), "flipbook sheet metadataをsandboxへ保存する" );
	// sheetと親atlasを同期読込して、参照元を解放する前のtextureを確認する。
	TSharedPtr<CSpriteSheet> Sheet = Assets.LoadSpriteSheet( SheetPath, false );
	C.Check( Sheet.IsValid() && Sheet->IsReady() && Sheet->GetNumFrames() == 3, "3コマsheetを同期準備できる" );
	if ( !Sheet.IsValid() || !Sheet->IsReady() ) { Assets.SetRootDirectory( PreviousRoot ); return; }
	// 各矩形に対応する共有派生画像を、独立した期待値として準備する。
	TArray<TSharedPtr<CTexture>> ExpectedRegions;
	for ( int32 Index = 0; Index < 3; ++Index )
	{
		const FSpriteFrame* Frame = Sheet->GetFrame( Index );
		ExpectedRegions.Add( Frame != nullptr ? Assets.CreateTextureRegion( Sheet->GetTexture(), Frame->X, Frame->Y, Frame->Width, Frame->Height ) : TSharedPtr<CTexture>() );
	}
	C.Check( ExpectedRegions.Num() == 3 && ExpectedRegions[0].IsValid() && ExpectedRegions[1].IsValid() && ExpectedRegions[2].IsValid() && ExpectedRegions[0]->GetHandle() != ExpectedRegions[1]->GetHandle() && ExpectedRegions[1]->GetHandle() != ExpectedRegions[2]->GetHandle(), "異なるframe矩形はそれぞれ有効なregion handleを持つ" );
	if ( ExpectedRegions.Num() != 3 || !ExpectedRegions[0].IsValid() || !ExpectedRegions[1].IsValid() || !ExpectedRegions[2].IsValid() ) { Assets.SetRootDirectory( PreviousRoot ); return; }
	// 同じ親objectと矩形から作るregionはweak cacheで再共有する。
	TSharedPtr<CTexture> SharedRegion = Assets.CreateTextureRegion( Sheet->GetTexture(), 4, 0, 4, 4 );
	C.Check( SharedRegion.IsValid() && SharedRegion->GetHandle() == ExpectedRegions[1]->GetHandle(), "同じ親identityと矩形は派生画像handleを共有する" );
	// 派生画像のさらに一pixelを切り出し、base texture上のUV中央へ合成されることを確認する。
	TSharedPtr<CTexture> NestedRegion = Assets.CreateTextureRegion( ExpectedRegions[1], 1, 1, 1, 1 );
	// 作成失敗時も配列参照を無効handleから取らない比較用UV。
	float32 NestedUV[4] = {};
	// 成功時は子、失敗時は有効な親regionから参照する。
	const CTexture& NestedUvSource = NestedRegion.IsValid() ? *NestedRegion : *ExpectedRegions[1];
	const float32 ( &NestedSourceUV )[4] = NestedUvSource.GetPolygonUvBounds();
	for ( int32 Index = 0; Index < 4; ++Index ) NestedUV[Index] = NestedSourceUV[Index];
	C.Check( NestedRegion.IsValid() && NMath::Abs( NestedUV[0] - NestedUV[2] ) < 1.0e-6f && NMath::Abs( NestedUV[1] - NestedUV[3] ) < 1.0e-6f && NestedUV[0] > 0.45f && NestedUV[0] < 0.55f && NestedUV[1] > 0.2f && NestedUV[1] < 0.8f, "nestedの1pixel regionは元atlas上の該当pixel中央を指すUVを持つ" );
	// sheetに親textureが保持されている間の弱参照。
	TWeakPtr<CTexture> ParentWeak = Sheet->GetTexture();
	// 既存の画像pathを指定しても、有効flipbookではsheetのregionを使う。
	FParticleEmitterDefinition Emitter = QuietEmitter();
	Emitter.Texture = "ignored-by-flipbook.png";
	Emitter.Lifetime = { 1.0f, 1.0f };
	Emitter.Bursts.Add( Burst( 0.0f, 1 ) );
	Emitter.Flipbook.bEnabled = true;
	Emitter.Flipbook.Sheet = SheetPath;
	Emitter.Flipbook.Clip = "Cycle";
	Emitter.Flipbook.TimeMode = FEffectFlipbookDefinition::ETimeMode::Lifetime;
	Emitter.Material.bEnabled = false;
	// 同じsimulationの2時点で、T=.25は1段目、T=.75は2段目を選ぶ。
	TSharedPtr<IEffectPlayback> LifetimePlayback = NEffect::CreateParticlePlayback( MakeEffect( { Emitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "flipbook-lifetime" );
	C.Check( LifetimePlayback.IsValid(), "Lifetime flipbookのPlaybackを作れる" );
	if ( LifetimePlayback.IsValid() )
	{
		LifetimePlayback->Update( 0.01f );
		LifetimePlayback->Update( 0.25f );
		const FDrawBuffer MidBuffer = RecordFlipbookPlayback( LifetimePlayback );
		C.Check( ReadRecordedTexture( MidBuffer, EDrawCommand::Quad3D ) == ExpectedRegions[1]->GetHandle(), "寿命割合の中間時刻でclipの2段目regionを記録する" );
		const FDrawQuad3DData LegacyQuad = ReadRecordedQuad( MidBuffer );
		const float32 ( &ExpectedUV )[4] = ExpectedRegions[1]->GetPolygonUvBounds();
		C.Check( NMath::Abs( LegacyQuad.U0 - ExpectedUV[0] ) < 1.0e-6f && NMath::Abs( LegacyQuad.V0 - ExpectedUV[1] ) < 1.0e-6f && NMath::Abs( LegacyQuad.U1 - ExpectedUV[2] ) < 1.0e-6f && NMath::Abs( LegacyQuad.V1 - ExpectedUV[3] ) < 1.0e-6f, "legacy Quadへ派生領域のbase texture UVを記録する" );
		const int32 HandlesBeforeRecord = Assets.GetBackend()->GetMemoryInfo().NumTextureHandles;
		const FDrawBuffer RepeatBuffer = RecordFlipbookPlayback( LifetimePlayback );
		const int32 HandlesAfterRecord = Assets.GetBackend()->GetMemoryInfo().NumTextureHandles;
		C.Check( MidBuffer.Words == RepeatBuffer.Words && HandlesBeforeRecord == HandlesAfterRecord, "Recordを繰り返してもframe選択は同一で資源を作らない" );
		// sheetのclip順を編集しても、開始済みPlaybackは元のframe snapshotを使う。
		FSpriteSheetDefinition EditedSheet = SheetDefinition;
		EditedSheet.Clips[0].Frames = { 1, 2, 0 };
		Sheet->SetDefinition( EditedSheet );
		const FDrawBuffer AfterSheetEdit = RecordFlipbookPlayback( LifetimePlayback );
		C.Check( MidBuffer.Words == AfterSheetEdit.Words, "sheet.SetDefinition後も既存Playbackの時間軸とregion順は変わらない" );
		TSharedPtr<IEffectPlayback> NewSnapshotPlayback = NEffect::CreateParticlePlayback( MakeEffect( { Emitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "flipbook-sheet-edited" );
		if ( NewSnapshotPlayback.IsValid() ) { NewSnapshotPlayback->Update( 0.01f ); NewSnapshotPlayback->Update( 0.25f ); }
		C.Check( NewSnapshotPlayback.IsValid() && ReadRecordedTexture( RecordFlipbookPlayback( NewSnapshotPlayback ), EDrawCommand::Quad3D ) == ExpectedRegions[2]->GetHandle(), "sheet編集後に開始した新Playbackは新しいclip順を読む" );
		NewSnapshotPlayback.Reset();
		Sheet->SetDefinition( SheetDefinition );
	}
	// 保存した定義をfresh LoadEffectし、アセット参照を離しても再生が同じframeを選ぶ。
	const FString SavedEffectPath = "flipbook/roundtrip.effect.json";
	const TSharedPtr<FParticleEffectDefinition> RoundtripDefinition = MakeEffect( { Emitter } );
	// 再生定義をsandboxのJSONへ保存できたか。
	const bool bEffectSaved = Assets.SaveEffect( SavedEffectPath, *RoundtripDefinition );
	// diskから新しく読み込んだ定義asset。
	TSharedPtr<CEffectAsset> LoadedEffect = bEffectSaved ? Assets.LoadEffect( SavedEffectPath ) : TSharedPtr<CEffectAsset>();
	// 読み込んだ定義からframe資源を準備した独立Playback。
	TSharedPtr<IEffectPlayback> DiskPlayback = LoadedEffect.IsValid() ? NEffect::CreateParticlePlayback( LoadedEffect->GetDefinitionShared(), &Assets, FEffectPlayParams(), FTransform::Identity, "flipbook-disk-roundtrip" ) : TSharedPtr<IEffectPlayback>();
	LoadedEffect.Reset();
	if ( DiskPlayback.IsValid() ) { DiskPlayback->Update( 0.01f ); DiskPlayback->Update( 0.25f ); }
	const FDrawBuffer DiskBuffer = RecordFlipbookPlayback( DiskPlayback );
	C.Check( bEffectSaved && DiskPlayback.IsValid() && ReadRecordedTexture( DiskBuffer, EDrawCommand::Quad3D ) == ExpectedRegions[1]->GetHandle(), "保存済み定義をfresh readしてアセットを解放した後もframe選択を再生する" );
	// Seconds時計は元clipのLoopを尊重し、境界を越えた秒数を巻き戻す。
	FParticleEmitterDefinition SecondsEmitter = Emitter;
	SecondsEmitter.Flipbook.TimeMode = FEffectFlipbookDefinition::ETimeMode::Seconds;
	SecondsEmitter.Flipbook.Speed = 1.0f;
	TSharedPtr<IEffectPlayback> SecondsPlayback = NEffect::CreateParticlePlayback( MakeEffect( { SecondsEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "flipbook-seconds" );
	SecondsPlayback->Update( 0.01f );
	SecondsPlayback->Update( 0.75f );
	C.Check( ReadRecordedTexture( RecordFlipbookPlayback( SecondsPlayback ), EDrawCommand::Quad3D ) == ExpectedRegions[1]->GetHandle(), "Seconds時計はloop境界後のclip段を選ぶ" );
	// OnceとPingPongで元clipの終端・復路step sourceを区別する。
	FSpriteSheetDefinition OnceSheet = SheetDefinition;
	OnceSheet.Clips[0].Loop = EAnimLoopMode::Once;
	const FString OnceSheetPath = "flipbook/once.sheet.json";
	C.Check( Assets.SaveSpriteSheetDefinition( OnceSheetPath, OnceSheet ), "Seconds Once clipを保存する" );
	FParticleEmitterDefinition OnceEmitter = SecondsEmitter;
	OnceEmitter.Flipbook.Sheet = OnceSheetPath;
	TSharedPtr<IEffectPlayback> OncePlayback = NEffect::CreateParticlePlayback( MakeEffect( { OnceEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "flipbook-seconds-once" );
	OncePlayback->Update( 0.01f ); OncePlayback->Update( 0.75f );
	C.Check( ReadRecordedTexture( RecordFlipbookPlayback( OncePlayback ), EDrawCommand::Quad3D ) == ExpectedRegions[2]->GetHandle(), "Seconds Onceは最終frameに留まる" );
	FSpriteSheetDefinition PingPongSheet = SheetDefinition;
	PingPongSheet.Clips[0].Loop = EAnimLoopMode::PingPong;
	const FString PingPongSheetPath = "flipbook/pingpong.sheet.json";
	C.Check( Assets.SaveSpriteSheetDefinition( PingPongSheetPath, PingPongSheet ), "Seconds PingPong clipを保存する" );
	FParticleEmitterDefinition PingPongEmitter = SecondsEmitter;
	PingPongEmitter.Flipbook.Sheet = PingPongSheetPath;
	TSharedPtr<IEffectPlayback> PingPongPlayback = NEffect::CreateParticlePlayback( MakeEffect( { PingPongEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "flipbook-seconds-pingpong" );
	PingPongPlayback->Update( 0.01f ); PingPongPlayback->Update( 0.65f );
	C.Check( ReadRecordedTexture( RecordFlipbookPlayback( PingPongPlayback ), EDrawCommand::Quad3D ) == ExpectedRegions[1]->GetHandle(), "Seconds PingPongは復路step sourceへ戻る" );
	// 異なるAgeの二粒子は同じPlayback内でも別region handleを同時に選べる。
	FParticleEmitterDefinition MultiEmitter = Emitter;
	MultiEmitter.Rate = 2.0f;
	TSharedPtr<IEffectPlayback> MultiPlayback = NEffect::CreateParticlePlayback( MakeEffect( { MultiEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "flipbook-multiple-ages" );
	MultiPlayback->Update( 0.01f ); MultiPlayback->Update( 0.5f );
	const FDrawBuffer MultiBuffer = RecordFlipbookPlayback( MultiPlayback );
	bool bHasFirstFrame = false, bHasLastFrame = false;
	ForEachDrawCommand( FDrawSpan{ MultiBuffer.Words.data(), static_cast<int32>( MultiBuffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::Quad3D ) { const int32 Handle = Command.Read<FDrawQuad3DData>().Handle; bHasFirstFrame |= Handle == ExpectedRegions[0]->GetHandle(); bHasLastFrame |= Handle == ExpectedRegions[2]->GetHandle(); } } );
	C.Check( bHasFirstFrame && bHasLastFrame, "同じシートの異なるAge粒子は別のframe handleを選ぶ" );
	// 材質の有効化はframe handleを変えず、MaterialQuadへ同じregionを渡す。
	FParticleEmitterDefinition MaterialEmitter = Emitter;
	MaterialEmitter.Material.bEnabled = true;
	TSharedPtr<IEffectPlayback> MaterialPlayback = NEffect::CreateParticlePlayback( MakeEffect( { MaterialEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "flipbook-material" );
	MaterialPlayback->Update( 0.01f);
	MaterialPlayback->Update( 0.25f );
	const FDrawBuffer MaterialBuffer = RecordFlipbookPlayback( MaterialPlayback );
	C.Check( ReadRecordedTexture( MaterialBuffer, EDrawCommand::MaterialQuad3D ) == ExpectedRegions[1]->GetHandle(), "材質有効時も同じ粒子時刻のregion handleを渡す" );
	// 一方の再生を停止しても、同じsnapshotを持つ別再生のframe選択は変わらない。
	TSharedPtr<IEffectPlayback> IndependentPlayback = NEffect::CreateParticlePlayback( MakeEffect( { Emitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "flipbook-independent" );
	if ( IndependentPlayback.IsValid() ) { IndependentPlayback->Update( 0.01f ); IndependentPlayback->Update( 0.25f ); }
	// 停止前に別Playbackが持つframe命令を写す。
	const FDrawBuffer IndependentBeforeStop = RecordFlipbookPlayback( IndependentPlayback );
	if ( LifetimePlayback.IsValid() ) LifetimePlayback->Stop( true );
	C.Check( !LifetimePlayback.IsValid() || LifetimePlayback->GetNumParticles() == 0, "即時停止したPlaybackは粒子を解放する" );
	LifetimePlayback.Reset();
	// 片方を解放した後に残るPlaybackのframe命令を再取得する。
	const FDrawBuffer IndependentAfterStop = RecordFlipbookPlayback( IndependentPlayback );
	C.Check( IndependentPlayback.IsValid() && ReadRecordedTexture( IndependentBeforeStop, EDrawCommand::Quad3D ) == ExpectedRegions[1]->GetHandle() && IndependentBeforeStop.Words == IndependentAfterStop.Words, "別Playbackを停止しても共有regionの再生は独立して続く" );
	// Pathはflipbookを無視し、既存Ribbon命令のbyte形状を保つ。
	FParticleEmitterDefinition RibbonEmitter = Emitter;
	RibbonEmitter.Path.Kind = FEffectPathDefinition::EKind::Ribbon;
	RibbonEmitter.Texture = AtlasPath;
	RibbonEmitter.Flipbook.bEnabled = true;
	RibbonEmitter.Rate = 4.0f;
	RibbonEmitter.Bursts.Reset();
	RibbonEmitter.Speed = { 0.0f, 0.0f };
	TSharedPtr<IEffectPlayback> RibbonPlayback = NEffect::CreateParticlePlayback( MakeEffect( { RibbonEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "flipbook-legacy-ribbon" );
	RibbonPlayback->SetTransform( FTransform( FVector3( -1.0f, 0.0f, 0.0f ) ) ); RibbonPlayback->Update( 0.25f );
	RibbonPlayback->SetTransform( FTransform( FVector3( 0.0f, 0.5f, 0.0f ) ) ); RibbonPlayback->Update( 0.25f );
	RibbonPlayback->SetTransform( FTransform( FVector3( 1.0f, 0.0f, 0.0f ) ) ); RibbonPlayback->Update( 0.25f );
	const FDrawBuffer RibbonBuffer = RecordFlipbookPlayback( RibbonPlayback );
	// 同じ乱数seedと更新列でFlipbookを無効化した旧Ribbonを作る。
	FParticleEmitterDefinition LegacyRibbonEmitter = RibbonEmitter;
	LegacyRibbonEmitter.Flipbook.bEnabled = false;
	// 従来Ribbonを同じseed・transform列で再生する。
	TSharedPtr<IEffectPlayback> LegacyRibbonPlayback = NEffect::CreateParticlePlayback( MakeEffect( { LegacyRibbonEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "flipbook-disabled-ribbon" );
	if ( LegacyRibbonPlayback.IsValid() ) { LegacyRibbonPlayback->SetTransform( FTransform( FVector3( -1.0f, 0.0f, 0.0f ) ) ); LegacyRibbonPlayback->Update( 0.25f ); LegacyRibbonPlayback->SetTransform( FTransform( FVector3( 0.0f, 0.5f, 0.0f ) ) ); LegacyRibbonPlayback->Update( 0.25f ); LegacyRibbonPlayback->SetTransform( FTransform( FVector3( 1.0f, 0.0f, 0.0f ) ) ); LegacyRibbonPlayback->Update( 0.25f ); }
	// flipbook有効時の旧path命令列。
	const FDrawBuffer LegacyRibbonBuffer = RecordFlipbookPlayback( LegacyRibbonPlayback );
	bool bHasLegacyRibbon = false;
	ForEachDrawCommand( FDrawSpan{ RibbonBuffer.Words.data(), static_cast<int32>( RibbonBuffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::Ribbon3D ) bHasLegacyRibbon = true; } );
	C.Check( bHasLegacyRibbon && ReadRecordedTexture( RibbonBuffer, EDrawCommand::Quad3D ) < 0 && !RibbonBuffer.Words.empty() && RibbonBuffer.Words == LegacyRibbonBuffer.Words, "path emitterではflipbookを無視して既存Ribbon命令byte列を保つ" );
	// 無効時は通常のatlas textureを使い、従来Quad命令を空にしない。
	FParticleEmitterDefinition DisabledEmitter = Emitter;
	DisabledEmitter.Texture = AtlasPath;
	DisabledEmitter.Flipbook.bEnabled = false;
	// flipbook無効時の従来sprite再生。
	TSharedPtr<IEffectPlayback> DisabledPlayback = NEffect::CreateParticlePlayback( MakeEffect( { DisabledEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "flipbook-disabled-quad" );
	if ( DisabledPlayback.IsValid() ) { DisabledPlayback->Update( 0.01f ); DisabledPlayback->Update( 0.25f ); }
	// 通常Quadを記録した命令buffer。
	const FDrawBuffer DisabledBuffer = RecordFlipbookPlayback( DisabledPlayback );
	C.Check( !DisabledBuffer.Words.empty() && ReadRecordedTexture( DisabledBuffer, EDrawCommand::Quad3D ) >= 0, "Flipbook無効の従来Quadも有効texture付きで記録する" );
	// 子regionが親atlasを保持し、同じ再生で選択されたhandleが有効なままか。
	const TWeakPtr<CTexture> RegionWeak = ExpectedRegions[1];
	Sheet.Reset();
	C.Check( ParentWeak.IsValid() && RegionWeak.IsValid(), "sheetを手放してもregionが親textureをpinする" );
	// 全ての試験再生を先に解放してからrootを復元する。
	MaterialPlayback.Reset(); SecondsPlayback.Reset(); LifetimePlayback.Reset(); RibbonPlayback.Reset(); LegacyRibbonPlayback.Reset(); DisabledPlayback.Reset(); IndependentPlayback.Reset(); DiskPlayback.Reset(); OncePlayback.Reset(); PingPongPlayback.Reset(); MultiPlayback.Reset(); NestedRegion.Reset(); SharedRegion.Reset(); ExpectedRegions.Empty();
	C.Check( !RegionWeak.IsValid() && !ParentWeak.IsValid(), "再生と外部regionを全て解放すると親子textureが解放される" );
	Assets.SetRootDirectory( PreviousRoot );
}
