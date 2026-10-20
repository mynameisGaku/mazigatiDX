// SPDX-License-Identifier: NOASSERTION
#include <cmath>
#include <limits>

#include "Library/Framework/Asset/DefinitionDocument.h"
#include "Library/Framework/Effect/EffectMaterialDefinition.h"
#include "Library/Framework/Effect/ParticleEffect.h"
#include "Library/Framework/Effect/ParticleSimulation.h"
#include "WorldSelfTestSupport.h"

namespace
{
	// 指定した寿命位置と値を、テスト曲線の末尾へ追加する。
	void AddKey( FEffectScalarCurve& Curve, float32 Time, float32 Value )
	{
		// 追加先へ積む、指定時刻と値のキー。
		FEffectScalarCurve::FKey Key;
		Key.Time = Time;
		Key.Value = Value;
		Curve.Keys.Add( Key );
	}

	// 補間境界、空曲線、壊れた直接入力でも評価が有限であることを確かめる。
	void TestCurveEvaluation( NAssetTest::FChecker& C )
	{
		C.Group = "effect-material-curve-evaluation";
		// 途中で補間方法を切り替えながら評価する曲線。
		FEffectScalarCurve Curve;
		C.Near( Curve.Evaluate( 0.5f, 7.0f ), 7.0, 0.0, "空の曲線は呼び出し元の既定値を返す" );
		C.Near( Curve.Evaluate( 0.5f, std::numeric_limits<float32>::infinity() ), 0.0, 0.0, "空の曲線の非有限既定値は有限な 0 に置き換える" );
		AddKey( Curve, 0.0f, 2.0f );
		AddKey( Curve, 0.5f, 4.0f );
		AddKey( Curve, 1.0f, 6.0f );
		C.Near( Curve.Evaluate( -1.0f, 0.0f ), 2.0, 0.0, "曲線の先頭より前は最初のキー値を返す" );
		C.Near( Curve.Evaluate( 0.0f, 0.0f ), 2.0, 0.0, "先頭のキー時刻で同じキー値を返す" );
		C.Near( Curve.Evaluate( 0.25f, 0.0f ), 3.0, 1.0e-6, "Linear は隣のキー値を直線で補間する" );
		C.Near( Curve.Evaluate( 0.5f, 0.0f ), 4.0, 0.0, "中間のキー時刻でそのキー値を返す" );
		C.Near( Curve.Evaluate( 1.5f, 0.0f ), 6.0, 0.0, "曲線の末尾より後は最後のキー値を返す" );
		Curve.Interpolation = FEffectScalarCurve::EInterpolation::Step;
		C.Near( Curve.Evaluate( 0.49f, 0.0f ), 2.0, 0.0, "Step は次のキーより前まで左の値を保つ" );
		C.Near( Curve.Evaluate( 0.5f, 0.0f ), 4.0, 0.0, "Step はキー境界で新しい値へ切り替える" );
		Curve.Keys.Reset();
		AddKey( Curve, 0.0f, 2.0f );
		AddKey( Curve, 1.0f, 6.0f );
		Curve.Interpolation = FEffectScalarCurve::EInterpolation::Smooth;
		C.Near( Curve.Evaluate( 0.25f, 0.0f ), 2.625, 1.0e-6, "Smooth は smoothstep の係数で値を補間する" );
		C.Near( Curve.Evaluate( std::numeric_limits<float32>::quiet_NaN(), 9.0f ), 9.0, 0.0, "非有限の時刻は安全な既定値を返す" );
		C.Near( Curve.Evaluate( 0.5f, std::numeric_limits<float32>::infinity() ), 4.0, 0.0, "キーがある曲線は未使用の非有限既定値を参照しない" );
		FEffectScalarCurve Unsorted; // 順番と時刻重複への耐性を調べる曲線。
		AddKey( Unsorted, 0.75f, 7.0f );
		AddKey( Unsorted, 0.25f, 3.0f );
		C.Near( Unsorted.Evaluate( 0.5f, 0.0f ), 5.0, 1.0e-6, "順序が壊れた直接入力でも 32 件以内を安全に探索する" );
		AddKey( Unsorted, 0.25f, 5.0f );
		C.Near( Unsorted.Evaluate( 0.25f, 0.0f ), 3.0, 0.0, "重複時刻は配列の先頭キーを決定的に使う" );
	}

