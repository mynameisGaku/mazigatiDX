// SPDX-License-Identifier: NOASSERTION
#include <cmath>
#include <limits>

#include "Library/Framework/Asset/DefinitionDocument.h"
#include "Library/Framework/Effect/EffectAppearanceDefinition.h"
#include "Library/Framework/Effect/ParticleEffect.h"
#include "Library/Framework/Effect/ParticleSimulation.h"
#include "WorldSelfTestSupport.h"

namespace
{
	// 指定位置と色を、色曲線の末尾へ加える。
	void AddColorKey( FEffectColorCurve& Curve, float32 Time, const FColor& Value )
	{
		// 追加する寿命位置と RGBA 倍率。
		FEffectColorCurve::FKey Key;
		Key.Time = Time;
		Key.Value = Value;
		Curve.Keys.Add( Key );
	}

	// 指定位置と値を、大きさ曲線の末尾へ加える。
	void AddScalarKey( FEffectScalarCurve& Curve, float32 Time, float32 Value )
	{
		// 追加する寿命位置と倍率。
		FEffectScalarCurve::FKey Key;
		Key.Time = Time;
		Key.Value = Value;
		Curve.Keys.Add( Key );
	}

	// RGBA の各補間方法、境界、壊れた入力への安全性を確かめる。
	void TestColorCurve( NAssetTest::FChecker& C )
	{
		C.Group = "effect-appearance-color-curve";
		// 四成分を異なる値で持つ補間曲線。
		FEffectColorCurve Curve;
		C.Check( Curve.Evaluate( 0.5f, FColor( 0.2f, 0.3f, 0.4f, 0.5f ) ).r == 0.2f && Curve.Evaluate( 0.5f, FColor( 0.2f, 0.3f, 0.4f, 0.5f ) ).a == 0.5f, "空の色曲線は呼び出し元の RGBA 既定値を返す" );
		AddColorKey( Curve, 0.0f, FColor( 0.0f, 0.2f, 0.4f, 0.6f ) );
		AddColorKey( Curve, 1.0f, FColor( 1.0f, 0.6f, 0.2f, 0.0f ) );
		// 中間の独立した RGBA 期待値。
		const FColor Linear = Curve.Evaluate( 0.5f, FColor::White );
		C.Check( std::fabs( Linear.r - 0.5f ) < 1.0e-6f && std::fabs( Linear.g - 0.4f ) < 1.0e-6f && std::fabs( Linear.b - 0.3f ) < 1.0e-6f && std::fabs( Linear.a - 0.3f ) < 1.0e-6f, "Linear は RGBA 各成分を独立に補間する" );
		C.Check( Curve.Evaluate( -1.0f, FColor::White ).g == 0.2f && Curve.Evaluate( 2.0f, FColor::White ).b == 0.2f, "曲線の外側は最初と最後のキー色を返す" );
		Curve.Interpolation = FEffectScalarCurve::EInterpolation::Step;
		// 境界前に保つ左キー色。
		const FColor Step = Curve.Evaluate( 0.5f, FColor::White );
		C.Check( Step.r == 0.0f && Step.a == 0.6f && Curve.Evaluate( 1.0f, FColor::White ).r == 1.0f, "Step は左値を保ちキー境界で切り替える" );
		Curve.Interpolation = FEffectScalarCurve::EInterpolation::Smooth;
		// smoothstep係数0.15625の期待値。
		const FColor Smooth = Curve.Evaluate( 0.25f, FColor::White );
		C.Check( std::fabs( Smooth.r - 0.15625f ) < 1.0e-6f && std::fabs( Smooth.a - 0.50625f ) < 1.0e-6f, "Smooth は smoothstep 係数で RGBA を補間する" );
		C.Check( std::isfinite( Curve.Evaluate( std::numeric_limits<float32>::quiet_NaN(), FColor::White ).r ), "非有限時刻でも色評価は有限値を返す" );
		// 順序不正・同時刻を直接渡す曲線。
		FEffectColorCurve Unsorted;
		AddColorKey( Unsorted, 0.75f, FColor( 0.8f, 0.8f, 0.8f, 0.8f ) );
		AddColorKey( Unsorted, 0.25f, FColor( 0.2f, 0.2f, 0.2f, 0.2f ) );
		AddColorKey( Unsorted, 0.25f, FColor( 0.9f, 0.9f, 0.9f, 0.9f ) );
		C.Check( Unsorted.Evaluate( 0.25f, FColor::White ).r == 0.2f && std::isfinite( Unsorted.Evaluate( 0.5f, FColor::White ).a ) && !Unsorted.Validate().IsEmpty(), "順序不正でも評価を安全に保ち、重複と降順を検証する" );
		// 33番目の色が探索へ混ざらないことを確認する曲線。
		FEffectColorCurve TooMany;
		for ( int32 Index = 0; Index < FEffectColorCurve::MaxKeys; ++Index ) AddColorKey( TooMany, static_cast<float32>( Index ) / 32.0f, FColor::Black );
		AddColorKey( TooMany, 1.0f, FColor::White );
		C.Check( TooMany.Validate().ContainsByPredicate( []( const FString& Issue ) { return Issue.Contains( "個以下" ); } ) && TooMany.Evaluate( 1.0f, FColor::White ).r == 0.0f, "33番目が時刻1の白でも評価は先頭32キーに制限する" );
	}

