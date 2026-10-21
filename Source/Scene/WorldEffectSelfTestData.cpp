#include "Library/Framework/World/Editor/EditorOutliner.h"
#include "Library/Framework/World/Editor/EditorPlacement.h"
#include "Library/Framework/World/Editor/EditorShapes.h"
#include "Library/Framework/World/WorldEdit.h"
#include "Library/Framework/World/WorldValidate.h"
#include "WorldEditorSelfTestSupport.h"
#include "WorldEffectSelfTestSupport.h"

using namespace NAssetTest;
using namespace NWorldMeshTest;
using namespace NEffectTest;

//=============================================================================
// エフェクトの定義の確認（JSON・内容の識別・検証。DxLib もシーンも使わない）
//=============================================================================
namespace
{
	void TestData( FChecker& C )
	{
		C.Group = "effect-data";
		// 既定のエミッターは、名前だけが書かれる（既定と同じ項目は書かない）
		FParticleEffectDefinition Plain;
		Plain.Emitters.Add( FParticleEmitterDefinition() );
		const NlohmannJson PlainJson = Plain.ToJson();
		C.Check( PlainJson["Version"] == ParticleEffectFormatVersion && PlainJson["Emitters"].is_array() && PlainJson["Emitters"].size() == 1 && PlainJson["Emitters"][0].size() == 1 && PlainJson["Emitters"][0]["Name"] == "Emitter" && PlainJson.size() == 2,
				 "an effect with a default emitter is written as just its version and the emitter's name (%s)", PlainJson.dump().c_str() );
		C.Check( PlainJson["Emitters"][0] == NlohmannJson{ { "Name", "Emitter" } } && Plain.ComputeHash() == 0xA811CEF10E93C86Eull,
				 "an empty ForceFields array keeps the old default JSON and golden hash" );

		// 全部の項目の往復
		FParticleEffectDefinition Full;
		Full.Name = "Fireworks";
		Full.Duration = 4.5f;
		Full.bLoop = false;
		Full.Seed = 12345;
		Full.Emitters.Add( FullEmitter() );
		Full.Emitters.Add( FParticleEmitterDefinition() );
		const NlohmannJson FullJson = Full.ToJson();
		FParticleEffectDefinition Back;
		TArray<FString> Warnings;
		C.Check( FParticleEffectDefinition::FromJson( ParseJsonText( FullJson.dump().c_str() ), Back, &Warnings ) && Warnings.IsEmpty() && Back.ToJson() == FullJson && Back.ComputeHash() == Full.ComputeHash(),
				 "an effect with every item changed is written, read back with no warnings, and written the same (%d warnings)", Warnings.Num() );
		C.Check( Back.Name == "Fireworks" && Back.Duration == 4.5f && !Back.bLoop && Back.Seed == 12345u && Back.Emitters.Num() == 2 && Back.Emitters[0].Name == "Sparks" && Back.Emitters[1].Name == "Emitter", "the effect's own items come back (name, duration, loop, seed, emitters)" );
		const FParticleEmitterDefinition& E = Back.Emitters[0];
		C.Check( E.Texture == "Textures/Spark.png" && E.Blend == EParticleBlend::Alpha && E.Space == EParticleSpace::Local && E.Orient == EParticleOrient::Fixed && E.MaxParticles == 77 && E.Delay == 0.25f && E.Rate == 12.5f,
				 "the emitter's items come back (image, blend, space, orient, max, delay, rate)" );
		C.Check( E.Bursts.Num() == 2 && E.Bursts[0].Time == 0.5f && E.Bursts[0].Count == 7 && E.Bursts[1].Time == 1.5f && E.Bursts[1].Count == 3 && E.Lifetime.Min == 0.75f && E.Lifetime.Max == 1.5f && E.Speed.Min == 2.0f && E.Speed.Max == 4.0f,
				 "bursts, lifetime and speed come back" );
		C.Check( E.Shape == EParticleShape::Box && E.ShapeRadius == 0.5f && E.ConeAngle == 33.0f && Near3( E.BoxSize, FVector3( 1, 2, 3 ) ) && Near3( E.Direction, FVector3( 0, 0, 1 ) ) && Near3( E.Gravity, FVector3( 0.5f, -9.0f, 0.25f ) ) && E.Drag == 0.5f,
				 "the shape, direction, gravity and drag come back" );
		C.Check( E.StartSize.Min == 0.5f && E.StartSize.Max == 0.75f && E.EndSize.Min == 0.125f && E.EndSize.Max == 0.25f && E.StartColor.r == 255 && E.StartColor.g == 128 && E.StartColor.b == 64 && E.StartColor.a == 200 && E.EndColor.r == 10 && E.EndColor.a == 0
					 && E.StartAngle.Max == 360.0f && E.Spin.Min == -90.0f && E.Spin.Max == 90.0f,
				 "sizes, colours (with alpha), angle and spin come back" );
		const FJson J( FullJson );
		C.Check( J["Emitters"][0]["Shape"]["Type"] == "Box" && J["Emitters"][0]["Size"]["Start"][0] == 0.5 && J["Emitters"][0]["Color"]["End"] == "#0A141E00" && J["Duration"] == 4.5 && J["Loop"] == false && J["Seed"] == 12345,
				 "the written form is the documented one (Shape.Type, Size.Start, Color.End as #RRGGBBAA, Duration, Loop, Seed) (%s)", FullJson.dump().c_str() );
		C.Check( FullJson["Emitters"][0]["Lifetime"].is_array() && FullJson["Emitters"][1].size() == 1, "a range is written as [min, max]; a default emitter as just its name" );

		// 力場の全値、順番、無効項目もJSONとhashへ保存する。
		FParticleEmitterDefinition FieldsEmitter; // 力場を保存するテスト用エミッター
		FEffectForceFieldDefinition Uniform; // 一様加速度の保存値
		Uniform.Kind = FEffectForceFieldDefinition::EKind::Uniform;
		Uniform.Acceleration = FVector3( 1.25f, -2.5f, 3.75f );
		FEffectForceFieldDefinition Radial; // 無効化した半径場の保存値
		Radial.Kind = FEffectForceFieldDefinition::EKind::Radial;
		Radial.bEnabled = false;
		Radial.Position = FVector3( -4.0f, 5.0f, -6.0f );
		Radial.Radius = 7.5f;
		Radial.Strength = -8.25f;
		FieldsEmitter.ForceFields.Add( Uniform );
		FieldsEmitter.ForceFields.Add( Radial );
		FParticleEffectDefinition FieldsEffect; // 二種類の場を持つテスト用定義
		FieldsEffect.Emitters.Add( FieldsEmitter );
		const NlohmannJson FieldsJson = FieldsEffect.ToJson(); // 保存した定義のJSON
		FParticleEffectDefinition FieldsBack; // JSONから戻す定義
		Warnings.Reset();
		const bool bFieldsRead = FParticleEffectDefinition::FromJson( ParseJsonText( FieldsJson.dump().c_str() ), FieldsBack, &Warnings ); // JSON読込結果
		const FParticleEmitterDefinition& FieldsResult = FieldsBack.Emitters.IsEmpty() ? FieldsEmitter : FieldsBack.Emitters[0]; // 読み戻したエミッター
		C.Check( FieldsJson["Emitters"][0].contains( "ForceFields" ) && FieldsJson["Emitters"][0]["ForceFields"].is_array() && FieldsJson["Emitters"][0]["ForceFields"].size() == 2 && bFieldsRead && Warnings.IsEmpty() && FieldsBack.ToJson() == FieldsJson && FieldsBack.ComputeHash() == FieldsEffect.ComputeHash(), "uniform and radial force fields keep their order and round trip without warnings" );
		C.Check( FieldsResult.ForceFields.Num() == 2 && FieldsResult.ForceFields[0].Kind == FEffectForceFieldDefinition::EKind::Uniform && FieldsResult.ForceFields[0].bEnabled && Near3( FieldsResult.ForceFields[0].Acceleration, FVector3( 1.25f, -2.5f, 3.75f ) ) && FieldsResult.ForceFields[1].Kind == FEffectForceFieldDefinition::EKind::Radial && !FieldsResult.ForceFields[1].bEnabled && Near3( FieldsResult.ForceFields[1].Position, FVector3( -4.0f, 5.0f, -6.0f ) ) && FieldsResult.ForceFields[1].Radius == 7.5f && FieldsResult.ForceFields[1].Strength == -8.25f, "disabled force fields preserve every configured value" );
		FParticleEffectDefinition ReorderedFields = FieldsEffect; // 順番だけを入れ替える定義
		std::swap( ReorderedFields.Emitters[0].ForceFields[0], ReorderedFields.Emitters[0].ForceFields[1] );
		C.Check( ReorderedFields.ComputeHash() != FieldsEffect.ComputeHash(), "force field order changes the effect hash" );
		FParticleEffectDefinition ExtraField = FieldsEffect; // 場の数だけを増やす定義
		ExtraField.Emitters[0].ForceFields.Add( Uniform );
		C.Check( ExtraField.ComputeHash() != FieldsEffect.ComputeHash(), "force field count changes the effect hash" );
		FParticleEffectDefinition DisabledEdit = FieldsEffect; // 無効場の値だけを変える定義
		DisabledEdit.Emitters[0].ForceFields[1].Strength -= 1.0f;
		C.Check( DisabledEdit.ComputeHash() != FieldsEffect.ComputeHash(), "an unused disabled force field value changes the effect hash" );
		struct FForceFieldHashCase
		{
			const char* Name; // 変更する場の項目名
			std::function<void( FEffectForceFieldDefinition& )> Edit; // 項目を一つだけ変える処理
		};
		const FForceFieldHashCase ForceFieldHashCases[] = { { "enabled", []( FEffectForceFieldDefinition& F ) { F.bEnabled = false; } }, { "kind", []( FEffectForceFieldDefinition& F ) { F.Kind = FEffectForceFieldDefinition::EKind::Radial; } }, { "acceleration x", []( FEffectForceFieldDefinition& F ) { F.Acceleration.x += 1.0f; } }, { "acceleration y", []( FEffectForceFieldDefinition& F ) { F.Acceleration.y += 1.0f; } }, { "acceleration z", []( FEffectForceFieldDefinition& F ) { F.Acceleration.z += 1.0f; } }, { "position x", []( FEffectForceFieldDefinition& F ) { F.Position.x += 1.0f; } }, { "position y", []( FEffectForceFieldDefinition& F ) { F.Position.y += 1.0f; } }, { "position z", []( FEffectForceFieldDefinition& F ) { F.Position.z += 1.0f; } }, { "radius", []( FEffectForceFieldDefinition& F ) { F.Radius += 1.0f; } }, { "strength", []( FEffectForceFieldDefinition& F ) { F.Strength += 1.0f; } }, };
		FString UnchangedForceFields; // hashが変わらなかった値名
		for ( const FForceFieldHashCase& Case : ForceFieldHashCases )
		{
			FEffectForceFieldDefinition Edited = Uniform; // 1値だけ変更する場
			Case.Edit( Edited );
			FParticleEffectDefinition EditedEffect = FieldsEffect; // 1値だけを含む定義hash
			EditedEffect.Emitters[0].ForceFields[0] = Edited;
			if ( Edited.ComputeHash() == Uniform.ComputeHash() || EditedEffect.ComputeHash() == FieldsEffect.ComputeHash() ) UnchangedForceFields += FString( " " ) + Case.Name + ";";
		}
		C.Check( UnchangedForceFields.IsEmpty(), "every force field value changes its own hash when edited (%d items; unchanged:%s)", static_cast<int32>( sizeof( ForceFieldHashCases ) / sizeof( ForceFieldHashCases[0] ) ), *UnchangedForceFields );

		// 上限を超えた入力も保存して検証で報告し、実行側で先頭だけ使える。
		FParticleEffectDefinition TooManyFields; // 上限超過を確認する定義
		FParticleEmitterDefinition TooManyEmitter; // 上限を超える場を持つエミッター
		for ( int32 i = 0; i < FEffectForceFieldDefinition::MaxFields + 1; ++i ) TooManyEmitter.ForceFields.Add( Uniform );
		TooManyFields.Emitters.Add( TooManyEmitter );
		const NlohmannJson TooManyJson = TooManyFields.ToJson(); // 上限超過を保つJSON
		C.Check( TooManyJson["Emitters"][0].contains( "ForceFields" ) && TooManyJson["Emitters"][0]["ForceFields"].is_array() && TooManyJson["Emitters"][0]["ForceFields"].size() == FEffectForceFieldDefinition::MaxFields + 1 && FParticleEffectDefinition::HasErrors( TooManyFields.Validate() ), "more than the runtime force field limit remains in JSON and is reported by validation" );

		// 型が壊れた設定はnullで往復し、次に読んでも不正状態が保たれる。
		FParticleEffectDefinition InvalidFieldType; // 配列でない設定を読む定義
		Warnings.Reset();
		const bool bInvalidRead = FParticleEffectDefinition::FromJson( ParseJsonText( "{\"Emitters\":[{\"ForceFields\":\"bad\"}]}" ), InvalidFieldType, &Warnings ); // 不正な配列型の読込結果
		const NlohmannJson InvalidFieldJson = InvalidFieldType.ToJson(); // 不正項目を保持したJSON
		FParticleEffectDefinition InvalidFieldAgain; // 不正JSONを再読する定義
		Warnings.Reset();
		const bool bInvalidAgain = FParticleEffectDefinition::FromJson( ParseJsonText( InvalidFieldJson.dump().c_str() ), InvalidFieldAgain, &Warnings ); // 二度目の読込結果
		C.Check( bInvalidRead && bInvalidAgain && InvalidFieldJson["Emitters"][0].contains( "ForceFields" ) && InvalidFieldJson["Emitters"][0]["ForceFields"].is_array() && InvalidFieldJson["Emitters"][0]["ForceFields"].size() == 1 && InvalidFieldJson["Emitters"][0]["ForceFields"][0].is_object() && InvalidFieldJson["Emitters"][0]["ForceFields"][0].contains( "Kind" ) && InvalidFieldJson["Emitters"][0]["ForceFields"][0]["Kind"].is_null() && FParticleEffectDefinition::HasErrors( InvalidFieldType.Validate() ) && FParticleEffectDefinition::HasErrors( InvalidFieldAgain.Validate() ) && InvalidFieldAgain.ToJson() == InvalidFieldJson, "a force field with the wrong container type becomes one invalid entry and stays invalid after another read" );
		FParticleEffectDefinition ValidForceFieldType; // 同じエフェクトの正規JSON型
		ValidForceFieldType.Emitters.Add( FParticleEmitterDefinition() );
		C.Check( InvalidFieldType.ComputeHash() != ValidForceFieldType.ComputeHash(), "an invalid-field type marker contributes to the hash" );
		FParticleEffectDefinition InvalidContainerObject; // 配列位置にオブジェクトがある定義
		Warnings.Reset();
		const bool bInvalidObjectRead = FParticleEffectDefinition::FromJson( ParseJsonText( "{\"Emitters\":[{\"ForceFields\":{\"Kind\":\"Uniform\",\"Acceleration\":[1,0,0]}}]}" ), InvalidContainerObject, &Warnings );
		const NlohmannJson InvalidContainerObjectJson = InvalidContainerObject.ToJson();
		C.Check( bInvalidObjectRead && !Warnings.IsEmpty() && InvalidContainerObjectJson["Emitters"][0].contains( "ForceFields" ) && InvalidContainerObjectJson["Emitters"][0]["ForceFields"].is_array() && InvalidContainerObjectJson["Emitters"][0]["ForceFields"].size() == 1 && InvalidContainerObjectJson["Emitters"][0]["ForceFields"][0].is_object() && InvalidContainerObjectJson["Emitters"][0]["ForceFields"][0].contains( "Kind" ) && InvalidContainerObjectJson["Emitters"][0]["ForceFields"][0]["Kind"].is_null() && InvalidContainerObjectJson["Emitters"][0]["ForceFields"][0].contains( "Acceleration" ) && InvalidContainerObjectJson["Emitters"][0]["ForceFields"][0]["Acceleration"].is_null() && FParticleEffectDefinition::HasErrors( InvalidContainerObject.Validate() ), "an object where the ForceFields array belongs is retained as one invalid field" );
		FParticleEffectDefinition InvalidEntry; // 配列要素型が違う定義
		Warnings.Reset();
		const bool bInvalidEntryRead = FParticleEffectDefinition::FromJson( ParseJsonText( "{\"Emitters\":[{\"ForceFields\":[7]}]}" ), InvalidEntry, &Warnings );
		const NlohmannJson InvalidEntryJson = InvalidEntry.ToJson(); // 不正要素を保持したJSON
		FParticleEffectDefinition InvalidEntryAgain; // 保存JSONを再読する定義
		const bool bInvalidEntryAgain = FParticleEffectDefinition::FromJson( InvalidEntryJson, InvalidEntryAgain );
		C.Check( bInvalidEntryRead && bInvalidEntryAgain && !Warnings.IsEmpty() && InvalidEntryJson["Emitters"][0].contains( "ForceFields" ) && InvalidEntryJson["Emitters"][0]["ForceFields"].is_array() && InvalidEntryJson["Emitters"][0]["ForceFields"].size() == 1 && InvalidEntryJson["Emitters"][0]["ForceFields"][0].is_object() && InvalidEntryJson["Emitters"][0]["ForceFields"][0].contains( "Kind" ) && InvalidEntryJson["Emitters"][0]["ForceFields"][0]["Kind"].is_null() && FParticleEffectDefinition::HasErrors( InvalidEntry.Validate() ) && FParticleEffectDefinition::HasErrors( InvalidEntryAgain.Validate() ) && InvalidEntryAgain.ToJson() == InvalidEntryJson, "a non-object force field entry remains invalid through a JSON round trip" );
		const char* InvalidFields[] = { "{\"Enabled\":1}", "{\"Kind\":\"Attractor\"}", "{\"Acceleration\":[1,2]}", "{\"Position\":\"bad\"}", "{\"Radius\":\"large\"}", "{\"Strength\":\"large\"}" }; // 各設定の不正JSON
		const char* InvalidKeys[] = { "Enabled", "Kind", "Acceleration", "Position", "Radius", "Strength" }; // null保存を調べる項目名
		bool bInvalidValuesRoundTrip = true; // 全不正値が往復するか
		for ( int32 i = 0; i < 6; ++i )
		{
			NlohmannJson Input = NlohmannJson::object(); // 個別の不正値を含む入力JSON
			Input["Emitters"] = NlohmannJson::array();
			NlohmannJson EmitterJson = NlohmannJson::object(); // 力場配列を持つエミッターJSON
			EmitterJson["ForceFields"] = NlohmannJson::array();
			EmitterJson["ForceFields"].push_back( ParseJsonText( InvalidFields[i] ) );
			Input["Emitters"].push_back( EmitterJson );
			FParticleEffectDefinition InvalidValue; // 不正な項目値を読む定義
			TArray<FString> InvalidWarnings; // 不正値の読込警告
			const bool bReadInvalidValue = FParticleEffectDefinition::FromJson( Input, InvalidValue, &InvalidWarnings );
			const NlohmannJson InvalidValueJson = InvalidValue.ToJson(); // nullを含む保存JSON
			FParticleEffectDefinition InvalidValueAgain; // 保存JSONを再読する定義
			const bool bReadInvalidValueAgain = FParticleEffectDefinition::FromJson( InvalidValueJson, InvalidValueAgain );
			bInvalidValuesRoundTrip = bInvalidValuesRoundTrip && bReadInvalidValue && bReadInvalidValueAgain && !InvalidWarnings.IsEmpty() && InvalidValueJson["Emitters"][0].contains( "ForceFields" ) && InvalidValueJson["Emitters"][0]["ForceFields"].is_array() && InvalidValueJson["Emitters"][0]["ForceFields"].size() == 1 && InvalidValueJson["Emitters"][0]["ForceFields"][0].is_object() && InvalidValueJson["Emitters"][0]["ForceFields"][0].contains( InvalidKeys[i] ) && InvalidValueJson["Emitters"][0]["ForceFields"][0][InvalidKeys[i]].is_null()
					 && FParticleEffectDefinition::HasErrors( InvalidValue.Validate() ) && FParticleEffectDefinition::HasErrors( InvalidValueAgain.Validate() ) && InvalidValueAgain.ToJson() == InvalidValueJson;
		}
		C.Check( bInvalidValuesRoundTrip, "invalid Enabled, Kind, vector, Radius, and Strength values each remain null and invalid after a JSON round trip" );
		FParticleEffectDefinition DisabledInvalid; // 無効化した不正設定を読む定義
		Warnings.Reset();
		const bool bReadDisabledInvalid = FParticleEffectDefinition::FromJson( ParseJsonText( "{\"Emitters\":[{\"ForceFields\":[{\"Enabled\":false,\"Kind\":\"Radial\",\"Radius\":0,\"Strength\":-3}]}]}" ), DisabledInvalid, &Warnings ); // 無効項目を含むJSON読込
		const NlohmannJson DisabledInvalidJson = DisabledInvalid.ToJson(); // 無効項目を保持する保存JSON
		FParticleEffectDefinition DisabledInvalidAgain; // 保存JSONを再読する定義
		const bool bReadDisabledInvalidAgain = FParticleEffectDefinition::FromJson( DisabledInvalidJson, DisabledInvalidAgain ); // 再読結果
		C.Check( bReadDisabledInvalid && bReadDisabledInvalidAgain && Warnings.IsEmpty() && DisabledInvalidJson["Emitters"][0].contains( "ForceFields" ) && DisabledInvalidJson["Emitters"][0]["ForceFields"].is_array() && DisabledInvalidJson["Emitters"][0]["ForceFields"].size() == 1 && DisabledInvalidJson["Emitters"][0]["ForceFields"][0].is_object() && DisabledInvalidJson["Emitters"][0]["ForceFields"][0].contains( "Enabled" ) && DisabledInvalidJson["Emitters"][0]["ForceFields"][0]["Enabled"] == false && DisabledInvalidJson["Emitters"][0]["ForceFields"][0].contains( "Radius" ) && DisabledInvalidJson["Emitters"][0]["ForceFields"][0]["Radius"] == 0 && FParticleEffectDefinition::HasErrors( DisabledInvalid.Validate() ) && FParticleEffectDefinition::HasErrors( DisabledInvalidAgain.Validate() ) && DisabledInvalidAgain.ToJson() == DisabledInvalidJson, "a disabled force field retains its invalid configured radius for validation and serialization" );

		// 範囲: 同じなら数 1 つで書く
		FParticleEffectDefinition Single;
		FParticleEmitterDefinition SE;
		SE.Lifetime = { 2.0f, 2.0f };
		Single.Emitters.Add( SE );
		const NlohmannJson SingleJson = Single.ToJson();
		C.Check( SingleJson["Emitters"][0]["Lifetime"].is_number() && SingleJson["Emitters"][0]["Lifetime"] == 2.0, "a range whose ends are equal is written as one number" );

		// 読み方
		FParticleEffectDefinition Read;
		Warnings.Reset();
		C.Check( FParticleEffectDefinition::FromJson( ParseJsonText( "{\"Emitters\":[{\"Lifetime\":2,\"Speed\":[1,3],\"Blend\":\"alpha\",\"Space\":\"LOCAL\",\"Orient\":\"cameray\",\"Shape\":{\"Type\":\"sphere\",\"Radius\":2},\"Color\":{\"Start\":\"#FF000080\"},\"Unknown\":5}],\"Other\":1}" ), Read, &Warnings )
					 && Warnings.IsEmpty() && Read.Emitters.Num() == 1,
				 "a hand-written effect is read (unknown items are ignored without a warning)" );
		const FParticleEmitterDefinition& R = Read.Emitters.IsEmpty() ? E : Read.Emitters[0];
		C.Check( R.Lifetime.Min == 2.0f && R.Lifetime.Max == 2.0f && R.Speed.Min == 1.0f && R.Speed.Max == 3.0f && R.Blend == EParticleBlend::Alpha && R.Space == EParticleSpace::Local && R.Orient == EParticleOrient::CameraY && R.Shape == EParticleShape::Sphere
					 && R.ShapeRadius == 2.0f && R.StartColor.r == 255 && R.StartColor.g == 0 && R.StartColor.a == 128 && R.EndColor.a == 0,
				 "a number is a range with equal ends, names ignore case, a partly given colour keeps the rest of its defaults" );
		C.Check( Read.bLoop && Read.Seed == 1u && Read.Duration == 0.0f && Read.Version == ParticleEffectFormatVersion, "items left out take their defaults (loop on, seed 1, endless)" );

		// 読めない項目: 既定のまま・警告
		Warnings.Reset();
		FParticleEffectDefinition Bad;
		C.Check( FParticleEffectDefinition::FromJson( ParseJsonText( "{\"Emitters\":[{\"Lifetime\":\"long\",\"Speed\":[1],\"Blend\":\"Multiply\",\"Space\":5,\"Shape\":{\"Type\":\"Cube\"},\"Color\":{\"Start\":\"red\"},\"Bursts\":3},7],\"Version\":9}" ), Bad, &Warnings ), "a damaged effect still reads" );
		C.Check( Warnings.Num() >= 7 && Bad.Emitters.Num() == 2 && Bad.Emitters[0].Lifetime.Min == FParticleEmitterDefinition().Lifetime.Min && Bad.Emitters[0].Blend == EParticleBlend::Add && Bad.Emitters[0].Shape == EParticleShape::Cone && Bad.Emitters[0].StartColor.a == 255
					 && Bad.Emitters[1].Name == "Emitter",
				 "each unreadable item keeps its default and is reported (%d warnings), and an emitter that is not an object becomes a default one", Warnings.Num() );
		bool bNewer = false, bEmitter1 = false;
		for ( const FString& W : Warnings )
		{
			bNewer = bNewer || W.Contains( "newer" );
			bEmitter1 = bEmitter1 || W.Contains( "emitter #1" );
		}
		C.Check( bNewer && bEmitter1, "a newer version and the emitter number are named in the warnings" );
		FParticleEffectDefinition NotObject;
		C.Check( !FParticleEffectDefinition::FromJson( ParseJsonText( "[1,2]" ), NotObject ) && !FParticleEffectDefinition::FromJson( ParseJsonText( "5" ), NotObject ), "an effect that is not an object does not read" );
		Warnings.Reset();
		FParticleEffectDefinition NoArray;
		C.Check( FParticleEffectDefinition::FromJson( ParseJsonText( "{\"Emitters\":5}" ), NoArray, &Warnings ) && NoArray.Emitters.IsEmpty() && Warnings.Num() == 1, "emitters that are not an array: no emitters, and a warning" );

		// 爆発（Burst）は、時刻の順に並ぶ
		FParticleEffectDefinition Sorted;
		Warnings.Reset();
		C.Check( FParticleEffectDefinition::FromJson( ParseJsonText( "{\"Emitters\":[{\"Bursts\":[{\"Time\":2,\"Count\":3},{\"Time\":0.5,\"Count\":9},{\"Time\":1,\"Count\":1},5]}]}" ), Sorted, &Warnings ) && Sorted.Emitters.Num() == 1 && Sorted.Emitters[0].Bursts.Num() == 3
					 && Sorted.Emitters[0].Bursts[0].Count == 9 && Sorted.Emitters[0].Bursts[1].Count == 1 && Sorted.Emitters[0].Bursts[2].Count == 3 && Warnings.Num() == 1,
				 "bursts are sorted by time when read (an entry that is not an object is skipped, with a warning)" );
		FParticleEmitterDefinition Unsorted;
		Unsorted.Bursts.Add( Burst( 3.0f, 1 ) );
		Unsorted.Bursts.Add( Burst( 1.0f, 2 ) );
		Unsorted.SortBursts();
		C.Check( Unsorted.Bursts[0].Time == 1.0f && Unsorted.Bursts[1].Time == 3.0f, "SortBursts sorts" );

		// 名前
		EParticleShape Shape = EParticleShape::Cone;
		EParticleBlend Blend = EParticleBlend::Add;
		EParticleSpace Space = EParticleSpace::World;
		EParticleOrient Orient = EParticleOrient::Camera;
		C.Check( TryParse( "BOX", Shape ) && Shape == EParticleShape::Box && TryParse( "alpha", Blend ) && Blend == EParticleBlend::Alpha && TryParse( "local", Space ) && Space == EParticleSpace::Local && TryParse( "FIXED", Orient ) && Orient == EParticleOrient::Fixed
					 && !TryParse( "Cube", Shape ) && Shape == EParticleShape::Box && !TryParse( "", Blend ) && Blend == EParticleBlend::Alpha && !TryParse( "x", Space ) && Space == EParticleSpace::Local && !TryParse( "x", Orient ) && Orient == EParticleOrient::Fixed,
				 "names parse ignoring case; an unknown name leaves the value alone" );
		bool bRoundTrip = true;
		for ( const EParticleShape Each : { EParticleShape::Cone, EParticleShape::Sphere, EParticleShape::Box } )
		{
			EParticleShape Parsed = EParticleShape::Cone;
			bRoundTrip = bRoundTrip && TryParse( ToString( Each ), Parsed ) && Parsed == Each;
		}
		for ( const EParticleOrient Each : { EParticleOrient::Camera, EParticleOrient::CameraY, EParticleOrient::Fixed } )
		{
			EParticleOrient Parsed = EParticleOrient::Camera;
			bRoundTrip = bRoundTrip && TryParse( ToString( Each ), Parsed ) && Parsed == Each;
		}
		C.Check( bRoundTrip && FString( ToString( EParticleBlend::Add ) ) == "Add" && FString( ToString( EParticleBlend::Alpha ) ) == "Alpha" && FString( ToString( EParticleSpace::Local ) ) == "Local" && FString( ToString( EParticleSpace::World ) ) == "World",
				 "every name parses back to itself" );
	}