	// 上限、時刻順、有限値、曲線ごとの値域を検証する。
	void TestCurveValidation( NAssetTest::FChecker& C )
	{
		C.Group = "effect-material-curve-validation";
		// 有効な端点を持つ基準曲線。
		FEffectScalarCurve Valid;
		AddKey( Valid, 0.0f, 1.0f );
		AddKey( Valid, 1.0f, 3.0f );
		C.Check( Valid.Validate().IsEmpty(), "端点を含む昇順の有限な曲線を受け入れる" );
		FEffectScalarCurve Invalid; // 順序・有限性・時刻範囲を壊した曲線。
		AddKey( Invalid, 0.7f, 1.0f );
		AddKey( Invalid, 0.2f, 2.0f );
		AddKey( Invalid, 0.2f, std::numeric_limits<float32>::infinity() );
		AddKey( Invalid, 1.1f, 4.0f );
		for ( int32 Index = 0; Index < FEffectScalarCurve::MaxKeys; ++Index ) AddKey( Invalid, 0.5f, 1.0f );
		C.Check( Invalid.Validate().Num() >= 4, "重複・降順・範囲外・非有限・キー数上限超過を検出する" );
		FEffectScalarCurve BelowRange; // 一つの時刻だけを下限より外した曲線。
		AddKey( BelowRange, -0.1f, 2.0f );
		C.Check( BelowRange.Validate().Num() == 1 && std::isfinite( BelowRange.Evaluate( 0.5f, 0.0f ) ), "時刻 -0.1 だけを持つ曲線は単独で範囲エラーになり、安全に評価できる" );
		FEffectScalarCurve AboveRange; // 一つの時刻だけを上限より外した曲線。
		AddKey( AboveRange, 1.1f, 2.0f );
		C.Check( AboveRange.Validate().Num() == 1 && std::isfinite( AboveRange.Evaluate( 0.5f, 0.0f ) ), "時刻 1.1 だけを持つ曲線は単独で範囲エラーになり、安全に評価できる" );
		FEffectScalarCurve DuplicateTime; // 同じ時刻だけで順序条件を壊す曲線。
		AddKey( DuplicateTime, 0.4f, 1.0f );
		AddKey( DuplicateTime, 0.4f, 2.0f );
		C.Check( DuplicateTime.Validate().Num() == 1 && std::isfinite( DuplicateTime.Evaluate( 0.4f, 0.0f ) ), "同時刻の二キーだけを持つ曲線を単独で検出し、安全に評価する" );
		FEffectScalarCurve DescendingTime; // 昇順だけを壊した曲線。
		AddKey( DescendingTime, 0.8f, 1.0f );
		AddKey( DescendingTime, 0.2f, 2.0f );
		C.Check( DescendingTime.Validate().Num() == 1 && std::isfinite( DescendingTime.Evaluate( 0.5f, 0.0f ) ), "降順の二キーだけを持つ曲線を単独で検出し、安全に評価する" );
		FEffectScalarCurve TooMany; // 33 個目の値を評価で無視する曲線。
		for ( int32 Index = 0; Index <= FEffectScalarCurve::MaxKeys; ++Index ) AddKey( TooMany, static_cast<float32>( Index ) / FEffectScalarCurve::MaxKeys, 0.0f );
		TooMany.Keys.Last().Value = 99.0f;
		C.Check( TooMany.Validate().Num() == 1 && TooMany.Evaluate( 1.0f, 99.0f ) == 0.0f, "キーが 32 個を超えた曲線を検出し、評価は先頭 32 個へ制限する" );
		FEffectScalarCurve Changed = Valid; // hash の補間方法差を調べる複製。
		Changed.Interpolation = FEffectScalarCurve::EInterpolation::Step;
		C.Check( Changed.ComputeHash() != Valid.ComputeHash(), "補間方法の変更を曲線 hash に反映する" );
		Changed = Valid;
		Changed.Keys[1].Value += 0.125f;
		C.Check( Changed.ComputeHash() != Valid.ComputeHash(), "キー時刻と値を曲線 hash に反映する" );
		FEffectScalarCurve Malformed; // 不正 JSON の検出対象。
		TArray<FString> Warnings; // 読み込み内容の警告。
		const NlohmannJson MalformedJson = NlohmannJson::parse( R"({"Interpolation":"Cubic","Keys":[{"Time":"bad","Value":0.5}]})" ); // 型と補間名を壊した入力。
		C.Check( FEffectScalarCurve::FromJson( MalformedJson, Malformed, &Warnings ) && !Warnings.IsEmpty() && !Malformed.Validate().IsEmpty() && std::isfinite( Malformed.Evaluate( 0.5f, 4.0f ) ), "未知補間と文字列の時刻を不正状態として残し、評価だけは安全にする" );
		C.Check( FEffectScalarCurve::FromJson( NlohmannJson::object( { { "Keys", 1 } } ), Malformed ) && !Malformed.Validate().IsEmpty(), "配列でない Keys を有効な空曲線へ変換しない" );
	}