	// 色曲線の JSON 精度、入力異常、キー値域を検証する。
	void TestColorCurveJsonAndValidation( NAssetTest::FChecker& C )
	{
		C.Group = "effect-appearance-color-json";
		// 浮動小数点精度を確認する色曲線。
		FEffectColorCurve Curve;
		AddColorKey( Curve, 0.1234567f, FColor( 0.1234567f, 0.2345678f, 0.3456789f, 0.4567891f ) );
		// JSONから復元する色曲線。
		FEffectColorCurve Loaded;
		C.Check( FEffectColorCurve::FromJson( NlohmannJson::parse( Curve.ToJson().dump() ), Loaded ) && Loaded.ComputeHash() == Curve.ComputeHash(), "RGBA の小数精度と曲線 hash を JSON 往復で保つ" );
		// 赤成分だけを変える色曲線。
		FEffectColorCurve RedChanged = Curve;
		RedChanged.Keys[0].Value.r += 0.01f;
		// 緑成分だけを変える色曲線。
		FEffectColorCurve GreenChanged = Curve;
		GreenChanged.Keys[0].Value.g += 0.01f;
		// 青成分だけを変える色曲線。
		FEffectColorCurve BlueChanged = Curve;
		BlueChanged.Keys[0].Value.b += 0.01f;
		// アルファだけを変える色曲線。
		FEffectColorCurve AlphaChanged = Curve;
		AlphaChanged.Keys[0].Value.a += 0.01f;
		C.Check( RedChanged.ComputeHash() != Curve.ComputeHash() && GreenChanged.ComputeHash() != Curve.ComputeHash() && BlueChanged.ComputeHash() != Curve.ComputeHash() && AlphaChanged.ComputeHash() != Curve.ComputeHash(), "RGBA の各成分を単独変更しても曲線 hash が変わる" );
		// 補間方法だけを変える色曲線。
		FEffectColorCurve InterpolationChanged = Curve;
		InterpolationChanged.Interpolation = FEffectScalarCurve::EInterpolation::Smooth;
		C.Check( InterpolationChanged.ComputeHash() != Curve.ComputeHash(), "補間方法の変更を色曲線 hash に反映する" );
		// RGBA配列の長さが違う入力。
		FEffectColorCurve InvalidValue;
		C.Check( FEffectColorCurve::FromJson( NlohmannJson{ { "Keys", NlohmannJson::array( { NlohmannJson{ { "Time", 0.5 }, { "Value", NlohmannJson::array( { 0.1, 0.2, 0.3 } ) } } } ) } }, InvalidValue ) && !InvalidValue.Validate().IsEmpty(), "成分が4つでない JSON 色を無効値として保持する" );
		// RGBA成分が5つある入力。
		FEffectColorCurve ExtraValue;
		C.Check( FEffectColorCurve::FromJson( NlohmannJson{ { "Keys", NlohmannJson::array( { NlohmannJson{ { "Time", 0.5 }, { "Value", NlohmannJson::array( { 0.1, 0.2, 0.3, 0.4, 0.5 } ) } } } ) } }, ExtraValue ) && !ExtraValue.Validate().IsEmpty(), "成分が5つの JSON 色も保存前に拒否する" );
		// RGBAの途中にnullを含む入力。
		FEffectColorCurve NullValue;
		C.Check( FEffectColorCurve::FromJson( NlohmannJson{ { "Keys", NlohmannJson::array( { NlohmannJson{ { "Time", 0.5 }, { "Value", NlohmannJson::array( { 0.1, nullptr, 0.3, 0.4 } ) } } } ) } }, NullValue ) && !NullValue.Validate().IsEmpty(), "RGBA成分のnullを無効値として保持する" );
		// 補間型が文字列でない入力。
		FEffectColorCurve WrongType;
		C.Check( FEffectColorCurve::FromJson( NlohmannJson{ { "Interpolation", 4 } }, WrongType ) && !WrongType.Validate().IsEmpty(), "補間名の型違いを Validate で拒否できる" );
		// 時刻が数値でないキーを含む色曲線。
		FEffectColorCurve MissingTime;
		C.Check( FEffectColorCurve::FromJson( NlohmannJson{ { "Keys", NlohmannJson::array( { NlohmannJson{ { "Time", "bad" }, { "Value", NlohmannJson::array( { 0.1, 0.2, 0.3, 0.4 } ) } } } ) } }, MissingTime ) && !MissingTime.Validate().IsEmpty(), "時刻の型違いを無効値として保持する" );
		// 有限だが色範囲外のキー。
		FEffectColorCurve OutOfRange;
		AddColorKey( OutOfRange, 0.5f, FColor( 1.1f, 0.0f, 0.0f, 1.0f ) );
		C.Check( OutOfRange.Validate().Num() == 1 && OutOfRange.Evaluate( 0.5f, FColor::White ).r == 1.0f, "色倍率の範囲外を検証し、実行評価は安全な範囲に収める" );
	}