	//=========================================================================
	// 内容の識別（どの項目を変えても変わる）
	//=========================================================================
	void TestHash( FChecker& C )
	{
		C.Group = "effect-hash";
		FParticleEffectDefinition Base;
		Base.Name = "X";
		Base.Emitters.Add( FullEmitter() );
		Base.Emitters.Add( FParticleEmitterDefinition() );
		const uint64 BaseHash = Base.ComputeHash();
		FParticleEffectDefinition Copy = Base;
		C.Check( Copy.ComputeHash() == BaseHash, "a copy has the same hash" );

		struct FCase
		{
			const char* Name;
			std::function<void( FParticleEffectDefinition& )> Edit;
		};
		const FCase Cases[] = {
			{ "duration", []( FParticleEffectDefinition& D ) { D.Duration += 1.0f; } },
			{ "loop", []( FParticleEffectDefinition& D ) { D.bLoop = !D.bLoop; } },
			{ "seed", []( FParticleEffectDefinition& D ) { D.Seed += 1; } },
			{ "emitter count", []( FParticleEffectDefinition& D ) { D.Emitters.Add( FParticleEmitterDefinition() ); } },
			{ "emitter name", []( FParticleEffectDefinition& D ) { D.Emitters[0].Name = "Other"; } },
			{ "texture", []( FParticleEffectDefinition& D ) { D.Emitters[0].Texture = "Other.png"; } },
			{ "blend", []( FParticleEffectDefinition& D ) { D.Emitters[0].Blend = EParticleBlend::Add; } },
			{ "space", []( FParticleEffectDefinition& D ) { D.Emitters[0].Space = EParticleSpace::World; } },
			{ "orient", []( FParticleEffectDefinition& D ) { D.Emitters[0].Orient = EParticleOrient::Camera; } },
			{ "max particles", []( FParticleEffectDefinition& D ) { D.Emitters[0].MaxParticles += 1; } },
			{ "delay", []( FParticleEffectDefinition& D ) { D.Emitters[0].Delay += 0.5f; } },
			{ "rate", []( FParticleEffectDefinition& D ) { D.Emitters[0].Rate += 1.0f; } },
			{ "number of bursts", []( FParticleEffectDefinition& D ) { D.Emitters[0].Bursts.Add( Burst( 9.0f, 1 ) ); } },
			{ "burst time", []( FParticleEffectDefinition& D ) { D.Emitters[0].Bursts[0].Time += 0.25f; } },
			{ "burst amount", []( FParticleEffectDefinition& D ) { D.Emitters[0].Bursts[0].Count += 1; } },
			{ "lifetime min", []( FParticleEffectDefinition& D ) { D.Emitters[0].Lifetime.Min += 0.125f; } },
			{ "lifetime max", []( FParticleEffectDefinition& D ) { D.Emitters[0].Lifetime.Max += 0.125f; } },
			{ "speed min", []( FParticleEffectDefinition& D ) { D.Emitters[0].Speed.Min += 0.5f; } },
			{ "speed max", []( FParticleEffectDefinition& D ) { D.Emitters[0].Speed.Max += 0.5f; } },
			{ "shape", []( FParticleEffectDefinition& D ) { D.Emitters[0].Shape = EParticleShape::Sphere; } },
			{ "radius", []( FParticleEffectDefinition& D ) { D.Emitters[0].ShapeRadius += 0.5f; } },
			{ "cone angle", []( FParticleEffectDefinition& D ) { D.Emitters[0].ConeAngle += 5.0f; } },
			{ "box size", []( FParticleEffectDefinition& D ) { D.Emitters[0].BoxSize.z += 1.0f; } },
			{ "direction", []( FParticleEffectDefinition& D ) { D.Emitters[0].Direction.x += 1.0f; } },
			{ "gravity", []( FParticleEffectDefinition& D ) { D.Emitters[0].Gravity.y += 1.0f; } },
			{ "drag", []( FParticleEffectDefinition& D ) { D.Emitters[0].Drag += 0.5f; } },
			{ "start size min", []( FParticleEffectDefinition& D ) { D.Emitters[0].StartSize.Min += 0.125f; } },
			{ "start size max", []( FParticleEffectDefinition& D ) { D.Emitters[0].StartSize.Max += 0.125f; } },
			{ "end size min", []( FParticleEffectDefinition& D ) { D.Emitters[0].EndSize.Min += 0.125f; } },
			{ "end size max", []( FParticleEffectDefinition& D ) { D.Emitters[0].EndSize.Max += 0.125f; } },
			{ "start colour r", []( FParticleEffectDefinition& D ) { D.Emitters[0].StartColor.r ^= 1; } },
			{ "start colour g", []( FParticleEffectDefinition& D ) { D.Emitters[0].StartColor.g ^= 1; } },
			{ "start colour b", []( FParticleEffectDefinition& D ) { D.Emitters[0].StartColor.b ^= 1; } },
			{ "start colour a", []( FParticleEffectDefinition& D ) { D.Emitters[0].StartColor.a ^= 1; } },
			{ "end colour r", []( FParticleEffectDefinition& D ) { D.Emitters[0].EndColor.r ^= 1; } },
			{ "end colour g", []( FParticleEffectDefinition& D ) { D.Emitters[0].EndColor.g ^= 1; } },
			{ "end colour b", []( FParticleEffectDefinition& D ) { D.Emitters[0].EndColor.b ^= 1; } },
			{ "end colour a", []( FParticleEffectDefinition& D ) { D.Emitters[0].EndColor.a ^= 1; } },
			{ "start angle min", []( FParticleEffectDefinition& D ) { D.Emitters[0].StartAngle.Min += 1.0f; } },
			{ "start angle max", []( FParticleEffectDefinition& D ) { D.Emitters[0].StartAngle.Max += 1.0f; } },
			{ "spin min", []( FParticleEffectDefinition& D ) { D.Emitters[0].Spin.Min += 1.0f; } },
			{ "spin max", []( FParticleEffectDefinition& D ) { D.Emitters[0].Spin.Max += 1.0f; } },
			{ "second emitter", []( FParticleEffectDefinition& D ) { D.Emitters[1].Rate += 1.0f; } },
		};
		FString Unchanged;
		for ( const FCase& Case : Cases )
		{
			FParticleEffectDefinition Edited = Base;
			Case.Edit( Edited );
			if ( Edited.ComputeHash() == BaseHash ) Unchanged += FString( " " ) + Case.Name + ";";
		}
		C.Check( Unchanged.IsEmpty(), "changing any one item changes the hash (%d items tried; the hash did not change for:%s)", static_cast<int32>( sizeof( Cases ) / sizeof( Cases[0] ) ), *Unchanged );

		// 名前の境目: 文字をつなげた結果が同じでも、区切りが違えば別
		FParticleEffectDefinition A, B;
		FParticleEmitterDefinition EA, EB;
		EA.Name = "ab";
		EA.Texture = "c";
		EB.Name = "a";
		EB.Texture = "bc";
		A.Emitters.Add( EA );
		B.Emitters.Add( EB );
		C.Check( A.ComputeHash() != B.ComputeHash(), "the name and the image are told apart (\"ab\" + \"c\" is not \"a\" + \"bc\")" );
		FParticleEffectDefinition Renamed = Base;
		Renamed.Name = "Y";
		C.Check( Renamed.ComputeHash() == BaseHash, "the effect's own name is not part of the hash (it does not change how the effect looks)" );
	}