	// 材質の全項目を JSON と hash に保存し、無効中も値を保つ。
	void TestMaterialData( NAssetTest::FChecker& C )
	{
		C.Group = "effect-material-data";
		const FEffectMaterialDefinition DefaultMaterial;
		C.Check( DefaultMaterial.Dissolve.Evaluate( 0.5f, 0.0f ) == 0.0f && DefaultMaterial.Emission.Evaluate( 0.5f, 1.0f ) == 1.0f, "空の材質曲線は消失 0 と発光 1 の既定値を使う" );
		// Material を省略した旧形式との互換性を調べる定義。
		FParticleEffectDefinition Legacy;
		Legacy.Emitters.Add( FParticleEmitterDefinition() );
		C.Check( !Legacy.ToJson()["Emitters"][0].contains( "Material" ), "既定材質を省略し、既存の effect JSON を保つ" );
		FEffectMaterialDefinition Material; // 全項目を変更した保存対象。
		Material.bEnabled = false;
		Material.bDissolve = true;
		Material.MaskTexture = "Textures/NoiseCutout.png";
		Material.UVScale = FVector2( 1.123456789f, 2.234567891f );
		Material.UVScroll = FVector2( -0.34567891f, 0.45678912f );
		Material.MaskScale = 3.4567891f;
		Material.Dissolve.Interpolation = FEffectScalarCurve::EInterpolation::Smooth;
		AddKey( Material.Dissolve, 0.123456791f, 0.234567895f );
		AddKey( Material.Dissolve, 0.987654328f, 0.876543224f );
		Material.Emission.Interpolation = FEffectScalarCurve::EInterpolation::Step;
		AddKey( Material.Emission, 0.0f, 0.123456791f );
		AddKey( Material.Emission, 1.0f, 12.3456783f );
		Material.EdgeWidth = 0.06789123f;
		Material.EdgeColor = FColor32( 12, 34, 56, 78 );
		Material.EdgeEmission = 23.456789f;
		FEffectMaterialDefinition Parsed; // JSON 往復後の材質。
		C.Check( FEffectMaterialDefinition::FromJson( NlohmannJson::parse( Material.ToJson().dump() ), Parsed ) && Parsed.ToJson() == Material.ToJson() && Parsed.ComputeHash() == Material.ComputeHash() && Parsed.UVScale == Material.UVScale && Parsed.Dissolve.Keys[0].Value == Material.Dissolve.Keys[0].Value, "材質の全値と曲線を JSON 文字列経由で精度を落とさず往復する" );
		C.Check( Material.Validate().IsEmpty(), "無効中も正しい材質データを保持できる" );
		FEffectMaterialDefinition Invalid; // 値を範囲外のまま保持する読み込み先。
		const NlohmannJson InvalidJson = NlohmannJson::parse( R"({"UVScale":[256,1],"UVScroll":[-129,0],"MaskScale":0,"Dissolve":{"Keys":[{"Time":0,"Value":2}]},"Emission":{"Keys":[{"Time":0,"Value":-4}]},"EdgeWidth":2,"EdgeEmission":33})" ); // 型は正しいが範囲を外れる材質。
		TArray<FString> Warnings; // この数値入力では読み込み警告がないことも見る。
		const bool bParsed = FEffectMaterialDefinition::FromJson( InvalidJson, Invalid, &Warnings ); // 入力を丸めず読み込めたか。
		C.Check( bParsed && Warnings.IsEmpty() && Invalid.UVScale.x == 256.0f && Invalid.Dissolve.Keys[0].Value == 2.0f && !Invalid.Validate().IsEmpty(), "JSON から範囲外の数を丸めず読み、Validate が不正値を示す" );
		const NlohmannJson WrongTypes = NlohmannJson::parse( R"({"UVScale":"bad","MaskScale":"bad","EdgeWidth":"bad"})" ); // 数値とベクトルの型が異なる入力。
		FEffectMaterialDefinition WrongTypeMaterial; // 不正な型を検証へ渡す出力。
		C.Check( FEffectMaterialDefinition::FromJson( WrongTypes, WrongTypeMaterial ) && !WrongTypeMaterial.Validate().IsEmpty(), "不正な浮動小数点・ベクトル型は NaN を残して Validate で検出する" );
		const NlohmannJson InvalidFlags = NlohmannJson::parse( R"({"Enabled":"true","DissolveEnabled":1,"MaskTexture":false,"EdgeColor":"oops"})" ); // 保存した既定値へ化けやすい型違い。
		FEffectMaterialDefinition InvalidFlagMaterial; // 型違いのフラグと文字列を検証する材質。
		C.Check( FEffectMaterialDefinition::FromJson( InvalidFlags, InvalidFlagMaterial ) && !InvalidFlagMaterial.Validate().IsEmpty(), "フラグ・マスク名・境界色の型違いを有効な既定値へ化けさせず Validate で止める" );
		const NlohmannJson InvalidMaterialObject = NlohmannJson::parse( R"({"Emitters":[{"Material":[]}]})" ); // Material がオブジェクトでない effect。
		FParticleEffectDefinition InvalidMaterialEffect; // 不正な Material を持つ effect の読み込み先。
		C.Check( FParticleEffectDefinition::FromJson( InvalidMaterialObject, InvalidMaterialEffect ) && !InvalidMaterialEffect.Validate().IsEmpty(), "Material が配列・null・数値などのときも既定材質として上書き可能にしない" );
		FEffectMaterialDefinition Changed = Material; // 一項目ずつ変えて hash を比べる複製。
		Changed.bEnabled = !Material.bEnabled;
		C.Check( Changed.ComputeHash() != Material.ComputeHash(), "Enabled の変更を材質 hash に反映する" );
		Changed = Material;
		Changed.bDissolve = !Material.bDissolve;
		C.Check( Changed.ComputeHash() != Material.ComputeHash(), "DissolveEnabled の変更を材質 hash に反映する" );
		Changed = Material;
		Changed.MaskTexture += ".copy";
		C.Check( Changed.ComputeHash() != Material.ComputeHash(), "MaskTexture の変更を材質 hash に反映する" );
		Changed = Material;
		Changed.UVScale.x += 0.1f;
		C.Check( Changed.ComputeHash() != Material.ComputeHash(), "UVScale の変更を材質 hash に反映する" );
		Changed = Material;
		Changed.UVScroll.y += 0.1f;
		C.Check( Changed.ComputeHash() != Material.ComputeHash(), "UVScroll の変更を材質 hash に反映する" );
		Changed = Material;
		Changed.MaskScale += 0.1f;
		C.Check( Changed.ComputeHash() != Material.ComputeHash(), "MaskScale の変更を材質 hash に反映する" );
		Changed = Material;
		Changed.Dissolve.Keys[0].Value += 0.1f;
		C.Check( Changed.ComputeHash() != Material.ComputeHash(), "Dissolve 曲線の変更を材質 hash に反映する" );
		Changed = Material;
		Changed.Emission.Keys[0].Time = 0.1f;
		C.Check( Changed.ComputeHash() != Material.ComputeHash(), "Emission 曲線の変更を材質 hash に反映する" );
		Changed = Material;
		Changed.EdgeWidth += 0.01f;
		C.Check( Changed.ComputeHash() != Material.ComputeHash(), "EdgeWidth の変更を材質 hash に反映する" );
		Changed = Material;
		Changed.EdgeColor.r++;
		C.Check( Changed.ComputeHash() != Material.ComputeHash(), "EdgeColor の変更を材質 hash に反映する" );
		Changed = Material;
		Changed.EdgeColor.g++;
		C.Check( Changed.ComputeHash() != Material.ComputeHash(), "EdgeColor の緑成分だけの変更を材質 hash に反映する" );
		Changed = Material;
		Changed.EdgeColor.b++;
		C.Check( Changed.ComputeHash() != Material.ComputeHash(), "EdgeColor の青成分だけの変更を材質 hash に反映する" );
		Changed = Material;
		Changed.EdgeColor.a++;
		C.Check( Changed.ComputeHash() != Material.ComputeHash(), "EdgeColor の alpha 成分だけの変更を材質 hash に反映する" );
		Changed = Material;
		Changed.EdgeEmission += 0.1f;
		C.Check( Changed.ComputeHash() != Material.ComputeHash(), "EdgeEmission の変更を材質 hash に反映する" );
		FEffectMaterialDefinition Unchanged = Material; // 読み込み失敗で変わらないことを調べる出力。
		C.Check( !FEffectMaterialDefinition::FromJson( NlohmannJson::array(), Unchanged ) && Unchanged.ComputeHash() == Material.ComputeHash(), "オブジェクト以外の入力は既存の出力を変えず失敗する" );
	}

