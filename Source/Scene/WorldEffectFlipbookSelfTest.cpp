// SPDX-License-Identifier: NOASSERTION
#include <cmath>
#include <limits>

#include "Library/Framework/Asset/DefinitionDocument.h"
#include "Library/Framework/Effect/EffectFlipbookDefinition.h"
#include "Library/Framework/Effect/ParticleEffect.h"
#include "WorldEffectSelfTestSupport.h"

namespace
{
	// 不等長の3コマから、指定された終わり方の時間軸を作る。
	FAnimTimeline MakeTimeline( EAnimLoopMode Mode )
	{
		// 元クリップの各コマが表示される秒数。
		TArray<float32> Durations;
		Durations.Add( 0.25f );
		Durations.Add( 0.5f );
		Durations.Add( 0.25f );
		return FAnimTimeline::Build( Durations, Mode );
	}

	// JSON文字列を経由してFlipbookの不正状態を検証する。
	void CheckInvalidFlipbookJson( NAssetTest::FChecker& C, const char* Text, const char* Description )
	{
		// 読み込んだeffectと往復後の検証結果。
		FParticleEffectDefinition Effect;
		FParticleEffectDefinition RoundTrip;
		const NlohmannJson Json = NlohmannJson::parse( Text );
		const bool bRead = FParticleEffectDefinition::FromJson( Json, Effect );
		const bool bRoundTrip = FParticleEffectDefinition::FromJson( NlohmannJson::parse( Effect.ToJson().dump() ), RoundTrip );
		C.Check( bRead && bRoundTrip && FParticleEffectDefinition::HasErrors( Effect.Validate() ) && FParticleEffectDefinition::HasErrors( RoundTrip.Validate() ), Description );
	}