	//=========================================================================
	// 検証
	//=========================================================================
	void TestValidate( FChecker& C )
	{
		C.Group = "effect-validate";
		FParticleEffectDefinition Good;
		Good.Emitters.Add( FParticleEmitterDefinition() );
		Good.Emitters.Add( FullEmitter() );
		TArray<FParticleIssue> Issues = Good.Validate();
		C.Check( Issues.IsEmpty() && !FParticleEffectDefinition::HasErrors( Issues ), "a default emitter and a fully set one have no problems (%d)", Issues.Num() );

		FParticleEffectDefinition Empty;
		Issues = Empty.Validate();
		C.Check( Issues.Num() == 1 && !Issues[0].bError && Issues[0].Emitter == -1 && !FParticleEffectDefinition::HasErrors( Issues ), "an effect with no emitters is a warning, not an error" );

		FParticleEffectDefinition Many;
		for ( int32 i = 0; i < FParticleEffectDefinition::MaxEmitters; ++i ) Many.Emitters.Add( FParticleEmitterDefinition() );
		C.Check( Many.Validate().IsEmpty(), "%d emitters are allowed", FParticleEffectDefinition::MaxEmitters );
		Many.Emitters.Add( FParticleEmitterDefinition() );
		Issues = Many.Validate();
		C.Check( CountIssues( Issues, true, -1 ) == 1 && HasIssueText( Issues, "limit" ) && FParticleEffectDefinition::HasErrors( Issues ), "one more is an error" );

		FParticleEffectDefinition Endless;
		Endless.Emitters.Add( FParticleEmitterDefinition() );
		Endless.Duration = -1.0f;
		C.Check( CountIssues( Endless.Validate(), true, -1 ) == 1, "a negative duration is an error" );
		Endless.Duration = std::numeric_limits<float32>::quiet_NaN();
		C.Check( CountIssues( Endless.Validate(), true, -1 ) == 1, "a duration that is not a number is an error" );
		Endless.Duration = 0.0f;
		C.Check( Endless.Validate().IsEmpty(), "a duration of 0 (endless) is fine" );

		// 1 つずつ壊す: 全部が、そのエミッター（番号 1）の誤りとして、ちょうど 1 つ出る
		struct FCase
		{
			const char* Name;
			std::function<void( FParticleEmitterDefinition& )> Edit;
		};
		const float32 NaN = std::numeric_limits<float32>::quiet_NaN();
		const float32 Inf = std::numeric_limits<float32>::infinity();
		const FCase Errors[] = {
			{ "max particles 0", []( FParticleEmitterDefinition& E ) { E.MaxParticles = 0; } },
			{ "max particles too many", []( FParticleEmitterDefinition& E ) { E.MaxParticles = FParticleEffectDefinition::MaxParticlesPerEmitter + 1; } },
			{ "negative delay", []( FParticleEmitterDefinition& E ) { E.Delay = -0.5f; } },
			{ "NaN delay", [NaN]( FParticleEmitterDefinition& E ) { E.Delay = NaN; } },
			{ "negative rate", []( FParticleEmitterDefinition& E ) { E.Rate = -1.0f; } },
			{ "infinite rate", [Inf]( FParticleEmitterDefinition& E ) { E.Rate = Inf; } },
			{ "negative burst time", []( FParticleEmitterDefinition& E ) { E.Bursts.Add( Burst( -1.0f, 1 ) ); } },
			{ "negative burst count", []( FParticleEmitterDefinition& E ) { E.Bursts.Add( Burst( 1.0f, -1 ) ); } },
			{ "lifetime zero", []( FParticleEmitterDefinition& E ) { E.Lifetime = { 0.0f, 1.0f }; } },
			{ "lifetime negative", []( FParticleEmitterDefinition& E ) { E.Lifetime = { -1.0f, -0.5f }; } },
			{ "lifetime NaN", [NaN]( FParticleEmitterDefinition& E ) { E.Lifetime = { NaN, 1.0f }; } },
			{ "negative speed", []( FParticleEmitterDefinition& E ) { E.Speed = { -1.0f, 1.0f }; } },
			{ "infinite speed", [Inf]( FParticleEmitterDefinition& E ) { E.Speed = { 1.0f, Inf }; } },
			{ "negative start size", []( FParticleEmitterDefinition& E ) { E.StartSize = { -0.1f, 0.5f }; } },
			{ "negative end size", []( FParticleEmitterDefinition& E ) { E.EndSize = { 0.0f, -0.5f }; } },
			{ "NaN start angle", [NaN]( FParticleEmitterDefinition& E ) { E.StartAngle = { NaN, NaN }; } },
			{ "NaN spin", [NaN]( FParticleEmitterDefinition& E ) { E.Spin = { 0.0f, NaN }; } },
			{ "negative radius", []( FParticleEmitterDefinition& E ) { E.ShapeRadius = -1.0f; } },
			{ "cone angle over 180", []( FParticleEmitterDefinition& E ) { E.ConeAngle = 181.0f; } },
			{ "negative cone angle", []( FParticleEmitterDefinition& E ) { E.ConeAngle = -1.0f; } },
			{ "negative box x", []( FParticleEmitterDefinition& E ) { E.BoxSize = FVector3( -1.0f, 1.0f, 1.0f ); } },
			{ "negative box y", []( FParticleEmitterDefinition& E ) { E.BoxSize = FVector3( 1.0f, -1.0f, 1.0f ); } },
			{ "negative box z", []( FParticleEmitterDefinition& E ) { E.BoxSize = FVector3( 1.0f, 1.0f, -1.0f ); } },
			{ "zero direction", []( FParticleEmitterDefinition& E ) { E.Direction = FVector3::Zero; } },
			{ "NaN direction", [NaN]( FParticleEmitterDefinition& E ) { E.Direction = FVector3( NaN, 0.0f, 1.0f ); } },
			{ "infinite gravity", [Inf]( FParticleEmitterDefinition& E ) { E.Gravity = FVector3( 0.0f, -Inf, 0.0f ); } },
			{ "negative drag", []( FParticleEmitterDefinition& E ) { E.Drag = -0.5f; } },
		};
		FString NotOne;
		for ( const FCase& Case : Errors )
		{
			FParticleEffectDefinition D;
			D.Emitters.Add( FParticleEmitterDefinition() );
			D.Emitters.Add( FParticleEmitterDefinition() );
			Case.Edit( D.Emitters[1] );
			const TArray<FParticleIssue> Found = D.Validate();
			if ( CountIssues( Found, true, 1 ) != 1 || CountIssues( Found, true, 0 ) != 0 || !FParticleEffectDefinition::HasErrors( Found ) ) NotOne += FString( " " ) + Case.Name + ";";
		}
		C.Check( NotOne.IsEmpty(), "each of %d broken values is reported as exactly one error on its own emitter (not on the other one); the exceptions:%s", static_cast<int32>( sizeof( Errors ) / sizeof( Errors[0] ) ), *NotOne );

		// 境界は誤りではない
		FParticleEffectDefinition Edge;
		FParticleEmitterDefinition EdgeEmitter;
		EdgeEmitter.MaxParticles = FParticleEffectDefinition::MaxParticlesPerEmitter;
		EdgeEmitter.ConeAngle = 180.0f;
		EdgeEmitter.Speed = { 0.0f, 0.0f };
		EdgeEmitter.StartSize = { 0.0f, 0.0f };
		EdgeEmitter.Delay = 0.0f;
		EdgeEmitter.Drag = 0.0f;
		EdgeEmitter.BoxSize = FVector3::Zero;
		EdgeEmitter.Bursts.Add( Burst( 0.0f, 0 ) );
		Edge.Emitters.Add( EdgeEmitter );
		C.Check( Edge.Validate().IsEmpty(), "the edges are allowed: max particles at the limit, cone angle 180, speed 0, size 0, delay 0, drag 0, box 0, a burst of 0 at time 0 (%d)", Edge.Validate().Num() );
		EdgeEmitter.MaxParticles = 1;
		EdgeEmitter.Lifetime = { 0.001f, 0.001f };
		Edge.Emitters[0] = EdgeEmitter;
		C.Check( CountIssues( Edge.Validate(), true ) == 0, "a tiny positive lifetime and a single particle are allowed" );

		// 警告
		FParticleEffectDefinition Warn;
		FParticleEmitterDefinition Swapped;
		Swapped.Speed = { 3.0f, 1.0f };
		Swapped.StartSize = { 0.5f, 0.2f };
		Warn.Emitters.Add( Swapped );
		Issues = Warn.Validate();
		C.Check( CountIssues( Issues, false, 0 ) == 2 && CountIssues( Issues, true ) == 0 && HasIssueText( Issues, "swapped" ), "a range whose minimum is larger than its maximum is a warning (it is used swapped), not an error (%d warnings)", CountIssues( Issues, false, 0 ) );

		FParticleEffectDefinition Crowd;
		FParticleEmitterDefinition CrowdEmitter;
		CrowdEmitter.Rate = 100.0f;
		CrowdEmitter.Lifetime = { 1.0f, 10.0f };
		CrowdEmitter.MaxParticles = 200;
		Crowd.Emitters.Add( CrowdEmitter );
		Issues = Crowd.Validate();
		C.Check( CountIssues( Issues, false, 0 ) == 1 && HasIssueText( Issues, "200" ), "a rate and lifetime that would not fit in the maximum is a warning" );
		Crowd.Emitters[0].Lifetime = { 1.0f, 2.0f };
		C.Check( Crowd.Validate().IsEmpty(), "and exactly fitting is not (100 per second x 2 s = 200)" );

		FParticleEffectDefinition Silent;
		FParticleEmitterDefinition SilentEmitter;
		SilentEmitter.Rate = 0.0f;
		Silent.Emitters.Add( SilentEmitter );
		Issues = Silent.Validate();
		C.Check( CountIssues( Issues, false, 0 ) == 1 && HasIssueText( Issues, "nothing is emitted" ), "no rate and no bursts is a warning" );
		Silent.Emitters[0].Bursts.Add( Burst( 0.0f, 5 ) );
		C.Check( Silent.Validate().IsEmpty(), "a burst is enough to emit" );
	}