	// Appearance の既定 JSON/hash と履歴、検証、評価を確かめる。
	void TestAppearanceDataAndUndo( NAssetTest::FChecker& C )
	{
		C.Group = "effect-appearance-definition";
		// 以前のフィールドだけを使う既定エフェクト。
		FParticleEffectDefinition DefaultEffect;
		DefaultEffect.Emitters.Add( FParticleEmitterDefinition() );
		// 既定 Appearance の追加前後を表す複製。
		FParticleEffectDefinition SameEffect = DefaultEffect;
		SameEffect.Emitters[0].Appearance = FEffectAppearanceDefinition();
		// Appearance追加前の旧FHasherで算出した1エミッター既定値の固定hash。
		C.Check( DefaultEffect.ComputeHash() == 0xA811CEF10E93C86Eull && DefaultEffect.ComputeHash() == SameEffect.ComputeHash() && !DefaultEffect.ToJson()["Emitters"][0].contains( "Appearance" ), "変更前に固定した既定hashを保ち、既定 Appearance を JSON から省く" );
		// 端点の倍率と全体の有効性を調べる設定。
		FEffectAppearanceDefinition Appearance;
		AddScalarKey( Appearance.Size, 0.0f, 0.0f );
		AddScalarKey( Appearance.Size, 1.0f, 32.0f );
		AddColorKey( Appearance.Color, 0.0f, FColor::Transparent );
		AddColorKey( Appearance.Color, 1.0f, FColor::White );
		C.Check( Appearance.Validate().IsEmpty() && Appearance.EvaluateSizeScale( 0.0f ) == 0.0f && Appearance.EvaluateSizeScale( 1.0f ) == 32.0f && Appearance.EvaluateColorScale( 0.0f ).a == 0.0f && Appearance.EvaluateColorScale( 1.0f ).r == 1.0f, "大きさ0..32と色0..1の境界値を受け入れ安全に評価する" );
		// Appearanceを追加した定義。
		FParticleEffectDefinition Effect = DefaultEffect;
		Effect.Emitters[0].Appearance = Appearance;
		// JSONから復元する定義。
		FParticleEffectDefinition Loaded;
		C.Check( Effect.ToJson()["Emitters"][0].contains( "Appearance" ) && FParticleEffectDefinition::FromJson( NlohmannJson::parse( Effect.ToJson().dump() ), Loaded ) && Loaded.ComputeHash() == Effect.ComputeHash(), "非既定 Appearance を effect JSON と全体 hash に接続する" );
		// サイズ倍率が上限外の定義。
		FParticleEffectDefinition InvalidRange = Effect;
		InvalidRange.Emitters[0].Appearance.Size.Keys[1].Value = 33.0f;
		C.Check( FParticleEffectDefinition::HasErrors( InvalidRange.Validate() ), "サイズ倍率33をAppearanceエラーとして検出する" );
		// Appearance自体が配列になっている入力。
		FParticleEffectDefinition InvalidObject;
		C.Check( FParticleEffectDefinition::FromJson( NlohmannJson{ { "Emitters", NlohmannJson::array( { NlohmannJson{ { "Appearance", NlohmannJson::array() } } } ) } }, InvalidObject ) && FParticleEffectDefinition::HasErrors( InvalidObject.Validate() ), "Appearanceオブジェクトの型違いを既定値として保存させない" );
		// JSON snapshotを使う既存編集履歴。
		TDefinitionDocument<FParticleEffectDefinition> Document;
		Document.Reset( Effect );
		// Undoで戻る定義hash。
		const uint64 OriginalHash = Document.Get().ComputeHash();
		// 精度を確認する編集値。
		FEffectAppearanceDefinition Edited = Appearance;
		Edited.Size.Keys[0].Value = 1.234567f;
		Edited.Color.Keys[0].Value.a = 0.3456789f;
		Document.BeginEdit().Emitters[0].Appearance = Edited;
		// Redoで戻る定義hash。
		const uint64 EditedHash = Document.Get().ComputeHash();
		C.Check( EditedHash != OriginalHash && Document.Undo() && Document.Get().ComputeHash() == OriginalHash && Document.Redo() && Document.Get().ComputeHash() == EditedHash && Document.Get().Emitters[0].Appearance.Color.Keys[0].Value.a == 0.3456789f, "Appearanceの全成分と小数精度を既存Undo/Redoで保つ" );
		// 不正補間だけを持つ空曲線。
		FEffectColorCurve InvalidEmpty;
		InvalidEmpty.Interpolation = static_cast<FEffectScalarCurve::EInterpolation>( 0xFFu );
		C.Check( InvalidEmpty.ComputeHash() != FEffectColorCurve().ComputeHash() && !InvalidEmpty.Validate().IsEmpty(), "キーが空でも不正補間をhashと検証へ残す" );
		// 不正な空曲線を含む修復前の定義。
		FParticleEffectDefinition BrokenEffect = DefaultEffect;
		BrokenEffect.Emitters[0].Appearance.Color = InvalidEmpty;
		Document.Reset( BrokenEffect );
		// 修復前の不正定義を識別する値。
		const uint64 BrokenHash = Document.Get().ComputeHash();
		// 補間方法を既定値へ戻す修復済み定義。
		FEffectAppearanceDefinition Repaired = BrokenEffect.Emitters[0].Appearance;
		Repaired.Color = FEffectColorCurve();
		Document.BeginEdit().Emitters[0].Appearance = Repaired;
		// 修復後の有効定義を識別する値。
		const uint64 RepairedHash = Document.Get().ComputeHash();
		C.Check( !FParticleEffectDefinition::HasErrors( Document.Get().Validate() ) && Document.Undo() && Document.Get().ComputeHash() == BrokenHash && FParticleEffectDefinition::HasErrors( Document.Get().Validate() ) && Document.Redo() && Document.Get().ComputeHash() == RepairedHash && !FParticleEffectDefinition::HasErrors( Document.Get().Validate() ), "不正な空曲線の修復もUndo/Redoで検証状態ごと戻す" );
	}