	// 保存値、hash、旧形式、警告、JSON履歴を確認する。
	void TestDefinition( NAssetTest::FChecker& C )
	{
		C.Group = "effect-flipbook-definition";
		// 既定のeffectと既定値の複製。
		FParticleEffectDefinition DefaultEffect;
		DefaultEffect.Emitters.Add( FParticleEmitterDefinition() );
		// JSONやhashに影響しない既定値の複製。
		FParticleEffectDefinition SameEffect = DefaultEffect;
		C.Check( DefaultEffect.ComputeHash() == 0xA811CEF10E93C86Eull && !DefaultEffect.ToJson()["Emitters"][0].contains( "Flipbook" ) && DefaultEffect.Emitters[0].Flipbook.IsDefault() && SameEffect.ComputeHash() == DefaultEffect.ComputeHash(), "既定FlipbookをJSON/hashへ追加せず旧Effect goldenを保つ" );
		// 全項目の精度を確かめる非既定値。
		FEffectFlipbookDefinition Definition;
		Definition.bEnabled = true;
		Definition.Sheet = "Textures/flipbook.sheet.json";
		Definition.Clip = "Spark/Loop";
		Definition.TimeMode = FEffectFlipbookDefinition::ETimeMode::Seconds;
		Definition.Speed = 1.2345678f;
		// JSON文字列経由で再構成した値。
		FEffectFlipbookDefinition Parsed;
		// 保存したobjectを文字列化して再読する。
		const NlohmannJson DefinitionJson = NlohmannJson::parse( Definition.ToJson().dump() );
		C.Check( FEffectFlipbookDefinition::FromJson( DefinitionJson, Parsed ) && Parsed.ToJson() == Definition.ToJson() && Parsed.ComputeHash() == Definition.ComputeHash() && Parsed.Speed == Definition.Speed, "Flipbookの5項目とfloat精度をJSON文字列往復で保つ" );
		// 個別値を変えてhashへ反映する複製。
		FEffectFlipbookDefinition Changed = Definition;
		Changed.bEnabled = false;
		// Enabledだけを変えたhash。
		const uint64 EnabledHash = Changed.ComputeHash();
		Changed = Definition;
		Changed.Sheet = "Textures/flipbook.sheet.json.copy";
		// Sheetだけを変えたhash。
		const uint64 SheetHash = Changed.ComputeHash();
		Changed = Definition;
		Changed.Clip += ".copy";
		// Clipだけを変えたhash。
		const uint64 ClipHash = Changed.ComputeHash();
		Changed = Definition;
		Changed.TimeMode = FEffectFlipbookDefinition::ETimeMode::Lifetime;
		// TimeModeだけを変えたhash。
		const uint64 ModeHash = Changed.ComputeHash();
		Changed = Definition;
		Changed.Speed += 0.125f;
		// Speedだけを変えたhash。
		const uint64 SpeedHash = Changed.ComputeHash();
		C.Check( EnabledHash != Definition.ComputeHash() && SheetHash != Definition.ComputeHash() && ClipHash != Definition.ComputeHash() && ModeHash != Definition.ComputeHash() && SpeedHash != Definition.ComputeHash(), "Enabled/Sheet/Clip/TimeMode/Speed各単独変更をFlipbook hashへ反映する" );
		// 非既定値はemitter保存、effect hash、undoへ接続する。
		// 設定を保持するemitter。
		FParticleEffectDefinition Effect;
		// 保存するemitter値。
		FParticleEmitterDefinition Emitter;
		Emitter.Flipbook = Definition;
		Effect.Emitters.Add( Emitter );
		// 読み戻すeffect。
		FParticleEffectDefinition EffectRoundTrip;
		C.Check( Effect.ToJson()["Emitters"][0].contains( "Flipbook" ) && FParticleEffectDefinition::FromJson( NlohmannJson::parse( Effect.ToJson().dump() ), EffectRoundTrip ) && EffectRoundTrip.ComputeHash() == Effect.ComputeHash() && EffectRoundTrip.Emitters[0].Flipbook.ToJson() == Definition.ToJson(), "emitterの非既定Flipbookを保存とeffect hashへ接続する" );
		// Flipbookの差をeffect全体hashで確認する複製。
		FParticleEffectDefinition ChangedEffect = Effect;
		ChangedEffect.Emitters[0].Flipbook.Speed += 0.25f;
		C.Check( ChangedEffect.ComputeHash() != Effect.ComputeHash(), "emitter Flipbookの変更をeffect全体hashへ反映する" );
		// JSON snapshotを使う既存のundo/redo。
		// 編集の履歴を保持するdocument。
		TDefinitionDocument<FParticleEffectDefinition> Document;
		Document.Reset( Effect );
		// 編集前の全体hash。
		const uint64 OriginalHash = Document.Get().ComputeHash();
		Document.BeginEdit().Emitters[0].Flipbook.Speed = 2.345678f;
		// 編集後の全体hash。
		const uint64 EditedHash = Document.Get().ComputeHash();
		C.Check( EditedHash != OriginalHash && Document.Undo() && Document.Get().ComputeHash() == OriginalHash && Document.Redo() && Document.Get().ComputeHash() == EditedHash && Document.Get().Emitters[0].Flipbook.Speed == 2.345678f, "Flipbookの速度とfloat精度をJSON履歴でUndo/Redoする" );
		// 不正clipを含む定義を読んだ履歴。
		FParticleEffectDefinition InvalidEffect;
		const bool bInvalidRead = FParticleEffectDefinition::FromJson( NlohmannJson::parse( R"({"Emitters":[{"Flipbook":{"Enabled":true,"Sheet":"test.sheet.json","Clip":null}}]})" ), InvalidEffect );
		TDefinitionDocument<FParticleEffectDefinition> InvalidDocument;
		InvalidDocument.Reset( InvalidEffect );
		const bool bHasInvalidEmitter = bInvalidRead && !InvalidDocument.Get().Emitters.IsEmpty();
		if ( bHasInvalidEmitter ) InvalidDocument.BeginEdit().Emitters[0].Flipbook.Speed = 1.75f;
		C.Check( bHasInvalidEmitter && FParticleEffectDefinition::HasErrors( InvalidDocument.Get().Validate() ) && InvalidDocument.Undo() && FParticleEffectDefinition::HasErrors( InvalidDocument.Get().Validate() ) && InvalidDocument.Redo() && FParticleEffectDefinition::HasErrors( InvalidDocument.Get().Validate() ), "不正Flipbookを別項目編集した後もUndo/Redoで保存拒否を維持する" );
		// disabledでも有効な非既定値を保存するemitter。
		FParticleEffectDefinition Disabled;
		// disabledでも非既定値を持つemitter。
		FParticleEmitterDefinition DisabledEmitter;
		DisabledEmitter.Flipbook.Sheet = "Textures/kept.sheet.json";
		DisabledEmitter.Flipbook.Clip = "later";
		DisabledEmitter.Flipbook.Speed = 0.0f;
		Disabled.Emitters.Add( DisabledEmitter );
		C.Check( Disabled.ToJson()["Emitters"][0].contains( "Flipbook" ) && !FParticleEffectDefinition::HasErrors( Disabled.Validate() ), "disabledでも非既定Flipbook値を保存しMode/Speedを検証する" );
		// 有効なFlipbookとRibbonを同時に持つ警告用定義。
		FParticleEffectDefinition RibbonEffect;
		FParticleEmitterDefinition RibbonEmitter;
		RibbonEmitter.Flipbook = Definition;
		RibbonEmitter.Path.Kind = FEffectPathDefinition::EKind::Ribbon;
		RibbonEffect.Emitters.Add( RibbonEmitter );
		// 帯に対する未対応を示すvalidation結果。
		const TArray<FParticleIssue> PathWarning = RibbonEffect.Validate();
		C.Check( !FParticleEffectDefinition::HasErrors( PathWarning ) && NEffectTest::HasIssueText( PathWarning, "Flipbook は四角形粒子" ), "Ribbon有効時のFlipbookは保存可能な警告にする" );
		// null・誤型・未知mode・速度範囲違反をJSON履歴の往復後も拒否する。
		CheckInvalidFlipbookJson( C, R"({"Emitters":[{"Flipbook":null}]})", "nullのFlipbookをJSON往復後も保存拒否にする" );
		CheckInvalidFlipbookJson( C, R"({"Emitters":[{"Flipbook":{"Enabled":"yes"}}]})", "Enabledの文字列型違いをJSON往復後も保存拒否にする" );
		CheckInvalidFlipbookJson( C, R"({"Emitters":[{"Flipbook":{"Sheet":false}}]})", "Sheetのbool型違いをJSON往復後も保存拒否にする" );
		CheckInvalidFlipbookJson( C, R"({"Emitters":[{"Flipbook":{"Clip":null}}]})", "ClipのnullをJSON往復後も保存拒否にする" );
		CheckInvalidFlipbookJson( C, R"({"Emitters":[{"Flipbook":[]}]})", "配列のFlipbookをJSON往復後も保存拒否にする" );
		CheckInvalidFlipbookJson( C, R"({"Emitters":[{"Flipbook":{"TimeMode":"frames"}}]})", "未知のTimeModeをJSON往復後も保存拒否にする" );
		CheckInvalidFlipbookJson( C, R"({"Emitters":[{"Flipbook":{"Speed":null}}]})", "nullのSpeedをJSON往復後も保存拒否にする" );
		CheckInvalidFlipbookJson( C, R"({"Emitters":[{"Flipbook":{"Speed":"fast"}}]})", "文字列のSpeedをJSON往復後も保存拒否にする" );
		CheckInvalidFlipbookJson( C, R"({"Emitters":[{"Flipbook":{"Speed":-0.1}}]})", "負のSpeedをJSON往復後も保存拒否にする" );
		CheckInvalidFlipbookJson( C, R"({"Emitters":[{"Flipbook":{"Speed":129}}]})", "上限を超えたSpeedをJSON往復後も保存拒否にする" );
		// 非object root、非有限の直接値もroundtripで不正を残す。
		FEffectFlipbookDefinition InvalidRoot;
		// invalid rootをJSON経由で再読する先。
		FEffectFlipbookDefinition InvalidRootAgain;
		C.Check( FEffectFlipbookDefinition::FromJson( NlohmannJson::array(), InvalidRoot ) && !InvalidRoot.Validate().IsEmpty() && FEffectFlipbookDefinition::FromJson( NlohmannJson::parse( InvalidRoot.ToJson().dump() ), InvalidRootAgain ) && !InvalidRootAgain.Validate().IsEmpty(), "配列rootを不正状態としてJSON往復後も検証する" );
		FEffectFlipbookDefinition NonFinite;
		NonFinite.Speed = std::numeric_limits<float32>::infinity();
		// 非有限値を書き戻した後の読込先。
		FEffectFlipbookDefinition NonFiniteAgain;
		C.Check( !NonFinite.Validate().IsEmpty() && FEffectFlipbookDefinition::FromJson( NlohmannJson::parse( NonFinite.ToJson().dump() ), NonFiniteAgain ) && !NonFiniteAgain.Validate().IsEmpty(), "Inf速度をJSON nullへ書いても検証エラーを保つ" );
		NonFinite.Speed = std::numeric_limits<float32>::quiet_NaN();
		C.Check( !NonFinite.Validate().IsEmpty() && FEffectFlipbookDefinition::FromJson( NlohmannJson::parse( NonFinite.ToJson().dump() ), NonFiniteAgain ) && !NonFiniteAgain.Validate().IsEmpty(), "NaN速度をJSON nullへ書いても検証エラーを保つ" );
		// 必須項目を省いた有効な既定定義。
		FEffectFlipbookDefinition MissingFields;
		C.Check( FEffectFlipbookDefinition::FromJson( NlohmannJson::object( { { "Unknown", 4 } } ), MissingFields ) && MissingFields.IsDefault() && MissingFields.Validate().IsEmpty(), "省略項目は既定値として扱い未知キーも無視する" );
		// Sheetを持たない有効設定。
		FParticleEffectDefinition MissingSheet;
		FParticleEmitterDefinition MissingSheetEmitter;
		MissingSheetEmitter.Flipbook.bEnabled = true;
		MissingSheet.Emitters.Add( MissingSheetEmitter );
		C.Check( NEffectTest::HasIssueText( MissingSheet.Validate(), "Sheet が必要" ), "有効なFlipbookでSheetが空なら保存前にエラーにする" );
	}

