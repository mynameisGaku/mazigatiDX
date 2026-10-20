// SPDX-License-Identifier: NOASSERTION
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>

#include "Library/Framework/Asset/DefinitionDocument.h"
#include "Library/Framework/Effect/EffectUVDistortionDefinition.h"
#include "Library/Framework/Effect/ParticleEffect.h"
#include "WorldSelfTestSupport.h"

namespace
{
	// Strengthへ寿命位置と歪み幅を追加する。
	void AddStrength( FEffectUVDistortionDefinition& Definition, float32 Time, float32 Value )
	{
		// 追加する時間と歪み幅の組。
		FEffectScalarCurve::FKey Key;
		Key.Time = Time;
		Key.Value = Value;
		Definition.Strength.Keys.Add( Key );
	}

	// 指定文字列を含む問題がエラーまたは警告であることを調べる。
	bool HasIssue( const TArray<FParticleIssue>& Issues, const char* Text, bool bError )
	{
		for ( const FParticleIssue& Issue : Issues ) if ( Issue.bError == bError && std::strstr( *Issue.Message, Text ) != nullptr ) return true;
		return false;
	}

	// 既定互換、小数精度、個別hash、曲線評価と履歴を検証する。
	void TestDistortionData( NAssetTest::FChecker& C )
	{
		C.Group = "effect-uv-distortion-data";
		// 従来の材質既定値。
		const FEffectMaterialDefinition DefaultMaterial;
		// 既定 emitter を一つ持つ以前のeffect。
		FParticleEffectDefinition DefaultEffect;
		DefaultEffect.Emitters.Add( FParticleEmitterDefinition() );
		C.Check( DefaultMaterial.ComputeHash() == 0xB2861AF1252440ABull && DefaultEffect.ComputeHash() == 0xA811CEF10E93C86Eull && DefaultMaterial.Distortion.IsDefault() && DefaultMaterial.Distortion.IsValidSettings() && !DefaultMaterial.ToJson().contains( "Distortion" ) && !DefaultEffect.ToJson()["Emitters"][0].contains( "Material" ), "既定DistortionはJSONと旧Material/Effect hashへ影響しない" );
		// 明示値を持たないStrengthは既定の歪み幅を返す。
		C.Near( DefaultMaterial.Distortion.Strength.Evaluate( 0.5f, 0.05f ), static_cast<float64>( 0.05f ), 0.0, "空Strengthは0.05 UV単位を返す" );
		// 精度を保って往復させる歪み設定。
		FEffectUVDistortionDefinition Definition;
		Definition.bEnabled = true;
		Definition.Texture = "Textures/FlowNoise.png";
		Definition.UVScale = FVector2( 1.1234568f, 2.2345679f );
		Definition.UVScroll = FVector2( -0.3456789f, 0.4567891f );
		AddStrength( Definition, 0.1234568f, 0.2345679f );
		AddStrength( Definition, 0.8765432f, 0.7654321f );
		// JSON再読後の設定。
		FEffectUVDistortionDefinition Parsed;
		// float精度を保って保存したJSON。
		const NlohmannJson Saved = NlohmannJson::parse( Definition.ToJson().dump() );
		C.Check( FEffectUVDistortionDefinition::FromJson( Saved, Parsed ) && Parsed.IsValidSettings() && Parsed.ToJson() == Definition.ToJson() && Parsed.ComputeHash() == Definition.ComputeHash() && Parsed.UVScale.x == Definition.UVScale.x && Parsed.UVScroll.y == Definition.UVScroll.y && Parsed.Strength.Keys[1].Value == Definition.Strength.Keys[1].Value, "Distortionの全値とStrengthのfloat精度をJSON往復で保つ" );
		C.Near( Parsed.Strength.Evaluate( 0.5f, 0.05f ), 0.5, 1.0e-6, "Strengthは寿命の中間値をLinear補間する" );
		C.Near( Parsed.Strength.Evaluate( -1.0f, 0.05f ), 0.2345679, 1.0e-6, "Strengthは先頭境界の値を返す" );
		C.Near( Parsed.Strength.Evaluate( 2.0f, 0.05f ), 0.7654321, 1.0e-6, "Strengthは末尾境界の値を返す" );
		// 個々の保存項目だけを変える比較用の複製。
		FEffectUVDistortionDefinition Changed = Definition;
		Changed.bEnabled = false;
		const uint64 EnabledHash = Changed.ComputeHash();
		Changed = Definition;
		Changed.Texture += ".copy";
		const uint64 TextureHash = Changed.ComputeHash();
		Changed = Definition;
		Changed.UVScale.x += 0.125f;
		const uint64 ScaleXHash = Changed.ComputeHash();
		Changed = Definition;
		Changed.UVScale.y += 0.125f;
		const uint64 ScaleYHash = Changed.ComputeHash();
		Changed = Definition;
		Changed.UVScroll.x += 0.125f;
		const uint64 ScrollXHash = Changed.ComputeHash();
		Changed = Definition;
		Changed.UVScroll.y += 0.125f;
		const uint64 ScrollYHash = Changed.ComputeHash();
		Changed = Definition;
		Changed.Strength.Keys[0].Value += 0.125f;
		const uint64 StrengthHash = Changed.ComputeHash();
		Changed = Definition;
		Changed.Strength.Interpolation = FEffectScalarCurve::EInterpolation::Step;
		C.Check( EnabledHash != Definition.ComputeHash() && TextureHash != Definition.ComputeHash() && ScaleXHash != Definition.ComputeHash() && ScaleYHash != Definition.ComputeHash() && ScrollXHash != Definition.ComputeHash() && ScrollYHash != Definition.ComputeHash() && StrengthHash != Definition.ComputeHash() && Changed.ComputeHash() != Definition.ComputeHash(), "Enabled/Texture/UV各成分/Strength値と補間の個別変更をhashへ反映する" );
		// Materialへ接続した非既定設定。
		FEffectMaterialDefinition Material;
		Material.Distortion = Definition;
		// Distortionを含めて保存し直した材質。
		FEffectMaterialDefinition ParsedMaterial;
		C.Check( Material.ToJson().contains( "Distortion" ) && FEffectMaterialDefinition::FromJson( NlohmannJson::parse( Material.ToJson().dump() ), ParsedMaterial ) && ParsedMaterial.ComputeHash() != DefaultMaterial.ComputeHash() && ParsedMaterial.Distortion.ComputeHash() == Definition.ComputeHash(), "非既定DistortionをMaterial JSON/hashへ保存する" );
		// 既定材質の旧hashにDistortionが混ざらないことを再確認する複製。
		FEffectMaterialDefinition SameMaterial;
		SameMaterial.Distortion = FEffectUVDistortionDefinition();
		C.Check( SameMaterial.ComputeHash() == DefaultMaterial.ComputeHash(), "新規Distortion既定値でも既存Material hashを保つ" );
		// 空曲線でも補間方法変更を既定と区別する値。
		FEffectUVDistortionDefinition EmptyStep;
		EmptyStep.Strength.Interpolation = FEffectScalarCurve::EInterpolation::Smooth;
		C.Check( !EmptyStep.IsDefault() && EmptyStep.ToJson() != FEffectUVDistortionDefinition().ToJson(), "空Strengthでも補間方法を既定から区別して保存する" );
		// 無効なDistortionを持つeffectの編集履歴。
		FParticleEffectDefinition InvalidEffect;
		FParticleEffectDefinition::FromJson( NlohmannJson::parse( R"({"Emitters":[{"Material":{"Distortion":null}}]})" ), InvalidEffect );
		TDefinitionDocument<FParticleEffectDefinition> Document;
		Document.Reset( InvalidEffect );
		const bool bInvalidBefore = FParticleEffectDefinition::HasErrors( Document.Get().Validate() );
		Document.BeginEdit().Name = "repaired metadata only";
		const bool bInvalidEdited = FParticleEffectDefinition::HasErrors( Document.Get().Validate() );
		const bool bUndo = Document.Undo();
		const bool bInvalidUndo = FParticleEffectDefinition::HasErrors( Document.Get().Validate() );
		const bool bRedo = Document.Redo();
		const bool bInvalidRedo = FParticleEffectDefinition::HasErrors( Document.Get().Validate() );
		C.Check( bInvalidBefore && bInvalidEdited && bUndo && bInvalidUndo && bRedo && bInvalidRedo, "Distortion root型不正を編集履歴で往復しても保存拒否を保つ" );
		// Undo相当で不正Distortion全体を既定へ戻す編集。
		// 不正JSONを含むdocumentでReset対象が存在するか。
		const bool bCanReset = !Document.Get().Emitters.IsEmpty();
		if ( bCanReset ) Document.BeginEdit().Emitters[0].Material.Distortion = FEffectUVDistortionDefinition();
		const bool bValidAfterReset = bCanReset && !FParticleEffectDefinition::HasErrors( Document.Get().Validate() );
		const bool bUndoReset = Document.Undo();
		const bool bInvalidAfterUndoReset = FParticleEffectDefinition::HasErrors( Document.Get().Validate() );
		const bool bRedoReset = Document.Redo();
		const bool bValidAfterRedoReset = !FParticleEffectDefinition::HasErrors( Document.Get().Validate() );
		C.Check( bValidAfterReset && bUndoReset && bInvalidAfterUndoReset && bRedoReset && bValidAfterRedoReset, "Distortion全体ResetとUndo/Redoで型不正を修復・復元する" );
	}