	//=========================================================================
	// ワールドの Effect 部品（JSON・検証・編集）
	//=========================================================================
	int32 CountWorldIssues( const TArray<FWorldIssue>& Issues, EWorldIssueCode Code, EWorldIssueSeverity Severity, const char* Contains = nullptr )
	{
		int32 Count = 0;
		for ( const FWorldIssue& Issue : Issues )
		{
			if ( Issue.Code == Code && Issue.Severity == Severity && ( Contains == nullptr || Issue.Message.Contains( Contains ) ) ) ++Count;
		}
		return Count;
	}

	FWorldObject MakeFxObject( int32 Id, const char* Name, const char* Path )
	{
		FWorldObject Object;
		Object.Id = Id;
		Object.Name = Name;
		FWorldEffect Effect;
		Effect.Path = Path;
		Object.Effect = Effect;
		return Object;
	}

	FWorldDefinition MakeFxWorld( std::initializer_list<FWorldObject> Objects )
	{
		FWorldDefinition World;
		World.Name = "Fx";
		for ( const FWorldObject& Object : Objects )
		{
			World.Objects.Add( Object );
			World.NextId = std::max( World.NextId, Object.Id + 1 );
		}
		return World;
	}

	void TestWorldComponent( FChecker& C )
	{
		C.Group = "effect-world-data";
		FWorldObject Plain = MakeFxObject( 1, "Plain", "Effects/Fire.effect.json" );
		FWorldObject Full = MakeFxObject( 2, "Full", "Effects/Boom.effect.json" );
		Full.Effect.GetValue().bAutoPlay = false;
		Full.Effect.GetValue().Scale = 2.0f;
		Full.Effect.GetValue().Speed = 0.5f;
		Full.Effect.GetValue().Seed = 7;
		Full.Effect.GetValue().Loop = 0;
		FWorldObject Looping = MakeFxObject( 3, "Looping", "Effects/Fire.effect.json" );
		Looping.Effect.GetValue().Loop = 1;
		const FWorldDefinition World = MakeFxWorld( { Plain, Full, Looping } );
		const NlohmannJson Json = World.ToJson();
		const FJson J( Json );
		C.Check( J["Objects"][0]["Components"]["Effect"]["Path"] == "Effects/Fire.effect.json" && J["Objects"][0]["Components"]["Effect"].size() == 1, "an Effect component is written under Components, with just its path when the rest is default (%s)", Json.dump().c_str() );
		C.Check( J["Objects"][1]["Components"]["Effect"]["AutoPlay"] == false && J["Objects"][1]["Components"]["Effect"]["Scale"] == 2.0 && J["Objects"][1]["Components"]["Effect"]["Speed"] == 0.5 && J["Objects"][1]["Components"]["Effect"]["Seed"] == 7 && J["Objects"][1]["Components"]["Effect"]["Loop"] == false,
				 "and the other items when they are not default (a loop of 0 is written as false)" );
		C.Check( J["Objects"][2]["Components"]["Effect"]["Loop"] == true && !J["Objects"][0]["Components"]["Effect"].contains( "Loop" ), "a loop of 1 is written as true, and an unset loop is left out" );

		FWorldDefinition Back;
		TArray<FString> Warnings;
		C.Check( FWorldDefinition::FromJson( ParseJsonText( Json.dump().c_str() ), Back, &Warnings ) && Warnings.IsEmpty() && Back.Objects.Num() == 3 && Back.ToJson() == Json, "it reads back, and writes the same, with no warnings (%d)", Warnings.Num() );
		const FWorldEffect* B0 = Back.Objects.Num() > 0 && Back.Objects[0].Effect.IsSet() ? &Back.Objects[0].Effect.GetValue() : nullptr;
		const FWorldEffect* B1 = Back.Objects.Num() > 1 && Back.Objects[1].Effect.IsSet() ? &Back.Objects[1].Effect.GetValue() : nullptr;
		const FWorldEffect* B2 = Back.Objects.Num() > 2 && Back.Objects[2].Effect.IsSet() ? &Back.Objects[2].Effect.GetValue() : nullptr;
		C.Check( B0 != nullptr && B0->Path == "Effects/Fire.effect.json" && B0->bAutoPlay && B0->Scale == 1.0f && B0->Speed == 1.0f && B0->Seed == 0 && B0->Loop == -1, "a plain Effect comes back with its defaults (auto play, scale 1, speed 1, seed 0, loop as the effect says)" );
		C.Check( B1 != nullptr && !B1->bAutoPlay && B1->Scale == 2.0f && B1->Speed == 0.5f && B1->Seed == 7 && B1->Loop == 0 && B2 != nullptr && B2->Loop == 1, "and a full one comes back as it was" );
		C.Check( Back.Objects.Num() == 3 && Back.Objects[0].RawComponents.IsEmpty() && Back.Objects[0].GetComponentNames().Num() == 1 && Back.Objects[0].GetComponentNames()[0] == "Effect", "it is a known component now (not kept as an unknown one)" );

		// 読めない形
		FWorldDefinition Odd;
		Warnings.Reset();
		C.Check( FWorldDefinition::FromJson( ParseJsonText( "{\"Objects\":[{\"Id\":1,\"Components\":{\"Effect\":5}},{\"Id\":2,\"Components\":{\"Effect\":{\"Path\":\"a.effect.json\",\"Loop\":\"yes\",\"Scale\":\"big\"}}}]}" ), Odd, &Warnings ) && Odd.Objects.Num() == 2,
				 "a world with odd Effect components reads" );
		C.Check( Odd.Objects.Num() == 2 && !Odd.Objects[0].Effect.IsSet() && Odd.Objects[0].RawComponents.Num() == 1 && Warnings.Num() >= 1, "an Effect that is not an object is kept as it is, with a warning" );
		C.Check( Odd.Objects.Num() == 2 && Odd.Objects[1].Effect.IsSet() && Odd.Objects[1].Effect.GetValue().Loop == -1 && Odd.Objects[1].Effect.GetValue().Scale == 1.0f && Odd.Objects[1].Effect.GetValue().Path == "a.effect.json",
				 "a loop that is not true / false is left to the effect, and a scale that is not a number is 1" );

		// 部品の名前の並び
		FWorldObject Many = MakeFxObject( 1, "Many", "a.effect.json" );
		Many.Model = FWorldModel();
		Many.Sound = FWorldSound();
		Many.Collider = FWorldCollider();
		const TArray<FString> Names = Many.GetComponentNames();
		C.Check( Names.Num() == 4 && Names[0] == "Model" && Names[1] == "Sound" && Names[2] == "Effect" && Names[3] == "Collider", "the component names put Effect after Sound and before Collider" );

		//---------------------------------------------------------------------
		// 検証
		//---------------------------------------------------------------------
		C.Group = "effect-world-validate";
		FFakeQuery Query;
		Query.Existing.insert( "fx/a.effect.json" );
		Query.Existing.insert( "fx/a.efk" );
		const auto Validate = [&]( const FWorldObject& Object ) { return NWorld::ValidateWorld( MakeFxWorld( { Object } ), &Query ); };
		C.Check( Validate( MakeFxObject( 1, "Good", "fx/a.effect.json" ) ).IsEmpty(), "a good Effect has no problems" );
		TArray<FWorldIssue> Issues = Validate( MakeFxObject( 1, "NoFile", "" ) );
		C.Check( CountWorldIssues( Issues, EWorldIssueCode::EmptyAssetPath, EWorldIssueSeverity::Error ) == 1 && Issues.Num() == 1, "an Effect without a file is an error" );
		Issues = Validate( MakeFxObject( 1, "Missing", "fx/none.effect.json" ) );
		C.Check( CountWorldIssues( Issues, EWorldIssueCode::MissingAsset, EWorldIssueSeverity::Error ) == 1 && Issues.Num() == 1, "a file that does not exist is an error" );
		Issues = Validate( MakeFxObject( 1, "Foreign", "fx/a.efk" ) );
		C.Check( CountWorldIssues( Issues, EWorldIssueCode::BadEffect, EWorldIssueSeverity::Warning, "backend" ) == 1 && Issues.Num() == 1, "a kind of file no backend handles is a warning that says so (the file exists)" );
		Issues = Validate( MakeFxObject( 1, "Foreign", "fx/none.efk" ) );
		C.Check( CountWorldIssues( Issues, EWorldIssueCode::MissingAsset, EWorldIssueSeverity::Error ) == 1 && CountWorldIssues( Issues, EWorldIssueCode::BadEffect, EWorldIssueSeverity::Warning ) == 1, "and a missing foreign file gets both" );

		const float32 NaN = std::numeric_limits<float32>::quiet_NaN();
		struct FCase
		{
			const char* Name;
			std::function<void( FWorldEffect& )> Edit;
			EWorldIssueCode Code;
		};
		const FCase Bad[] = {
			{ "scale 0", []( FWorldEffect& E ) { E.Scale = 0.0f; }, EWorldIssueCode::BadEffect },
			{ "negative scale", []( FWorldEffect& E ) { E.Scale = -1.0f; }, EWorldIssueCode::BadEffect },
			{ "negative speed", []( FWorldEffect& E ) { E.Speed = -0.5f; }, EWorldIssueCode::BadEffect },
			{ "negative seed", []( FWorldEffect& E ) { E.Seed = -3; }, EWorldIssueCode::BadEffect },
			{ "NaN scale", [NaN]( FWorldEffect& E ) { E.Scale = NaN; }, EWorldIssueCode::NonFiniteValue },
			{ "NaN speed", [NaN]( FWorldEffect& E ) { E.Speed = NaN; }, EWorldIssueCode::NonFiniteValue },
		};
		FString NotOne;
		for ( const FCase& Case : Bad )
		{
			FWorldObject Object = MakeFxObject( 1, "Bad", "fx/a.effect.json" );
			Case.Edit( Object.Effect.GetValue() );
			const TArray<FWorldIssue> Found = Validate( Object );
			if ( Found.Num() != 1 || Found[0].Code != Case.Code || Found[0].Severity != EWorldIssueSeverity::Error ) NotOne += FString( " " ) + Case.Name + ";";
		}
		C.Check( NotOne.IsEmpty(), "each of %d bad values is exactly one error of the right kind; the exceptions:%s", static_cast<int32>( sizeof( Bad ) / sizeof( Bad[0] ) ), *NotOne );
		FWorldObject Frozen = MakeFxObject( 1, "Frozen", "fx/a.effect.json" );
		Frozen.Effect.GetValue().Speed = 0.0f;
		Frozen.Effect.GetValue().Seed = 0;
		C.Check( Validate( Frozen ).IsEmpty(), "a speed of 0 (frozen) and a seed of 0 are fine" );
		C.Check( NWorld::ValidateWorld( MakeFxWorld( { MakeFxObject( 1, "Anything", "whatever.effect.json" ) } ), nullptr ).IsEmpty(), "without an asset query, the file is not checked" );

		//---------------------------------------------------------------------
		// 編集
		//---------------------------------------------------------------------
		C.Group = "effect-world-edit";
		FWorldObject Group = MakeFxObject( 1, "Group", "fx/a.effect.json" );
		FWorldObject Child;
		Child.Id = 2;
		Child.Name = "Child";
		Child.Parent = 1;
		TWorldDocument Document;
		Document.Reset( MakeFxWorld( { Group, Child } ) );
		C.Check( !NWorldEdit::UngroupObject( Document, 1 ) && Document.Get().Objects.Num() == 2, "an object with an Effect is not a group: it cannot be ungrouped (its effect would be lost)" );
		FWorldObject PlainGroup = Group;
		PlainGroup.Effect.Reset();
		Document.Reset( MakeFxWorld( { PlainGroup, Child } ) );
		C.Check( NWorldEdit::UngroupObject( Document, 1 ) && Document.Get().FindObject( 1 ) == nullptr, "without the Effect, the same object is an ordinary group" );
	}