	// 再生時計の境界、loop方式、巨大時刻、安全な拒否を確認する。
	void TestTimeline( NAssetTest::FChecker& C )
	{
		C.Group = "effect-flipbook-timeline";
		// 不等長clipの各loop方式の時間軸。
		const FAnimTimeline Once = MakeTimeline( EAnimLoopMode::Once );
		// Loop clipの同じ累積時刻。
		const FAnimTimeline Loop = MakeTimeline( EAnimLoopMode::Loop );
		// PingPong展開用の元duration。
		TArray<float32> PingPongDurations;
		PingPongDurations.Add( 0.25f );
		PingPongDurations.Add( 0.5f );
		PingPongDurations.Add( 0.25f );
		// 往復再生へ展開した時間軸。
		const FAnimTimeline PingPong = FAnimTimeline::Build( PingPongDurations, EAnimLoopMode::PingPong );
		C.Check( NEffectFlipbook::IsValidTimeline( Once ) && NEffectFlipbook::IsValidTimeline( Loop ) && NEffectFlipbook::IsValidTimeline( PingPong ) && PingPong.NumSteps() == 4, "Once/Loop/PingPongの正常timelineを受け入れ、往復展開を確認する" );
		// 寿命時計は速度に関係なく全clipを一度たどり最後で止まる。
		FEffectFlipbookDefinition Definition;
		Definition.bEnabled = true;
		Definition.Sheet = "test.sheet.json";
		Definition.TimeMode = FEffectFlipbookDefinition::ETimeMode::Lifetime;
		Definition.Speed = 0.0f;
		C.Check( Definition.IsValidSettings(), "正常timeline評価設定のsheetと設定値を検証する" );
		C.Check( NEffectFlipbook::EvaluateStep( Definition, Once, 99.0, 0.0f ) == 0 && NEffectFlipbook::EvaluateStep( Definition, Once, 99.0, 0.249f ) == 0 && NEffectFlipbook::EvaluateStep( Definition, Once, 99.0, 0.25f ) == 1 && NEffectFlipbook::EvaluateStep( Definition, Once, 99.0, 0.749f ) == 1 && NEffectFlipbook::EvaluateStep( Definition, Once, 99.0, 0.75f ) == 2 && NEffectFlipbook::EvaluateStep( Definition, Once, 99.0, 1.0f ) == 2, "寿命割合0/.249/.25/.749/.75/1を各コマへ写し、端で最終コマを保つ" );
		Definition.Speed = 128.0f;
		C.Check( NEffectFlipbook::EvaluateStep( Definition, Once, 0.0, 1.0f ) == 2, "寿命時計はSpeedを無視し寿命終端で最後のコマを返す" );
		// 秒時計はclip loopに従って境界時刻を扱う。
		Definition.TimeMode = FEffectFlipbookDefinition::ETimeMode::Seconds;
		Definition.Speed = 1.0f;
		const int32 HugeFiniteStep = NEffectFlipbook::EvaluateStep( Definition, Loop, 1.0e300, 0.0f );
		C.Check( HugeFiniteStep >= 0 && HugeFiniteStep < Loop.NumSteps(), "巨大な有限秒数は有効範囲のコマへfmodで折り返す" );
		C.Check( NEffectFlipbook::EvaluateStep( Definition, Loop, 0.0, 0.99f ) == 0 && NEffectFlipbook::EvaluateStep( Definition, Loop, 0.25, 0.0f ) == 1 && NEffectFlipbook::EvaluateStep( Definition, Loop, 0.75, 0.0f ) == 2 && NEffectFlipbook::EvaluateStep( Definition, Loop, 1.0, 0.0f ) == 0 && NEffectFlipbook::EvaluateStep( Definition, Loop, 1.25, 0.0f ) == 1, "秒時計のloop境界は先頭へ戻り、次cycleの不等長区間も切り替わる" );
		C.Check( NEffectFlipbook::EvaluateStep( Definition, Once, 1.0, 0.0f ) == 2 && NEffectFlipbook::EvaluateStep( Definition, PingPong, 0.75, 0.0f ) == 2 && NEffectFlipbook::EvaluateStep( Definition, PingPong, 1.0, 0.0f ) == 3, "Onceは終端を最後へ止め、PingPongは展開された戻りstepを選ぶ" );
		Definition.Speed = 1.5f;
		C.Check( NEffectFlipbook::EvaluateStep( Definition, Once, 0.5, 0.0f ) == 2, "秒時計はAge×Speedでコマを選ぶ" );
		Definition.Speed = 0.0f;
		C.Check( NEffectFlipbook::EvaluateStep( Definition, Loop, 1.0e300, 0.0f ) == 0 && NEffectFlipbook::EvaluateStep( Definition, Loop, 5.0, 1.0f ) == 0, "巨大な有限秒数は整数cycleへ変換せずfmodで安全に折り返し、Speed0は先頭で止まる" );
		C.Check( NEffectFlipbook::EvaluateStep( Definition, Loop, std::numeric_limits<float64>::quiet_NaN(), 0.0f ) == INDEX_NONE && NEffectFlipbook::EvaluateStep( Definition, Loop, std::numeric_limits<float64>::infinity(), 0.0f ) == INDEX_NONE && NEffectFlipbook::EvaluateStep( Definition, Loop, -1.0, 0.0f ) == INDEX_NONE, "NaN/Inf/負のAgeを秒時計で拒否する" );
		Definition.Speed = 128.0f;
		C.Check( NEffectFlipbook::EvaluateStep( Definition, Loop, std::numeric_limits<float64>::max(), 0.0f ) == INDEX_NONE, "Age×Speedのdouble overflowを拒否する" );
		Definition.bEnabled = false;
		C.Check( NEffectFlipbook::EvaluateStep( Definition, Loop, 0.0, 0.0f ) == INDEX_NONE, "無効なFlipbookはコマを返さない" );
		// 単一フレームと壊れたtimelineの扱い。
		// 一枚だけのclipのduration。
		TArray<float32> OneDuration;
		OneDuration.Add( 0.125f );
		// 単一コマを往復再生として展開した時間軸。
		const FAnimTimeline Single = FAnimTimeline::Build( OneDuration, EAnimLoopMode::PingPong );
		// 単一コマを有効化した定義。
		FEffectFlipbookDefinition SingleEnabled;
		SingleEnabled.bEnabled = true;
		SingleEnabled.Sheet = "single.sheet.json";
		C.Check( NEffectFlipbook::IsValidTimeline( Single ) && NEffectFlipbook::EvaluateStep( SingleEnabled, Single, 100000.0, 0.5f ) == 0, "単一コマのPingPongは常にそのコマを返す" );
		// 累積時間を重複させた破損timeline。
		FAnimTimeline Bad = Once;
		Bad.StepEnd[1] = Bad.StepEnd[0];
		// 壊れたtimelineの安全性を確かめる有効定義。
		FEffectFlipbookDefinition Enabled;
		Enabled.bEnabled = true;
		Enabled.Sheet = "test.sheet.json";
		C.Check( !NEffectFlipbook::IsValidTimeline( Bad ), "重複累積時刻のtimelineを事前検証で拒否する" );
		Bad = Once;
		Bad.StepSource[1] = -1;
		C.Check( !NEffectFlipbook::IsValidTimeline( Bad ) && NEffectFlipbook::EvaluateStep( Enabled, Bad, 0.3, 0.3f ) == INDEX_NONE, "不正な元コマ参照を検出し選択対象でも拒否する" );
		Bad = Once;
		Bad.Total += 0.5f;
		C.Check( !NEffectFlipbook::IsValidTimeline( Bad ) && NEffectFlipbook::EvaluateStep( Enabled, Bad, 0.3, 0.0f ) == INDEX_NONE, "Totalと最終累積時刻の不一致を拒否する" );
		Bad = Once;
		Bad.Loop = static_cast<EAnimLoopMode>( 0xFFu );
		C.Check( !NEffectFlipbook::IsValidTimeline( Bad ) && NEffectFlipbook::EvaluateStep( Enabled, Bad, 0.3, 0.0f ) == INDEX_NONE, "未知のloop方式を拒否する" );
		Enabled.TimeMode = FEffectFlipbookDefinition::ETimeMode::Lifetime;
		// 寿命時計で無視する経過秒をNaNにしたparticle結果。
		const int32 LifetimeParticleStep = NEffectFlipbook::EvaluateStep( Enabled, Once, std::numeric_limits<float64>::quiet_NaN(), 0.25f );
		Enabled.TimeMode = FEffectFlipbookDefinition::ETimeMode::Seconds;
		// 秒時計で無視するnormalized ageをNaNにしたparticle結果。
		const int32 SecondsParticleStep = NEffectFlipbook::EvaluateStep( Enabled, Loop, 0.5, std::numeric_limits<float32>::quiet_NaN() );
		C.Check( LifetimeParticleStep == 1 && SecondsParticleStep == 1, "共有timelineでも粒子ごとの時計だけを使い、無関係なage入力は参照しない" );
		Enabled.TimeMode = FEffectFlipbookDefinition::ETimeMode::Lifetime;
		C.Check( NEffectFlipbook::EvaluateStep( Enabled, Once, 0.0, std::numeric_limits<float32>::quiet_NaN() ) == INDEX_NONE, "Lifetime時計の非有限normalized ageを拒否する" );
		Enabled.Speed = std::numeric_limits<float32>::quiet_NaN();
		C.Check( NEffectFlipbook::EvaluateStep( Enabled, Once, 0.0, 0.25f ) == INDEX_NONE, "不正なSpeedを寿命時計でも設定エラーとして拒否する" );
		FEffectFlipbookDefinition InvalidFlags;
		FEffectFlipbookDefinition::FromJson( NlohmannJson::parse( R"({"Enabled":true,"Sheet":"test.sheet.json","Clip":null})" ), InvalidFlags );
		C.Check( InvalidFlags.bEnabled && NEffectFlipbook::EvaluateStep( InvalidFlags, Once, 0.0, 0.25f ) == INDEX_NONE, "寿命時計でもJSON型不正を含む設定を拒否する" );
		// source順とstep順を意図的に分けたtimeline。
		FAnimTimeline Reordered = Once;
		Reordered.StepSource[0] = 2;
		Reordered.StepSource[1] = 0;
		Reordered.StepSource[2] = 1;
		FEffectFlipbookDefinition ReorderedDefinition;
		ReorderedDefinition.bEnabled = true;
		ReorderedDefinition.Sheet = "test.sheet.json";
		ReorderedDefinition.TimeMode = FEffectFlipbookDefinition::ETimeMode::Lifetime;
		const int32 ReorderedStep = NEffectFlipbook::EvaluateStep( ReorderedDefinition, Reordered, 0.0, 0.25f );
		// PingPongの戻り末尾をstep番号で確認する設定。
		FEffectFlipbookDefinition PingPongDefinition = ReorderedDefinition;
		PingPongDefinition.TimeMode = FEffectFlipbookDefinition::ETimeMode::Seconds;
		C.Check( NEffectFlipbook::IsValidTimeline( Reordered ) && ReorderedStep == 1 && Reordered.StepSource[ReorderedStep] == 0 && NEffectFlipbook::EvaluateStep( ReorderedDefinition, Reordered, 0.0, 1.0f ) == 2 && Reordered.StepSource.Last() == 1 && NEffectFlipbook::EvaluateStep( PingPongDefinition, PingPong, 1.0, 0.0f ) == 3 && PingPong.StepSource[3] == 1, "EvaluateStepは終端でもstep番号を返し、戻りコマはStepSourceから引く" );
	}
}

void NWorldTest::RunEffectFlipbookTests( NAssetTest::FChecker& C )
{
	TestDefinition( C );
	TestTimeline( C );
}