	// 全保存値、型不正、値域と非対応時の警告を検証する。
	void TestDistortionValidation( NAssetTest::FChecker& C )
	{
		C.Group = "effect-uv-distortion-validation";
		// 有効化に必要な画像がない設定。
		FEffectUVDistortionDefinition MissingTexture;
		MissingTexture.bEnabled = true;
		C.Check( !MissingTexture.IsValidSettings() && !MissingTexture.Validate().IsEmpty(), "有効Distortionの空Textureを拒否する" );
		// 1項目ずつ壊した設定がvalidationで拒否されることを調べる。
		const auto CheckInvalid = [&C]( const FEffectUVDistortionDefinition& Value, const char* Description ) { C.Check( !Value.IsValidSettings() && !Value.Validate().IsEmpty(), Description ); };
		const float32 InvalidFloatValues[] = { 0.0f, -0.25f, 128.01f, std::numeric_limits<float32>::quiet_NaN(), std::numeric_limits<float32>::infinity() };
		for ( int32 Axis = 0; Axis < 2; ++Axis )
		{
			for ( const float32 Value : InvalidFloatValues )
			{
				// 一成分だけ範囲外のUVScale。
				FEffectUVDistortionDefinition InvalidScale;
				if ( Axis == 0 ) InvalidScale.UVScale.x = Value;
				else InvalidScale.UVScale.y = Value;
				CheckInvalid( InvalidScale, "UVScaleの各成分は非正・128超・NaN・Infを個別に拒否する" );
			}
			for ( const float32 Value : { -128.01f, 128.01f, std::numeric_limits<float32>::quiet_NaN(), std::numeric_limits<float32>::infinity() } )
			{
				// 一成分だけ範囲外のUVScroll。
				FEffectUVDistortionDefinition InvalidScroll;
				if ( Axis == 0 ) InvalidScroll.UVScroll.x = Value;
				else InvalidScroll.UVScroll.y = Value;
				CheckInvalid( InvalidScroll, "UVScrollの各成分は正負128超・NaN・Infを個別に拒否する" );
			}
		}
		FEffectUVDistortionDefinition Boundary;
		Boundary.UVScale = FVector2( 128.0f, 128.0f );
		Boundary.UVScroll = FVector2( -128.0f, 128.0f );
		C.Check( Boundary.IsValidSettings() && Boundary.Validate().IsEmpty(), "UVScaleとUVScrollの128境界を許可する" );
		// Strengthと時刻の両端を含む有効曲線。
		FEffectUVDistortionDefinition StrengthBoundary;
		AddStrength( StrengthBoundary, 0.0f, 0.0f );
		AddStrength( StrengthBoundary, 1.0f, 1.0f );
		C.Check( StrengthBoundary.IsValidSettings() && StrengthBoundary.Validate().IsEmpty(), "Strength値0/1と時刻0/1の境界を許可する" );
		// Strengthの値・時刻・順序を個別に壊した曲線。
		FEffectUVDistortionDefinition LowStrength;
		AddStrength( LowStrength, 0.5f, -0.01f );
		CheckInvalid( LowStrength, "負のStrength値を拒否する" );
		FEffectUVDistortionDefinition HighStrength;
		AddStrength( HighStrength, 0.5f, 1.01f );
		CheckInvalid( HighStrength, "1を超えるStrength値を拒否する" );
		FEffectUVDistortionDefinition NonFiniteStrength;
		AddStrength( NonFiniteStrength, 0.5f, std::numeric_limits<float32>::quiet_NaN() );
		CheckInvalid( NonFiniteStrength, "非有限Strength値を拒否する" );
		FEffectUVDistortionDefinition BelowTime;
		AddStrength( BelowTime, -0.1f, 0.5f );
		CheckInvalid( BelowTime, "0未満のStrength時刻を拒否する" );
		FEffectUVDistortionDefinition AboveTime;
		AddStrength( AboveTime, 1.1f, 0.5f );
		CheckInvalid( AboveTime, "1を超えるStrength時刻を拒否する" );
		FEffectUVDistortionDefinition NaNTime;
		AddStrength( NaNTime, std::numeric_limits<float32>::quiet_NaN(), 0.5f );
		CheckInvalid( NaNTime, "非有限Strength時刻を拒否する" );
		FEffectUVDistortionDefinition DuplicateTimes;
		AddStrength( DuplicateTimes, 0.5f, 0.25f );
		AddStrength( DuplicateTimes, 0.5f, 0.75f );
		CheckInvalid( DuplicateTimes, "重複Strength時刻を拒否する" );
		FEffectUVDistortionDefinition DescendingTimes;
		AddStrength( DescendingTimes, 0.75f, 0.25f );
		AddStrength( DescendingTimes, 0.25f, 0.75f );
		CheckInvalid( DescendingTimes, "降順Strength時刻を拒否する" );
		FEffectUVDistortionDefinition InvalidDirectInterpolation;
		InvalidDirectInterpolation.Strength.Interpolation = static_cast<FEffectScalarCurve::EInterpolation>( 0xFFu );
		CheckInvalid( InvalidDirectInterpolation, "未知のStrength補間方式を拒否する" );
		// 読み込み型とroot型を壊した入力群。
		const NlohmannJson BadTypes = NlohmannJson::parse( R"({"Enabled":null,"Texture":7,"UVScale":[1],"UVScroll":false,"Strength":null})" );
		FEffectUVDistortionDefinition InvalidTypes;
		FEffectUVDistortionDefinition InvalidTypesAgain;
		TArray<FString> Warnings;
		const bool bRead = FEffectUVDistortionDefinition::FromJson( BadTypes, InvalidTypes, &Warnings );
		const bool bRoundRead = FEffectUVDistortionDefinition::FromJson( NlohmannJson::parse( InvalidTypes.ToJson().dump() ), InvalidTypesAgain, nullptr );
		C.Check( bRead && !Warnings.IsEmpty() && !InvalidTypes.IsValidSettings() && bRoundRead && !InvalidTypesAgain.IsValidSettings(), "既知fieldの型違いをnull sentinelへ保存し再読後も拒否する" );
		// 既定JSONの一つのfieldだけを型違いにするfixture。
		const char* InvalidFieldNames[] = { "Enabled", "Texture", "UVScale", "UVScale", "UVScroll", "UVScroll", "Strength", "Strength" };
		// 各fieldで拒否するnull・別型・配列形状。
		const NlohmannJson InvalidFieldValues[] = { nullptr, 17, NlohmannJson::array( { 1.0f } ), NlohmannJson::array( { 1.0f, true } ), nullptr, "scroll", nullptr, NlohmannJson::array() };
		for ( usize Index = 0; Index < sizeof( InvalidFieldNames ) / sizeof( InvalidFieldNames[0] ); ++Index )
		{
			// 正しい他fieldを保った単独誤型入力。
			NlohmannJson SingleBadField = FEffectUVDistortionDefinition().ToJson();
			SingleBadField[InvalidFieldNames[Index]] = InvalidFieldValues[Index];
			// 型違いを読んだ定義。
			FEffectUVDistortionDefinition SingleInvalid;
			// 保存後に再読した不正定義。
			FEffectUVDistortionDefinition SingleInvalidAgain;
			const bool bSingleRead = FEffectUVDistortionDefinition::FromJson( SingleBadField, SingleInvalid );
			const bool bSingleRoundRead = FEffectUVDistortionDefinition::FromJson( NlohmannJson::parse( SingleInvalid.ToJson().dump() ), SingleInvalidAgain );
			C.Check( bSingleRead && !SingleInvalid.IsValidSettings() && !SingleInvalid.Validate().IsEmpty() && bSingleRoundRead && !SingleInvalidAgain.IsValidSettings() && !SingleInvalidAgain.Validate().IsEmpty(), "単独fieldの誤型を読み込み・JSON再読後も拒否する" );
		}
		// scalar curveが保持する壊れた補間名とKeys型。
		FEffectUVDistortionDefinition InvalidInterpolation;
		FEffectUVDistortionDefinition InvalidInterpolationAgain;
		const bool bInterpolationRead = FEffectUVDistortionDefinition::FromJson( NlohmannJson::parse( R"({"Strength":{"Interpolation":"Cubic","Keys":[]}})" ), InvalidInterpolation );
		const bool bInterpolationAgain = FEffectUVDistortionDefinition::FromJson( NlohmannJson::parse( InvalidInterpolation.ToJson().dump() ), InvalidInterpolationAgain );
		FEffectUVDistortionDefinition InvalidKeys;
		FEffectUVDistortionDefinition InvalidKeysAgain;
		const bool bKeysRead = FEffectUVDistortionDefinition::FromJson( NlohmannJson::parse( R"({"Strength":{"Keys":null}})" ), InvalidKeys );
		const bool bKeysAgain = FEffectUVDistortionDefinition::FromJson( NlohmannJson::parse( InvalidKeys.ToJson().dump() ), InvalidKeysAgain );
		C.Check( bInterpolationRead && !InvalidInterpolation.IsValidSettings() && bInterpolationAgain && !InvalidInterpolationAgain.IsValidSettings() && bKeysRead && !InvalidKeys.IsValidSettings() && bKeysAgain && !InvalidKeysAgain.IsValidSettings(), "Strengthの未知補間とnull KeysをJSON再読後も拒否する" );
		// 33個の正しい時刻キーを曲線上限超過として残す入力。
		NlohmannJson TooManyKeys = NlohmannJson::array();
		for ( int32 Index = 0; Index <= FEffectScalarCurve::MaxKeys; ++Index ) TooManyKeys.push_back( NlohmannJson{ { "Time", static_cast<float32>( Index ) / FEffectScalarCurve::MaxKeys }, { "Value", 0.5f } } );
		NlohmannJson TooManyStrength = NlohmannJson::object();
		TooManyStrength["Keys"] = TooManyKeys;
		FEffectUVDistortionDefinition TooMany;
		FEffectUVDistortionDefinition TooManyAgain;
		const bool bTooManyRead = FEffectUVDistortionDefinition::FromJson( NlohmannJson{ { "Strength", TooManyStrength } }, TooMany );
		const bool bTooManyAgain = FEffectUVDistortionDefinition::FromJson( NlohmannJson::parse( TooMany.ToJson().dump() ), TooManyAgain );
		C.Check( bTooManyRead && TooMany.Strength.Keys.Num() == FEffectScalarCurve::MaxKeys + 1 && !TooMany.IsValidSettings() && bTooManyAgain && !TooManyAgain.IsValidSettings(), "上限を超えたStrengthキーを丸めず保存拒否に残す" );
		// object以外のrootも不正状態として保存に残す。
		FEffectUVDistortionDefinition InvalidRoot;
		FEffectUVDistortionDefinition InvalidRootAgain;
		C.Check( FEffectUVDistortionDefinition::FromJson( NlohmannJson::array(), InvalidRoot ) && !InvalidRoot.IsValidSettings() && FEffectUVDistortionDefinition::FromJson( NlohmannJson::parse( InvalidRoot.ToJson().dump() ), InvalidRootAgain ) && !InvalidRootAgain.IsValidSettings(), "object以外のroot型をJSON往復後も拒否する" );
		// 未知fieldだけを含む既定object。
		FEffectUVDistortionDefinition UnknownOnly;
		C.Check( FEffectUVDistortionDefinition::FromJson( NlohmannJson::parse( R"({"FutureOption":17})" ), UnknownOnly ) && UnknownOnly.IsDefault() && UnknownOnly.Validate().IsEmpty(), "未知の追加fieldを無視し既定値を保つ" );
		// Effect内で描画されない設定を保持する状態。
		FParticleEffectDefinition Effect;
		Effect.Emitters.Add( FParticleEmitterDefinition() );
		Effect.Emitters[0].Material.Distortion = FEffectUVDistortionDefinition();
		Effect.Emitters[0].Material.Distortion.bEnabled = true;
		Effect.Emitters[0].Material.Distortion.Texture = "Textures/FlowNoise.png";
		const TArray<FParticleIssue> MaterialOffIssues = Effect.Validate();
		C.Check( !FParticleEffectDefinition::HasErrors( MaterialOffIssues ) && HasIssue( MaterialOffIssues, "材質を有効", false ) && Effect.Emitters[0].Material.Distortion.bEnabled, "Material無効時は歪み値を保持し未適用を警告する" );
		Effect.Emitters[0].Material.bEnabled = true;
		Effect.Emitters[0].Path.Kind = FEffectPathDefinition::EKind::Ribbon;
		const TArray<FParticleIssue> RibbonIssues = Effect.Validate();
		C.Check( !FParticleEffectDefinition::HasErrors( RibbonIssues ) && HasIssue( RibbonIssues, "帯と雷", false ) && Effect.Emitters[0].Material.Distortion.bEnabled, "帯/雷では保存値を保持し未対応を警告する" );
	}
}

// UV歪みの純粋な値、JSON、hash、validationと警告を検証する。
void NWorldTest::RunEffectUVDistortionTests( NAssetTest::FChecker& C )
{
	TestDistortionData( C );
	TestDistortionValidation( C );
}