	// emitter 保存、材質対応範囲、履歴の JSON 再構築を確認する。
	void TestEmitterMaterialAndUndo( NAssetTest::FChecker& C )
	{
		C.Group = "effect-material-emitter-undo";
		// 材質付きエミッターを保存する基準定義。
		FParticleEffectDefinition Effect;
		Effect.Emitters.Add( FParticleEmitterDefinition() );
		FEffectMaterialDefinition Material; // emitter に保存する曲線と UV 設定。
		Material.MaskTexture = "Textures/Noise.png";
		Material.UVScroll = FVector2( 0.125f, -0.25f );
		Material.Emission.Interpolation = FEffectScalarCurve::EInterpolation::Smooth;
		AddKey( Material.Emission, 0.0f, 0.25f );
		AddKey( Material.Emission, 1.0f, 8.5f );
		Effect.Emitters[0].Material = Material;
		const NlohmannJson Saved = NlohmannJson::parse( Effect.ToJson().dump() ); // 文字列化した effect 保存データ。
		FParticleEffectDefinition Loaded; // 保存データの読み戻し先。
		C.Check( Saved["Emitters"][0].contains( "Material" ) && FParticleEffectDefinition::FromJson( Saved, Loaded ) && Loaded.ComputeHash() == Effect.ComputeHash() && Loaded.Emitters[0].Material.ToJson() == Material.ToJson(), "emitter の Material を effect JSON と全体 hash に接続する" );
		FParticleEffectDefinition ChangedEffect = Effect; // 材質変更の全体 hash への波及を調べる複製。
		ChangedEffect.Emitters[0].Material.EdgeEmission += 0.5f;
		C.Check( ChangedEffect.ComputeHash() != Effect.ComputeHash(), "emitter の材質変更を effect 全体の hash へ反映する" );
		TDefinitionDocument<FParticleEffectDefinition> Document; // JSON snapshot を使う既存編集履歴。
		Document.Reset( Effect );
		const uint64 OriginalHash = Document.Get().ComputeHash(); // 編集前に戻すべき定義の識別値。
		FEffectMaterialDefinition Edited = Material; // 履歴へ記録する曲線値の変更。
		Edited.Emission.Keys[1].Value = 9.876543f;
		Document.BeginEdit().Emitters[0].Material = Edited;
		const uint64 EditedHash = Document.Get().ComputeHash(); // Undo / Redo 後に復元する識別値。
		C.Check( EditedHash != OriginalHash && Document.Undo() && Document.Get().ComputeHash() == OriginalHash && Document.Redo() && Document.Get().ComputeHash() == EditedHash && Document.Get().Emitters[0].Material.Emission.Keys[1].Value == Edited.Emission.Keys[1].Value, "JSON を使う既存履歴で材質曲線を精度を保って Undo / Redo する" );
		// alpha 成分だけの変更を、独立した JSON 履歴で確認する。
		// alpha履歴専用の編集文書。
		TDefinitionDocument<FParticleEffectDefinition> AlphaDocument;
		AlphaDocument.Reset( Effect );
		// alpha編集前の定義全体のhash。
		const uint64 AlphaOriginalHash = AlphaDocument.Get().ComputeHash();
		// 編集前に復元する境界色のalpha。
		const uint8 AlphaOriginal = AlphaDocument.Get().Emitters[0].Material.EdgeColor.a;
		// alphaだけを変更する材質の複製。
		FEffectMaterialDefinition AlphaEdited = AlphaDocument.Get().Emitters[0].Material;
		AlphaEdited.EdgeColor.a = static_cast<uint8>( AlphaOriginal == 255 ? 254 : AlphaOriginal + 1 );
		AlphaDocument.BeginEdit().Emitters[0].Material = AlphaEdited;
		// alpha編集後の定義全体のhash。
		const uint64 AlphaEditedHash = AlphaDocument.Get().ComputeHash();
		C.Check( AlphaEditedHash != AlphaOriginalHash, "EdgeColor alpha だけの変更を effect 全体の hash に反映する" );
		// alpha編集を取り消せたか。
		const bool bAlphaUndo = AlphaDocument.Undo();
		C.Check( bAlphaUndo && AlphaDocument.Get().ComputeHash() == AlphaOriginalHash && AlphaDocument.Get().Emitters[0].Material.EdgeColor.a == AlphaOriginal, "alpha 変更の Undo で元の hash と alpha を戻す" );
		// alpha編集をやり直せたか。
		const bool bAlphaRedo = AlphaDocument.Redo();
		C.Check( bAlphaRedo && AlphaDocument.Get().ComputeHash() == AlphaEditedHash && AlphaDocument.Get().Emitters[0].Material.EdgeColor.a == AlphaEdited.EdgeColor.a, "alpha 変更の Redo で編集後の hash と alpha を戻す" );
		Effect.Emitters[0].Path.Kind = FEffectPathDefinition::EKind::Ribbon;
		Effect.Emitters[0].Material.bEnabled = true;
		const TArray<FParticleIssue> Issues = Effect.Validate(); // Ribbon材質を含む定義の検証結果。
		C.Check( !FParticleEffectDefinition::HasErrors( Issues ) && Issues.IsEmpty(), "材質有効のRibbon定義は警告なしで検証できる" );
		Effect.Emitters[0].Lifetime = { 20000.0f, 20000.0f };
		Effect.Emitters[0].Material.UVScroll = FVector2( 128.0f, 0.0f );
		const TArray<FParticleIssue> ExcessiveRibbonScroll = Effect.Validate(); // GPU位相上限を超える有効Ribbon材質。
		C.Check( ExcessiveRibbonScroll.ContainsByPredicate( []( const FParticleIssue& Issue ) { return Issue.bError && Issue.Message.Contains( "帯の材質では寿命×UV速度" ); } ), "Ribbon材質の寿命×UV速度が1000000を超えたら保存前にエラーにする" );
		Effect.Emitters[0].Lifetime = { 20000.0f, 10000.0f };
		const TArray<FParticleIssue> ReversedLifetimeScroll = Effect.Validate(); // 実行時に入れ替わる逆順範囲。
		C.Check( ReversedLifetimeScroll.ContainsByPredicate( []( const FParticleIssue& Issue ) { return Issue.bError && Issue.Message.Contains( "帯の材質では寿命×UV速度" ); } ), "逆順Lifetimeでも最大端点を使ってRibbon材質のUV上限を検証する" );
		Effect.Emitters[0].Lifetime = { 10000.0f, 20000.0f };
		Effect.Emitters[0].Material.UVScroll = FVector2( 50.0f, 0.0f );
		const TArray<FParticleIssue> BoundaryRibbonScroll = Effect.Validate(); // 積が上限と等しい有効範囲。
		C.Check( !BoundaryRibbonScroll.ContainsByPredicate( []( const FParticleIssue& Issue ) { return Issue.bError && Issue.Message.Contains( "帯の材質では寿命×UV速度" ); } ), "Lifetime最大値×UV速度が1000000と等しい境界は許可する" );
		Effect.Emitters[0].Material.UVScroll = FVector2( 128.0f, 0.0f );
		Effect.Emitters[0].Material.bEnabled = false;
		C.Check( !FParticleEffectDefinition::HasErrors( Effect.Validate() ), "材質無効なら寿命×UV速度のRibbon制限を適用しない" );
		Effect.Emitters[0].Material.bEnabled = true;
		Effect.Emitters[0].Path.Kind = FEffectPathDefinition::EKind::Lightning;
		const TArray<FParticleIssue> LightningScroll = Effect.Validate(); // Lightningは粒子秒齢を共通基準で扱う。
		C.Check( !LightningScroll.ContainsByPredicate( []( const FParticleIssue& Issue ) { return Issue.Message.Contains( "帯の材質では寿命×UV速度" ); } ), "LightningのUV制限は帯用の寿命差上限を使わない" );
	}

