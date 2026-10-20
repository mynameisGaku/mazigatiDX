// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"

#include <cstring>
#include <vector>

#include "Library/DebugUI/EffectEditor.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Render/DrawContext.h"

namespace
{
	// 再生の記録から材質付き四角形の値を取り出す。
	TArray<FDrawMaterialQuadData> ReadMaterialQuads( const FDrawBuffer& Buffer )
	{
		TArray<FDrawMaterialQuadData> Result; // バッファから複写した材質命令。
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type == EDrawCommand::MaterialQuad3D && Command.NumDataWords * 4 >= static_cast<int32>( sizeof( FDrawMaterialQuadData ) ) ) Result.Add( Command.Read<FDrawMaterialQuadData>() );
		} );
		return Result;
	}

	// 再生を変えず描画命令だけを一度記録する。
	FDrawBuffer RecordMaterialPlayback( const TSharedPtr<IEffectPlayback>& Playback )
	{
		FDrawBuffer Buffer; // この記録呼び出しが所有する命令列。
		FDrawContext Draw( Buffer ); // 命令をメモリへ記録する窓口。
		if ( Playback.IsValid() ) Playback->Record( Draw );
		return Buffer;
	}
}

// 材質の再生値、UV 周回、アセット保持と従来経路との分離を確認する。
void NWorldTest::RunEffectMaterialPlaybackTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir )
{
	using namespace NEffectTest;
	C.Group = "effect-material-playback";
	Assets.SetRootDirectory( Dir );
	// 加算でも材質へ元の RGBA を渡す再生定義。
	FParticleEmitterDefinition Emitter = QuietEmitter();
	Emitter.Blend = EParticleBlend::Add;
	Emitter.Lifetime = { 2.0f, 2.0f };
	Emitter.StartColor = Emitter.EndColor = FColor32( 180, 90, 40, 128 );
	Emitter.StartAngle = Emitter.Spin = { 0.4f, 0.4f };
	Emitter.Material.bEnabled = true;
	Emitter.Material.bDissolve = true;
	Emitter.Material.MaskTexture = "material-playback-mask-unique.tga";
	Emitter.Material.UVScroll = FVector2( -0.25f, -1.25f );
	Emitter.Material.Dissolve.Interpolation = FEffectScalarCurve::EInterpolation::Linear;
	Emitter.Material.Dissolve.Keys.Add( { 0.0f, 0.0f } );
	Emitter.Material.Dissolve.Keys.Add( { 1.0f, 1.0f } );
	Emitter.Material.Emission.Interpolation = FEffectScalarCurve::EInterpolation::Linear;
	Emitter.Material.Emission.Keys.Add( { 0.0f, 1.0f } );
	Emitter.Material.Emission.Keys.Add( { 1.0f, 5.0f } );
	Emitter.Bursts.Add( Burst( 0.0f, 1 ) );
	const TSharedPtr<FParticleEffectDefinition> Definition = MakeEffect( { Emitter } ); // 再生契約を確認する材質定義。
	const TArray<uint8> MaskImage = NEffect::MakeSoftDotImage(); // 個別に所有状態を追跡するマスクの画像。
	std::vector<uint8> MaskBytes; // AssetManager に読み込ませる画像byte列。
	for ( const uint8 Byte : MaskImage ) MaskBytes.push_back( Byte );
	C.Check( NAssetTest::WriteFileBytes( Dir / Emitter.Material.MaskTexture, MaskBytes ), "独立したマスクfixtureをテスト用folderへ書き込む" );
	TSharedPtr<CTexture> ExpectedMask = Assets.LoadTexture( Emitter.Material.MaskTexture ); // 再生中に保持することを確認するマスク。
	TSharedPtr<IEffectPlayback> Playback = NEffect::CreateParticlePlayback( Definition, &Assets, FEffectPlayParams(), FTransform::Identity, "material-test" );
	C.Check( Playback.IsValid() && ExpectedMask.IsValid(), "材質マスクを読み込んで再生を作れる" );
	if ( !Playback.IsValid() || !ExpectedMask.IsValid() ) return;
	const int32 ExpectedMaskHandle = ExpectedMask->GetHandle(); // アセット管理の弱参照cacheへ戻す前の比較用handle。
	TWeakPtr<CTexture> MaskWeak = ExpectedMask; // 再生がresourceを保持するかの弱参照。
	ExpectedMask.Reset();
	Playback->Update( 0.01f );
	Playback->Update( 1.0f );
	const FDrawBuffer FirstBuffer = RecordMaterialPlayback( Playback ); // 1 秒経過した粒子の描画。
	const TArray<FDrawMaterialQuadData> Quads = ReadMaterialQuads( FirstBuffer ); // 実際に記録された材質値。
	C.Check( Quads.Num() == 1 && Playback->GetNumParticles() == 1, "材質が有効な Sprite 粒子は一つの材質四角形として記録する" );
	if ( Quads.Num() == 1 )
	{
		const FDrawMaterialQuadData& Data = Quads[0]; // 読み取った材質描画契約。
		C.Check( Data.MaskTexture == ExpectedMaskHandle && Data.bDissolve == 1 && Data.Dissolve > 0.45f && Data.Dissolve < 0.55f && Data.Emission > 2.9f && Data.Emission < 3.1f, "読み込んだマスク、消失曲線、Emission 曲線を粒子の寿命割合で渡す" );
		C.Check( Data.UVOffset[0] > 0.74f && Data.UVOffset[0] < 0.76f && Data.UVOffset[1] > 0.74f && Data.UVOffset[1] < 0.76f, "負の UV スクロールも粒子ごとの秒齢で正の一周内へ折り返す" );
		C.Check( Data.Quad.Color.R == 180 && Data.Quad.Color.G == 90 && Data.Quad.Color.B == 40 && Data.Quad.Color.A == 128, "材質付き Add は従来のアルファ前掛けをせず元の RGBA を渡す" );
		C.Check( Data.Quad.Angle != 0.0f, "Camera 向きの材質四角形へ既存の粒子回転角を渡す" );
	}
	// 同じ定義の別再生は独立した時間と記録を持つ。
	TSharedPtr<IEffectPlayback> OtherPlayback = NEffect::CreateParticlePlayback( Definition, &Assets, FEffectPlayParams(), FTransform::Identity, "material-other" );
	OtherPlayback->Update( 0.01f );
	const FDrawBuffer BeforeOtherUpdate = RecordMaterialPlayback( Playback ); // もう一方の再生を進める前に元の再生命令を保存。
	OtherPlayback->Update( 0.5f );
	const FDrawBuffer AfterOtherUpdate = RecordMaterialPlayback( Playback ); // 別再生を進めた後の元の再生命令。
	C.Check( BeforeOtherUpdate.Words == AfterOtherUpdate.Words, "別々の再生の更新は共有マスクや曲線の描画状態を変えない" );
	const TArray<FDrawMaterialQuadData> OtherQuads = ReadMaterialQuads( RecordMaterialPlayback( OtherPlayback ) ); // 半分の秒齢で評価した別個体。
	C.Check( OtherQuads.Num() == 1 && OtherQuads[0].UVOffset[0] > 0.87f && OtherQuads[0].UVOffset[0] < 0.88f && OtherQuads[0].UVOffset[1] > 0.37f && OtherQuads[0].UVOffset[1] < 0.38f, "同じ材質画像を共有する別再生の UV は自身の Age で進む" );
	// Fixed 向きでも材質命令のRight/Upへ従来と同じ親回転を含む粒子回転を渡す。
	FParticleEmitterDefinition FixedEmitter = Emitter;
	FixedEmitter.Orient = EParticleOrient::Fixed;
	const FQuaternion ParentRotation = FQuaternion::FromAxisAngle( FVector3::Forward, 0.35f ); // 板の基準方向に使う親回転。
	TSharedPtr<IEffectPlayback> FixedPlayback = NEffect::CreateParticlePlayback( MakeEffect( { FixedEmitter } ), &Assets, FEffectPlayParams(), FTransform( FVector3::Zero, ParentRotation ), "material-fixed" );
	FixedPlayback->Update( 0.01f );
	FixedPlayback->Update( 0.1f );
	const TArray<FDrawMaterialQuadData> FixedQuads = ReadMaterialQuads( RecordMaterialPlayback( FixedPlayback ) ); // 固定向きの材質命令。
	TArray<NParticle::FDrawParticle> FixedParticles; // 固定向き命令と比較するシミュレーション値。
	const FParticlePlayback* FixedState = dynamic_cast<const FParticlePlayback*>( FixedPlayback.Get() );
	if ( FixedState != nullptr ) NParticle::CollectDrawParticles( FixedState->GetSimulation(), FixedParticles );
	const FVector3 FixedRight = ParentRotation.Rotate( FVector3::Right ); // 親回転後の基準Right。
	const FVector3 FixedUp = ParentRotation.Rotate( FVector3::Up ); // 親回転後の基準Up。
	const FVector3 ExpectedRight = FixedRight * std::cos( FixedParticles.IsEmpty() ? 0.0f : FixedParticles[0].Angle ) + FixedUp * std::sin( FixedParticles.IsEmpty() ? 0.0f : FixedParticles[0].Angle ); // 粒子角を加えたRight。
	const FVector3 ExpectedUp = FixedUp * std::cos( FixedParticles.IsEmpty() ? 0.0f : FixedParticles[0].Angle ) - FixedRight * std::sin( FixedParticles.IsEmpty() ? 0.0f : FixedParticles[0].Angle ); // 粒子角を加えたUp。
	C.Check( FixedQuads.Num() == 1 && !FixedParticles.IsEmpty() && std::fabs( FixedQuads[0].Quad.Right[0] - ExpectedRight.x ) < 1.0e-5f && std::fabs( FixedQuads[0].Quad.Right[1] - ExpectedRight.y ) < 1.0e-5f && std::fabs( FixedQuads[0].Quad.Right[2] - ExpectedRight.z ) < 1.0e-5f && std::fabs( FixedQuads[0].Quad.Up[0] - ExpectedUp.x ) < 1.0e-5f && std::fabs( FixedQuads[0].Quad.Up[1] - ExpectedUp.y ) < 1.0e-5f && std::fabs( FixedQuads[0].Quad.Up[2] - ExpectedUp.z ) < 1.0e-5f, "Fixed 向きの材質四角形へ親回転と粒子角を反映する" );
	FParticleEmitterDefinition LegacyFixedEmitter = FixedEmitter;
	LegacyFixedEmitter.Material = FEffectMaterialDefinition();
	TSharedPtr<IEffectPlayback> LegacyFixedPlayback = NEffect::CreateParticlePlayback( MakeEffect( { LegacyFixedEmitter } ), &Assets, FEffectPlayParams(), FTransform( FVector3::Zero, ParentRotation ), "material-legacy-fixed" );
	LegacyFixedPlayback->Update( 0.01f );
	LegacyFixedPlayback->Update( 0.1f );
	FDrawQuad3DData LegacyFixedQuad; // 材質無効の固定向き命令。
	bool bLegacyFixed = false; // 従来の固定向き四角形が記録されたか。
	const FDrawBuffer LegacyFixedBuffer = RecordMaterialPlayback( LegacyFixedPlayback );
	ForEachDrawCommand( FDrawSpan{ LegacyFixedBuffer.Words.data(), static_cast<int32>( LegacyFixedBuffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::Quad3D ) { LegacyFixedQuad = Command.Read<FDrawQuad3DData>(); bLegacyFixed = true; } } );
	C.Check( FixedQuads.Num() == 1 && bLegacyFixed && std::memcmp( FixedQuads[0].Quad.Right, LegacyFixedQuad.Right, sizeof( LegacyFixedQuad.Right ) ) == 0 && std::memcmp( FixedQuads[0].Quad.Up, LegacyFixedQuad.Up, sizeof( LegacyFixedQuad.Up ) ) == 0, "Fixed 向きの材質描画は従来の親回転と粒子回転を同じbyte値で保つ" );
	// 入力定義の変更後も、開始時に複写した材質と曲線を保つ。
	TSharedPtr<FParticleEffectDefinition> MutableDefinition = MakeEffect( { Emitter } ); // 編集元として変更する別定義。
	TSharedPtr<IEffectPlayback> SnapshotPlayback = NEffect::CreateParticlePlayback( MutableDefinition, &Assets, FEffectPlayParams(), FTransform::Identity, "material-snapshot" );
	MutableDefinition->Emitters[0].Material.UVScroll = FVector2( 16.0f, 16.0f );
	SnapshotPlayback->Update( 0.01f );
	SnapshotPlayback->Update( 0.5f );
	const TArray<FDrawMaterialQuadData> SnapshotQuads = ReadMaterialQuads( RecordMaterialPlayback( SnapshotPlayback ) ); // 元定義の変更後も保持する再生値。
	C.Check( SnapshotQuads.Num() == 1 && SnapshotQuads[0].UVOffset[0] > 0.87f && SnapshotQuads[0].UVOffset[0] < 0.88f, "再生開始後に元定義を編集しても Snapshot 内の材質設定を保つ" );
	// 存在しない画像の指定は組み込みマスクへ戻る。
	FParticleEmitterDefinition MissingMaskEmitter = Emitter;
	MissingMaskEmitter.Material.MaskTexture = "material-mask-does-not-exist.png";
	const TSharedPtr<IEffectPlayback> MissingMaskPlayback = NEffect::CreateParticlePlayback( MakeEffect( { MissingMaskEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "material-missing-mask" );
	MissingMaskPlayback->Update( 0.1f );
	const TArray<FDrawMaterialQuadData> MissingMaskQuads = ReadMaterialQuads( RecordMaterialPlayback( MissingMaskPlayback ) ); // 読込失敗時のマスク指定。
	C.Check( MissingMaskQuads.Num() == 1 && MissingMaskQuads[0].MaskTexture == -1, "マスク画像の読み込み失敗は既定マスクのハンドル -1 へ安全に戻す" );
	// 材質無効時は既存 Add の byte 値を保つ。
	FParticleEmitterDefinition LegacyEmitter = Emitter;
	LegacyEmitter.Material = FEffectMaterialDefinition();
	const TSharedPtr<IEffectPlayback> LegacyPlayback = NEffect::CreateParticlePlayback( MakeEffect( { LegacyEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "material-legacy" );
	LegacyPlayback->Update( 0.01f );
	LegacyPlayback->Update( 1.0f );
	const FDrawBuffer LegacyBuffer = RecordMaterialPlayback( LegacyPlayback ); // 材質無効の従来命令。
	bool bLegacyQuad = false; // 従来の Quad3D 命令が同じ加算色を記録したか。
	FDrawQuad3DData LegacyCameraQuad; // 材質無効の Camera 向き命令。
	ForEachDrawCommand( FDrawSpan{ LegacyBuffer.Words.data(), static_cast<int32>( LegacyBuffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
	{
		if ( Command.Type != EDrawCommand::Quad3D ) return;
		const FDrawQuad3DData Data = Command.Read<FDrawQuad3DData>(); // 従来形式の四角形。
		LegacyCameraQuad = Data;
		bLegacyQuad = Data.Color.R == 90 && Data.Color.G == 45 && Data.Color.B == 20 && Data.Color.A == 255;
	} );
	C.Check( ReadMaterialQuads( LegacyBuffer ).IsEmpty() && bLegacyQuad && Quads.Num() == 1 && LegacyCameraQuad.Angle == Quads[0].Quad.Angle, "材質無効時は材質命令を出さず既存 Add の色変換と Camera 回転を保つ" );
	// Sprite材質quadの命令をRibbon経路へ誤って流用しない。
	FParticleEmitterDefinition RibbonEmitter = Emitter;
	RibbonEmitter.Path.Kind = FEffectPathDefinition::EKind::Ribbon;
	TSharedPtr<IEffectPlayback> RibbonPlayback = NEffect::CreateParticlePlayback( MakeEffect( { RibbonEmitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, "material-ribbon" );
	RibbonPlayback->Update( 0.1f );
	const FDrawBuffer RibbonBuffer = RecordMaterialPlayback( RibbonPlayback ); // 材質を適用しない経路描画。
	bool bMaterialCommand = false; // 帯の記録へ材質命令が混ざったか。
	ForEachDrawCommand( FDrawSpan{ RibbonBuffer.Words.data(), static_cast<int32>( RibbonBuffer.Words.size() ) }, [&]( const FDrawCommandView& Command ) { if ( Command.Type == EDrawCommand::MaterialQuad3D ) bMaterialCommand = true; } );
	C.Check( !bMaterialCommand, "RibbonにはSprite用MaterialQuad命令を出さない" );
	// プリセットの編集コピーを別名保存し、原本を保護する。
	const FString PresetPath = "editor-material/material_dissolve.effect.json"; // テスト専用のプリセットpath。
	C.Check( Assets.SaveEffect( PresetPath, *Definition ), "材質付きプリセットfixtureを保存する" );
	const TSharedPtr<CEffectAsset> PresetAsset = Assets.LoadEffect( PresetPath ); // 複製後も保持するプリセット原本。
	FEffectEditorState Editor; // プリセット複製を受ける独立した編集文書。
	NDebugUI::NewEffect( Editor );
	C.Check( NDebugUI::NewEffectFromPreset( Editor, PresetPath ) && Editor.FilePath.IsEmpty() && Editor.Document.IsDirty() && Editor.SaveAsPath != PresetPath, "プリセットは保存先を分けた未保存の編集コピーとして開く" );
	if ( Editor.Document.Get().Emitters.IsValidIndex( 0 ) )
	{
		FParticleEmitterDefinition EditedEmitter = Editor.Document.Get().Emitters[0]; // 文書を更新する材質付きコピー。
		EditedEmitter.Material.Emission.Keys[1].Value = 4.56789123f;
		C.Check( NParticleEdit::SetEmitter( Editor.Document, 0, EditedEmitter ), "コピー上の材質曲線を編集履歴へ登録する" );
		C.Check( NDebugUI::SaveEffectFile( Editor, "editor-material/material_dissolve_copy.effect.json" ), "編集コピーを別ファイルへ保存する" );
	}
	const TSharedPtr<CEffectAsset> SavedCopy = Assets.LoadEffect( "editor-material/material_dissolve_copy.effect.json" ); // 編集したコピーを再度読む。
	C.Check( PresetAsset.IsValid() && PresetAsset->GetDefinition().ComputeHash() == Definition->ComputeHash() && SavedCopy.IsValid() && SavedCopy->GetDefinition().Emitters[0].Material.Emission.Keys[1].Value == 4.56789123f, "別名保存後も原本を保ち、曲線の全精度を読み戻す" );
	NDebugUI::ReleaseEffectEditor( Editor );
	OtherPlayback.Reset();
	FixedPlayback.Reset();
	LegacyFixedPlayback.Reset();
	SnapshotPlayback.Reset();
	RibbonPlayback.Reset();
	Playback.Reset();
	C.Check( !MaskWeak.IsValid(), "最後の再生解放後にだけ共有マスク画像を解放する" );
}