	// 同じ実粒子へ Appearance を適用して寿命割合・丸め順・乱数状態を確かめる。
	void TestSimulationAppearance( NAssetTest::FChecker& C )
	{
		C.Group = "effect-appearance-simulation";
		// 親指定の非対称色と中間寿命を使う基準定義。
		TSharedPtr<FParticleEffectDefinition> Definition = MakeShared<FParticleEffectDefinition>();
		Definition->Emitters.Add( FParticleEmitterDefinition() );
		// 描画用に曲線を参照するエミッター定義。
		FParticleEmitterDefinition& Emitter = Definition->Emitters[0];
		Emitter.Lifetime = { 2.0f, 2.0f };
		Emitter.StartSize = { 1.0f, 1.0f };
		Emitter.EndSize = { 3.0f, 3.0f };
		Emitter.StartColor = FColor32( 10, 20, 40, 200 );
		Emitter.EndColor = FColor32( 140, 180, 120, 40 );
		AddScalarKey( Emitter.Appearance.Size, 0.0f, 0.5f );
		AddScalarKey( Emitter.Appearance.Size, 0.25f, 2.0f );
		AddScalarKey( Emitter.Appearance.Size, 1.0f, 4.0f );
		AddColorKey( Emitter.Appearance.Color, 0.0f, FColor( 0.1f, 0.1f, 0.1f, 0.1f ) );
		AddColorKey( Emitter.Appearance.Color, 0.25f, FColor( 0.5f, 0.25f, 0.75f, 0.5f ) );
		AddColorKey( Emitter.Appearance.Color, 1.0f, FColor( 0.9f, 0.9f, 0.9f, 0.9f ) );
		// 一粒の描画値を集めるシミュレーション。
		FParticleSimulation Simulation;
		Simulation.Definition = Definition;
		Simulation.Emitters.SetNum( 1 );
		// Age 0.5 / Lifetime 2 となるテスト粒子。
		FParticle Particle;
		Particle.Age = 0.5f;
		Particle.Lifetime = 2.0f;
		Particle.StartSize = 1.0f;
		Particle.EndSize = 3.0f;
		Simulation.Emitters[0].Particles.Add( Particle );
		// 材質適用後の描画粒子。
		TArray<NParticle::FDrawParticle> Draw;
		NParticle::CollectDrawParticles( Simulation, Draw );
		C.Check( Draw.Num() == 1 && Draw[0].NormalizedAge == 0.25f && Draw[0].Size == 3.0f, "寿命割合0.25のSize倍率2を基準size1.5へ掛ける" );
		C.Check( Draw.Num() == 1 && Draw[0].Color.r == 21 && Draw[0].Color.g == 15 && Draw[0].Color.b == 45 && Draw[0].Color.a == 80, "RGBAをbyte丸め前に個別乗算し、RGBへalphaを前掛けしない" );
		// アルファ倍率だけを変えた独立エミッター定義。
		TSharedPtr<FParticleEffectDefinition> AlphaDefinition = MakeShared<FParticleEffectDefinition>( *Definition );
		AlphaDefinition->Emitters[0].Appearance.Color.Keys[1].Value.a = 0.25f;
		// 同じAgeからアルファ独立性を確認する再生状態。
		FParticleSimulation AlphaSimulation = Simulation;
		AlphaSimulation.Definition = AlphaDefinition;
		NParticle::CollectDrawParticles( AlphaSimulation, Draw );
		C.Check( Draw.Num() == 1 && Draw[0].Color.r == 21 && Draw[0].Color.g == 15 && Draw[0].Color.b == 45 && Draw[0].Color.a == 40, "alpha倍率だけを0.25へ変えてもRGBは変わらずalphaだけ40になる" );
		// Color曲線だけ有効でSize曲線が空の定義。
		TSharedPtr<FParticleEffectDefinition> ColorOnlyDefinition = MakeShared<FParticleEffectDefinition>( *Definition );
		ColorOnlyDefinition->Emitters[0].Appearance.Size.Keys.Reset();
		ColorOnlyDefinition->Emitters[0].Appearance.Size.Interpolation = FEffectScalarCurve::EInterpolation::Smooth;
		FParticleSimulation ColorOnlySimulation = Simulation;
		ColorOnlySimulation.Definition = ColorOnlyDefinition;
		ColorOnlySimulation.Emitters[0].Particles[0].StartSize = 1000000000.0f;
		ColorOnlySimulation.Emitters[0].Particles[0].EndSize = 1000000000.0f;
		NParticle::CollectDrawParticles( ColorOnlySimulation, Draw );
		C.Check( Draw.Num() == 1 && Draw[0].Size == 1000000000.0f, "Colorだけ有効でSize.Keysが空なら補間methodに関わらず旧size値を保つ" );
		// Size曲線だけ有効でColor曲線が空の定義。
		TSharedPtr<FParticleEffectDefinition> SizeOnlyDefinition = MakeShared<FParticleEffectDefinition>( *Definition );
		SizeOnlyDefinition->Emitters[0].Appearance.Color.Keys.Reset();
		SizeOnlyDefinition->Emitters[0].Appearance.Color.Interpolation = FEffectScalarCurve::EInterpolation::Smooth;
		FParticleSimulation SizeOnlySimulation = Simulation;
		SizeOnlySimulation.Definition = SizeOnlyDefinition;
		NParticle::CollectDrawParticles( SizeOnlySimulation, Draw );
		C.Check( Draw.Num() == 1 && Draw[0].Size == 3.0f && Draw[0].Color.r == 43 && Draw[0].Color.g == 60 && Draw[0].Color.b == 60 && Draw[0].Color.a == 160, "Sizeだけ有効でColor.Keysが空なら旧RGBAを保つ" );
		// 寿命端点で倍率0と1を使う独立定義。
		TSharedPtr<FParticleEffectDefinition> BoundaryDefinition = MakeShared<FParticleEffectDefinition>( *Definition );
		BoundaryDefinition->Emitters[0].Appearance.Size.Keys.Reset();
		AddScalarKey( BoundaryDefinition->Emitters[0].Appearance.Size, 0.0f, 0.0f );
		AddScalarKey( BoundaryDefinition->Emitters[0].Appearance.Size, 1.0f, 1.0f );
		BoundaryDefinition->Emitters[0].Appearance.Color.Keys.Reset();
		AddColorKey( BoundaryDefinition->Emitters[0].Appearance.Color, 0.0f, FColor::Transparent );
		AddColorKey( BoundaryDefinition->Emitters[0].Appearance.Color, 1.0f, FColor::White );
		// 誕生時と寿命超過時を曲線端点へ収める再生状態。
		FParticleSimulation BoundarySimulation = Simulation;
		BoundarySimulation.Definition = BoundaryDefinition;
		BoundarySimulation.Emitters[0].Particles[0].Age = 0.0f;
		NParticle::CollectDrawParticles( BoundarySimulation, Draw );
		// 誕生時の倍率0の期待値。
		const bool bBirth = Draw.Num() == 1 && Draw[0].NormalizedAge == 0.0f && Draw[0].Size == 0.0f && Draw[0].Color.r == 0 && Draw[0].Color.a == 0;
		BoundarySimulation.Emitters[0].Particles[0].Age = 3.0f;
		NParticle::CollectDrawParticles( BoundarySimulation, Draw );
		C.Check( bBirth && Draw.Num() == 1 && Draw[0].NormalizedAge == 1.0f && Draw[0].Size == 3.0f && Draw[0].Color.r == 140 && Draw[0].Color.a == 40, "誕生と寿命超過を正規化端点へ収めて曲線倍率を適用する" );
		// Appearanceを空にした旧描画定義。
		FParticleEffectDefinition Legacy = *Definition;
		Legacy.Emitters[0].Appearance = FEffectAppearanceDefinition();
		// Appearanceなしの旧エミッターを参照する定義。
		TSharedPtr<FParticleEffectDefinition> LegacyDefinition = MakeShared<FParticleEffectDefinition>( Legacy );
		// 同じ粒子状態を使う旧経路。
		FParticleSimulation LegacySimulation = Simulation;
		LegacySimulation.Definition = LegacyDefinition;
		// 旧式の描画値。
		TArray<NParticle::FDrawParticle> LegacyDraw;
		NParticle::CollectDrawParticles( LegacySimulation, LegacyDraw );
		C.Check( LegacyDraw.Num() == 1 && LegacyDraw[0].Size == 1.5f && LegacyDraw[0].Color.r == 43 && LegacyDraw[0].Color.g == 60 && LegacyDraw[0].Color.b == 60 && LegacyDraw[0].Color.a == 160, "空 Appearance は従来の補間・丸め結果を維持する" );
		// 直接壊れた基準値を試す複製。
		FParticleSimulation NaNSimulation = Simulation;
		NaNSimulation.Emitters[0].Particles[0].StartSize = std::numeric_limits<float32>::quiet_NaN();
		NaNSimulation.Emitters[0].Particles[0].EndSize = std::numeric_limits<float32>::infinity();
		NParticle::CollectDrawParticles( NaNSimulation, Draw );
		C.Check( Draw.Num() == 1 && std::isfinite( Draw[0].Size ) && Draw[0].Size == 0.0f, "壊れた大きさ入力でも有効曲線経路は有限な描画値を返す" );
		Definition->Emitters[0].Appearance.Size.Keys[0].Value = 32.0f;
		// 上限倍率を使う大きさ検査。
		FParticleSimulation HugeSimulation = Simulation;
		HugeSimulation.Emitters[0].Particles[0].StartSize = 1000000000.0f;
		HugeSimulation.Emitters[0].Particles[0].EndSize = 1000000000.0f;
		NParticle::CollectDrawParticles( HugeSimulation, Draw );
		C.Check( Draw.Num() == 1 && Draw[0].Size == 1000000.0f, "巨大な大きさ積を有限な描画上限へ収める" );
		// Appearanceの有無だけが異なる同一seed定義。
		TSharedPtr<FParticleEffectDefinition> PlainDefinition = MakeShared<FParticleEffectDefinition>();
		PlainDefinition->Emitters.Add( FParticleEmitterDefinition() );
		PlainDefinition->Emitters[0].Rate = 20.0f;
		PlainDefinition->Emitters[0].Lifetime = { 3.0f, 3.0f };
		// 同じ乱数列で曲線だけを追加した複製。
		TSharedPtr<FParticleEffectDefinition> CurvedDefinition = MakeShared<FParticleEffectDefinition>( *PlainDefinition );
		AddScalarKey( CurvedDefinition->Emitters[0].Appearance.Size, 0.0f, 0.5f );
		AddColorKey( CurvedDefinition->Emitters[0].Appearance.Color, 0.0f, FColor( 0.5f, 0.4f, 0.3f, 0.2f ) );
		// それぞれの定義を変更不可の再生snapshotとして渡す。
		TSharedPtr<const FParticleEffectDefinition> PlainSnapshot = PlainDefinition;
		TSharedPtr<const FParticleEffectDefinition> CurvedSnapshot = CurvedDefinition;
		// 既定の描画倍率で進める粒子状態。
		FParticleSimulation PlainSimulation;
		// 表示曲線を持つ粒子状態。
		FParticleSimulation CurvedSimulation;
		NParticle::Start( PlainSimulation, PlainSnapshot, 381u );
		NParticle::Start( CurvedSimulation, CurvedSnapshot, 381u );
		NParticle::Advance( PlainSimulation, 0.5f );
		NParticle::Advance( CurvedSimulation, 0.5f );
		// Appearanceなし側の生成粒子。
		const FParticle& PlainParticle = PlainSimulation.Emitters[0].Particles[0];
		// Appearanceあり側の生成粒子。
		const FParticle& CurvedParticle = CurvedSimulation.Emitters[0].Particles[0];
		C.Check( PlainSimulation.Emitters[0].Random == CurvedSimulation.Emitters[0].Random && PlainSimulation.Emitters[0].NextSerial == CurvedSimulation.Emitters[0].NextSerial && PlainSimulation.Emitters[0].Particles.Num() == CurvedSimulation.Emitters[0].Particles.Num() && PlainParticle.Serial == CurvedParticle.Serial && PlainParticle.Position.x == CurvedParticle.Position.x && PlainParticle.Velocity.y == CurvedParticle.Velocity.y && PlainParticle.Lifetime == CurvedParticle.Lifetime, "Appearance追加で発生数・乱数・生成状態を変えない" );
	}
}

void NWorldTest::RunEffectAppearanceTests( NAssetTest::FChecker& C )
{
	TestColorCurve( C );
	TestColorCurveJsonAndValidation( C );
	TestAppearanceDataAndUndo( C );
	TestSimulationAppearance( C );
}