	//=========================================================================
	// エディタ（アセットの種類・置く・目印・絞り込み）
	//=========================================================================
	void TestEditor( FChecker& C )
	{
		C.Group = "effect-editor";
		C.Check( NWorldEditor::ClassifyAsset( "Effects/Fire.effect.json" ) == EEditorAssetKind::Effect && NWorldEditor::ClassifyAsset( "Effects\\Fire.EFFECT.JSON" ) == EEditorAssetKind::Effect && NWorldEditor::ClassifyAsset( "a.effect.json" ) == EEditorAssetKind::Effect,
				 "an .effect.json file is an effect (any case, any folder)" );
		C.Check( NWorldEditor::ClassifyAsset( "a.json" ) == EEditorAssetKind::Unknown && NWorldEditor::ClassifyAsset( "a.efk" ) == EEditorAssetKind::Unknown && NWorldEditor::ClassifyAsset( "dir.effect.json/file" ) == EEditorAssetKind::Unknown && NWorldEditor::ClassifyAsset( "a.modelanim.json" ) == EEditorAssetKind::Unknown,
				 "other .json files, a foreign effect file nothing handles, and a dot only in a folder name are not" );
		C.Check( NWorldEditor::GetAssetDisplayName( "Effects/Fire.effect.json" ) == "Fire" && NWorldEditor::GetAssetDisplayName( "a.EFFECT.JSON" ) == "a" && NWorldEditor::GetAssetDisplayName( "x.json" ) == "x", "the display name of an effect drops both extensions" );

		TWorldDocument Document;
		NWorldEditorTest::FFakeEditorAssets Assets;
		const int32 Placed = NWorldEditor::AddAssetObject( Document, "Effects\\Fire.effect.json", WorldNoId, FVector3( 1.0f, 2.0f, 3.0f ), &Assets );
		const FWorldObject* Object = Document.Get().FindObject( Placed );
		C.Check( Placed != WorldNoId && Object != nullptr && Object->Name == "Fire" && Object->Effect.IsSet() && Object->Effect.GetValue().Path == "Effects/Fire.effect.json" && Near3( Object->Transform.Position, FVector3( 1.0f, 2.0f, 3.0f ) ) && !Object->Model.IsSet() && !Object->Sound.IsSet(),
				 "placing an effect makes an object named after the file, with just an Effect component, at the position" );
		C.Check( Object != nullptr && Object->Effect.GetValue().bAutoPlay && Object->Effect.GetValue().Scale == 1.0f && Object->Effect.GetValue().Loop == -1 && Document.GetNumUndo() == 1, "with the default settings, in one undo step" );

		FWorldObject Plain;
		Plain.Id = 1;
		Plain.Effect = FWorldEffect();
		const TArray<FEditorShape> Shapes = NWorldEditor::GatherShapes( Plain, FTransform( FVector3( 4.0f, 5.0f, 6.0f ) ), FEditorShapeContext() );
		C.Check( Shapes.Num() == 1 && Shapes[0].Role == EEditorShapeRole::Effect && Near3( Shapes[0].Center, FVector3( 4.0f, 5.0f, 6.0f ) ), "an effect object is an effect marker at its position (so that it can be picked)" );
		FWorldObject Both = Plain;
		Both.Sound = FWorldSound();
		const TArray<FEditorShape> BothShapes = NWorldEditor::GatherShapes( Both, FTransform::Identity, FEditorShapeContext() );
		int32 NumEffect = 0, NumSound = 0;
		for ( const FEditorShape& Shape : BothShapes )
		{
			NumEffect += Shape.Role == EEditorShapeRole::Effect ? 1 : 0;
			NumSound += Shape.Role == EEditorShapeRole::Sound ? 1 : 0;
		}
		C.Check( NumEffect == 1 && NumSound == 1, "an object with a sound and an effect has both markers" );

		FWorldDefinition World;
		World.Objects.Add( MakeFxObject( 1, "Fire", "a.effect.json" ) );
		FWorldObject NoEffect;
		NoEffect.Id = 2;
		NoEffect.Name = "Plain";
		World.Objects.Add( NoEffect );
		World.NextId = 3;
		const TSet<int32> None;
		const TArray<FEditorOutlinerRow> Rows = NWorldEditor::BuildOutlinerRows( World, None, "comp:effect" );
		C.Check( Rows.Num() == 1 && Rows[0].ObjectId == 1, "the outliner finds objects by comp:Effect" );
	}
}

void NWorldTest::RunEffectDataTests( FChecker& C )
{
	TestData( C );
	TestHash( C );
	TestValidate( C );
	TestWorldComponent( C );
	TestEditor( C );
}