	// 異なる寿命の粒子が、秒齢とは別に寿命割合を渡すことを確認する。
	void TestNormalizedAge( NAssetTest::FChecker& C )
	{
		C.Group = "effect-material-normalized-age";
		// 異なる寿命を持つ粒子を直接集めるシミュレーション。
		FParticleSimulation Simulation;
		TSharedPtr<FParticleEffectDefinition> Definition = MakeShared<FParticleEffectDefinition>(); // 二つのエミッター定義。
		Definition->Emitters.Add( FParticleEmitterDefinition() );
		Definition->Emitters.Add( FParticleEmitterDefinition() );
		Simulation.Definition = Definition;
		Simulation.Emitters.SetNum( 2 );
		FParticle ShortLife; // 同じ経過秒数で寿命割合が 0.5 の粒子。
		ShortLife.Age = 0.5f;
		ShortLife.Lifetime = 1.0f;
		FParticle LongLife; // 同じ経過秒数で寿命割合が 0.25 の粒子。
		LongLife.Age = 0.5f;
		LongLife.Lifetime = 2.0f;
		Simulation.Emitters[0].Particles.Add( ShortLife );
		Simulation.Emitters[1].Particles.Add( LongLife );
		TArray<NParticle::FDrawParticle> DrawParticles; // 寿命割合を含む描画用粒子の出力。
		NParticle::CollectDrawParticles( Simulation, DrawParticles );
		FEffectScalarCurve Curve; // 寿命割合の違いを数値へ変える曲線。
		AddKey( Curve, 0.0f, 0.0f );
		AddKey( Curve, 1.0f, 1.0f );
		C.Check( DrawParticles.Num() == 2 && DrawParticles[0].Age == DrawParticles[1].Age && DrawParticles[0].NormalizedAge == 0.5f && DrawParticles[1].NormalizedAge == 0.25f && Curve.Evaluate( DrawParticles[0].NormalizedAge, 0.0f ) != Curve.Evaluate( DrawParticles[1].NormalizedAge, 0.0f ), "秒齢が同じ粒子も lifetime に応じて異なる寿命割合を曲線評価へ渡す" );
	}
}

void NWorldTest::RunEffectMaterialTests( NAssetTest::FChecker& C )
{
	TestCurveEvaluation( C );
	TestCurveValidation( C );
	TestMaterialData( C );
	TestEmitterMaterialAndUndo( C );
	TestNormalizedAge( C );
}
