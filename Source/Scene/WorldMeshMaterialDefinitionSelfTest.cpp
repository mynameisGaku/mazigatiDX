// SPDX-License-Identifier: NOASSERTION
#include "WorldMeshMaterialSelfTestSupport.h"

#include <cmath>
#include <limits>

#include "Library/Framework/World/Mesh/EditMesh.h"
#include "Library/Framework/World/Mesh/EditMeshOps.h"
#include "Library/Framework/World/WorldEdit.h"
#include "Library/Framework/World/WorldValidate.h"

namespace
{
	// 旧hash goldenに使う三角形の材質番号。
	FEditMesh MakeLegacyMeshFixture()
	{
		// legacy goldenとして固定する三角形mesh。
		FEditMesh Mesh;
		Mesh.Vertices.Add( FVector3( 0.0f, 0.0f, 0.0f ) );
		Mesh.Vertices.Add( FVector3( 1.0f, 0.0f, 0.0f ) );
		Mesh.Vertices.Add( FVector3( 0.0f, 1.0f, 0.0f ) );
		// golden三角形を構成する一枚の面。
		FEditMeshFace Face;
		Face.Indices.Add( 0 );
		Face.Indices.Add( 1 );
		Face.Indices.Add( 2 );
		Mesh.Faces.Add( Face );
		Mesh.Materials.Add( FEditMeshMaterial() );
		return Mesh;
	}

	// テスト専用の、決めた二つのtextureだけを持つ問い合わせ先。
	class FTestAssetQuery final : public IWorldAssetQuery
	{
	public:
		// mask.pngが存在するか。
		bool bHasMask = false;
		// noise.pngが存在するか。
		bool bHasNoise = false;

		bool Exists( const FString& Path ) const override
		{
			return ( Path == "mask.png" && bHasMask ) || ( Path == "noise.png" && bHasNoise );
		}
	};

	// 指定issue codeとseverityを持つ問題があるか調べる。
	bool HasWorldIssue( const TArray<FWorldIssue>& Issues, EWorldIssueCode Code, EWorldIssueSeverity Severity )
	{
		// 今回照合する一件のWorld issue。
		for ( const FWorldIssue& Issue : Issues ) if ( Issue.Code == Code && Issue.Severity == Severity ) return true;
		return false;
	}

	// 指定種類・severityのissue件数を数える。
	int32 CountWorldIssues( const TArray<FWorldIssue>& Issues, EWorldIssueCode Code, EWorldIssueSeverity Severity )
	{
		// 条件に一致したissue件数。
		int32 Count = 0;
		// severityとcodeが一致するissueだけ数える。
		for ( const FWorldIssue& Issue : Issues ) if ( Issue.Code == Code && Issue.Severity == Severity ) ++Count;
		return Count;
	}

	// 材質効果を有効にし、MaskとNoiseの参照を持たせる。
	FMeshMaterialEffectDefinition MakeReferencedEffect()
	{
		// World validationで画像参照を検査する材質効果。
		FMeshMaterialEffectDefinition Effect;
		Effect.Material.bEnabled = true;
		Effect.Material.bDissolve = true;
		Effect.Material.MaskTexture = "mask.png";
		Effect.Material.Distortion.bEnabled = true;
		Effect.Material.Distortion.Texture = "noise.png";
		Effect.Material.Distortion.Strength.Keys.Add( { 0.0f, 0.25f } );
		return Effect;
	}
}

void NWorldTest::RunMeshMaterialEffectDefinitionTests( NAssetTest::FChecker& C )
{
	C.Group = "mesh-material-effect-definition";

	// 旧材質hashのgolden fixture。座標・面index・全材質fieldは旧EditMesh.cppのFNV順序で算出した。
	// 旧JSONとbyte順序を比較する三角形。
	const FEditMesh LegacyMesh = MakeLegacyMeshFixture();
	// 旧形式のcompact JSON。
	const NlohmannJson LegacyJson = LegacyMesh.ToJson();
	C.Check( LegacyMesh.ComputeHash() == 0x82BA0A4346F0A711ull && LegacyJson.dump() == "{\"Faces\":[[0,1,2]],\"Materials\":[{}],\"Vertices\":[[0.0,0.0,0.0],[1.0,0.0,0.0],[0.0,1.0,0.0]]}", "既定Effectを持つmeshは旧JSONと旧hashを維持する" );

	// 既定効果を基準に、各外側fieldを独立して変える。
	// 既定値のEffect definition。
	const FMeshMaterialEffectDefinition DefaultEffect;
	// 外側field比較の基準hash。
	const uint64 DefaultHash = DefaultEffect.ComputeHash();
	// Durationだけ異なる定義。
	FMeshMaterialEffectDefinition ChangedDuration = DefaultEffect;
	ChangedDuration.Duration = 3.0f;
	// Loopだけ異なる定義。
	FMeshMaterialEffectDefinition ChangedLoop = DefaultEffect;
	ChangedLoop.bLoop = false;
	// Opacityだけ異なる定義。
	FMeshMaterialEffectDefinition ChangedOpacity = DefaultEffect;
	ChangedOpacity.Opacity = 0.5f;
	// Blendだけ異なる定義。
	FMeshMaterialEffectDefinition ChangedBlend = DefaultEffect;
	ChangedBlend.Blend = EDrawBlend::Add;
	// 内包材質だけ異なる定義。
	FMeshMaterialEffectDefinition ChangedMaterial = DefaultEffect;
	ChangedMaterial.Material.bEnabled = true;
	C.Check( DefaultEffect.IsDefault() && DefaultEffect.IsValidSettings() && ChangedDuration.ComputeHash() != DefaultHash && ChangedLoop.ComputeHash() != DefaultHash && ChangedOpacity.ComputeHash() != DefaultHash && ChangedBlend.ComputeHash() != DefaultHash && ChangedMaterial.ComputeHash() != DefaultHash, "Duration・Loop・Opacity・Blend・内包材質は個別に識別される" );

	// Duration、Opacity、Blendの各境界を一つずつ破る。
	// 境界を一つずつ外した設定群。
	TArray<FMeshMaterialEffectDefinition> InvalidSettings;
	// 0秒のDurationを持つinvalid fixture。
	FMeshMaterialEffectDefinition ZeroDuration;
	ZeroDuration.Duration = 0.0f;
	InvalidSettings.Add( ZeroDuration );
	// 上限を越えるDurationを持つinvalid fixture。
	FMeshMaterialEffectDefinition LargeDuration;
	LargeDuration.Duration = 3600.1f;
	InvalidSettings.Add( LargeDuration );
	// 0未満のOpacityを持つinvalid fixture。
	FMeshMaterialEffectDefinition NegativeOpacity;
	NegativeOpacity.Opacity = -0.01f;
	InvalidSettings.Add( NegativeOpacity );
	// 1を越えるOpacityを持つinvalid fixture。
	FMeshMaterialEffectDefinition LargeOpacity;
	LargeOpacity.Opacity = 1.01f;
	InvalidSettings.Add( LargeOpacity );
	// 許可していないBlendを持つinvalid fixture。
	FMeshMaterialEffectDefinition BadBlend;
	BadBlend.Blend = EDrawBlend::Multiply;
	InvalidSettings.Add( BadBlend );
	// 各invalid fixtureを検査するための集約結果。
	bool bEachInvalidRejected = true;
	// すべての境界ケースが単独で拒否されることを確認する。
	for ( const FMeshMaterialEffectDefinition& Invalid : InvalidSettings ) bEachInvalidRejected = bEachInvalidRejected && !Invalid.IsValidSettings() && !Invalid.Validate().IsEmpty();
	C.Check( bEachInvalidRejected, "Duration・Opacity・Blendは項目ごとに値域外を拒否する" );

	// 小数精度のまま曲線とwrapper値を往復する。
	// 複数種類の小数値を持つ有効定義。
	FMeshMaterialEffectDefinition Precise = DefaultEffect;
	Precise.Duration = 1.2345678f;
	Precise.Opacity = 0.1234567f;
	Precise.Material.bEnabled = true;
	Precise.Material.Emission.Keys.Add( { 0.1234567f, 2.345678f } );
	Precise.Material.Distortion.UVScroll = FVector2( -1.234567f, 2.345678f );
	// JSONから再構成する定義。
	FMeshMaterialEffectDefinition PreciseRoundTrip;
	// 小数値を失わず読み取れたか。
	const NlohmannJson PreciseDiskJson = NlohmannJson::parse( Precise.ToJson().dump() );
	// compact JSON再読でも元のfloat精度を保てたか。
	const bool bPreciseParsed = FMeshMaterialEffectDefinition::FromJson( PreciseDiskJson, PreciseRoundTrip );
	C.Check( bPreciseParsed && PreciseRoundTrip.ComputeHash() == Precise.ComputeHash() && PreciseRoundTrip.Duration == Precise.Duration && PreciseRoundTrip.Opacity == Precise.Opacity && PreciseRoundTrip.Material.Emission.Keys.IsValidIndex( 0 ) && PreciseRoundTrip.Material.Emission.Keys[0].Value == Precise.Material.Emission.Keys[0].Value, "Effectのfloat・曲線・歪みscrollは丸めず保存される" );

	// 各既知fieldの型違いと未知enumを読み書き往復させ、検証エラーを残す。
	// 各fieldを壊すための有効JSON。
	const NlohmannJson ValidJson = Precise.ToJson();
	// 個別に型を壊して確認するwrapper field名。
	const TArray<FString> InvalidFieldNames = { "Material", "Duration", "Loop", "Opacity", "Blend" };
	// 全fieldがroundtrip後も不正として検出されたか。
	bool bInvalidRoundTrips = true;
	// 一つのfieldだけをnullにした定義ごとに再読する。
	for ( const FString& Field : InvalidFieldNames )
	{
		// 現在のfieldだけnullへ差し替えた入力。
		NlohmannJson InvalidJson = ValidJson;
		InvalidJson[Field.ToUtf8().GetData()] = nullptr;
		// 最初の読み込み結果を保持する定義。
		FMeshMaterialEffectDefinition Invalid;
		// ToJson後に再読した定義。
		FMeshMaterialEffectDefinition InvalidAgain;
		// 型違いを読めたか。
		const bool bReadFirst = FMeshMaterialEffectDefinition::FromJson( InvalidJson, Invalid );
		// invalid markerをJSON往復できたか。
		const bool bReadAgain = bReadFirst && FMeshMaterialEffectDefinition::FromJson( Invalid.ToJson(), InvalidAgain );
		bInvalidRoundTrips = bInvalidRoundTrips && bReadAgain && !Invalid.IsValidSettings() && !Invalid.Validate().IsEmpty() && !InvalidAgain.IsValidSettings() && !InvalidAgain.Validate().IsEmpty();
	}
	// 未知Blend名を持つJSON。
	NlohmannJson UnknownBlendJson = ValidJson;
	UnknownBlendJson["Blend"] = "Multiply";
	// 未知名を読み取る定義。
	FMeshMaterialEffectDefinition UnknownBlend;
	// 未知名を再保存後に読み取る定義。
	FMeshMaterialEffectDefinition UnknownBlendAgain;
	// 未知名の型違いを保持できたか。
	const bool bUnknownBlendRead = FMeshMaterialEffectDefinition::FromJson( UnknownBlendJson, UnknownBlend ) && FMeshMaterialEffectDefinition::FromJson( UnknownBlend.ToJson(), UnknownBlendAgain );
	bInvalidRoundTrips = bInvalidRoundTrips && bUnknownBlendRead && !UnknownBlendAgain.IsValidSettings();
	// 内包MaterialのEnabled型違いを持つJSON。
	NlohmannJson InvalidNestedMaterialJson = ValidJson;
	InvalidNestedMaterialJson["Material"]["Enabled"] = nullptr;
	// 内包fieldの型違いを読む定義。
	FMeshMaterialEffectDefinition InvalidNestedMaterial;
	// 内包fieldの型違いを再読する定義。
	FMeshMaterialEffectDefinition InvalidNestedMaterialAgain;
	// 内包fieldの型違いをJSON往復できたか。
	const bool bInvalidNestedMaterialRead = FMeshMaterialEffectDefinition::FromJson( InvalidNestedMaterialJson, InvalidNestedMaterial ) && FMeshMaterialEffectDefinition::FromJson( InvalidNestedMaterial.ToJson(), InvalidNestedMaterialAgain );
	bInvalidRoundTrips = bInvalidRoundTrips && bInvalidNestedMaterialRead && !InvalidNestedMaterial.IsValidSettings() && !InvalidNestedMaterialAgain.IsValidSettings() && !InvalidNestedMaterialAgain.Validate().IsEmpty();
	// null rootを保持する定義。
	FMeshMaterialEffectDefinition InvalidRoot;
	// null rootを再読した定義。
	FMeshMaterialEffectDefinition InvalidRootAgain;
	// 不正rootを往復できたか。
	const bool bInvalidRootRead = FMeshMaterialEffectDefinition::FromJson( nullptr, InvalidRoot ) && FMeshMaterialEffectDefinition::FromJson( InvalidRoot.ToJson(), InvalidRootAgain );
	C.Check( bInvalidRoundTrips && bInvalidRootRead && !InvalidRootAgain.IsValidSettings() && !InvalidRootAgain.Validate().IsEmpty(), "null field・内包Material型違い・未知Blend・不正rootはJSON再読後も保存拒否される" );

	// 直接代入された非有限値も検証し、JSONへ出して再読後に無効状態を保つ。
	// NaN/Infを持つinvalid定義。
	FMeshMaterialEffectDefinition NonFinite = DefaultEffect;
	NonFinite.Duration = std::numeric_limits<float32>::infinity();
	NonFinite.Opacity = std::numeric_limits<float32>::quiet_NaN();
	// JSONから戻すinvalid定義。
	FMeshMaterialEffectDefinition NonFiniteRoundTrip;
	// 非有限値をnull経由で読み直せたか。
	const NlohmannJson NonFiniteDiskJson = NlohmannJson::parse( NonFinite.ToJson().dump() );
	// dumpでnull化されたNaN/Infを再読できたか。
	const bool bNonFiniteParsed = FMeshMaterialEffectDefinition::FromJson( NonFiniteDiskJson, NonFiniteRoundTrip );
	C.Check( !NonFinite.IsValidSettings() && bNonFiniteParsed && !NonFiniteRoundTrip.IsValidSettings(), "DurationとOpacityのInf/NaNは保存と再読後も不正である" );

	// disabledでも保存値の違いを保ち、完全既定時だけEffect項目を省略する。
	// wrapper値を変えたdisabled材質を含むmesh。
	FEditMesh DisabledMesh = LegacyMesh;
	DisabledMesh.Materials[0].Effect.Duration = 4.0f;
	// 保存JSONから読み直すmesh。
	FEditMesh DisabledRoundTrip;
	// 非既定Effectを読み込めたか。
	const bool bDisabledParsed = FEditMesh::FromJson( DisabledMesh.ToJson(), DisabledRoundTrip );
	C.Check( bDisabledParsed && DisabledMesh.ToJson()["Materials"][0].contains( "Effect" ) && DisabledRoundTrip.Materials[0].Effect.Duration == 4.0f && DisabledMesh.ComputeHash() != LegacyMesh.ComputeHash(), "無効材質の非既定値もMesh JSONとhashに保存される" );

	// 材質結合はhashでなくEffectの構造を比較する。
	// 同一effectを持つ材質の一方。
	FEditMeshMaterial SameA;
	// 同一effectを持つ材質のもう一方。
	FEditMeshMaterial SameB;
	SameA.Effect = ChangedOpacity;
	SameB.Effect = ChangedOpacity;
	// Opacityだけ異なる材質。
	FEditMeshMaterial Different;
	Different.Effect = ChangedOpacity;
	Different.Effect.Opacity = 0.75f;
	C.Check( NEditMesh::IsSameMaterial( SameA, SameB ) && !NEditMesh::IsSameMaterial( SameA, Different ), "材質統合は同じEffectを共有し、異なるOpacityを統合しない" );

	// DocumentのJSON履歴がEffectの変更を元へ戻し、再適用する。
	// historyを試す初期world。
	FWorldDefinition World;
	// meshを含めるworld object。
	FWorldObject Object;
	Object.Id = 1;
	Object.Name = "Mesh";
	// history fixtureとして使うmesh。
	FEditMesh HistoryMesh = LegacyMesh;
	Object.Mesh = HistoryMesh;
	World.Objects.Add( Object );
	// mesh effectを持つworld document。
	TWorldDocument Document;
	Document.Reset( World );
	// JSON snapshotの対象になる編集world。
	FWorldDefinition& EditedWorld = Document.BeginEdit();
	EditedWorld.Objects[0].Mesh.GetValue().Materials[0].Effect = ChangedOpacity;
	// 変更後に期待するmesh hash。
	const uint64 EditedHash = EditedWorld.Objects[0].Mesh.GetValue().ComputeHash();
	// 履歴が既定値へ戻ったか。
	const bool bUndo = Document.Undo();
	// Redo直前の復元結果。
	const bool bUndoRestored = bUndo && Document.Get().Objects[0].Mesh.GetValue().Materials[0].Effect.IsDefault();
	// 変更をもう一度適用できたか。
	const bool bRedo = Document.Redo();
	C.Check( bUndoRestored && bRedo && Document.Get().Objects[0].Mesh.GetValue().Materials[0].Effect.ComputeHash() == ChangedOpacity.ComputeHash() && Document.Get().Objects[0].Mesh.GetValue().ComputeHash() == EditedHash, "Mesh effectのJSON履歴がUndo/Redoで全項目を復元する" );

	// World validationはmesh内の数値と有効な画像参照を伝える。
	// 参照検査を行うvalidation world。
	FWorldDefinition ValidationWorld;
	// meshを格納するworld object。
	FWorldObject ValidationObject;
	ValidationObject.Id = 2;
	ValidationObject.Name = "Surface";
	// 有効なEffectを持つmesh。
	FEditMesh ValidationMesh;
	ValidationMesh.Materials.Add( FEditMeshMaterial() );
	ValidationMesh.Materials[0].Effect = MakeReferencedEffect();
	ValidationObject.Mesh = ValidationMesh;
	ValidationWorld.Objects.Add( ValidationObject );
	// mask/noiseの有無を切り替えるasset query。
	FTestAssetQuery AssetQuery;
	// 両画像を欠いた時のvalidation結果。
	const TArray<FWorldIssue> MissingAssets = NWorld::ValidateWorld( ValidationWorld, &AssetQuery );
	// 両方の画像がmissingとして指摘されたか。
	const bool bBothMissing = CountWorldIssues( MissingAssets, EWorldIssueCode::MissingAsset, EWorldIssueSeverity::Error ) == 2;
	AssetQuery.bHasMask = true;
	// maskだけ存在する時のvalidation結果。
	const TArray<FWorldIssue> MissingNoise = NWorld::ValidateWorld( ValidationWorld, &AssetQuery );
	// noiseだけがmissingとして指摘されたか。
	const bool bOnlyNoiseMissing = CountWorldIssues( MissingNoise, EWorldIssueCode::MissingAsset, EWorldIssueSeverity::Error ) == 1;
	AssetQuery.bHasNoise = true;
	// 両画像が存在する時のvalidation結果。
	const TArray<FWorldIssue> ExistingAssets = NWorld::ValidateWorld( ValidationWorld, &AssetQuery );
	C.Check( bBothMissing && bOnlyNoiseMissing && !HasWorldIssue( ExistingAssets, EWorldIssueCode::MissingAsset, EWorldIssueSeverity::Error ), "有効なMaskと正強度NoiseだけWorldのasset検査に渡される" );
	// 空Strengthを0として扱うNoise未読込のworld。
	FWorldDefinition EmptyStrengthWorld = ValidationWorld;
	// 画像参照の検査対象となる材質効果。
	FMeshMaterialEffectDefinition& EmptyStrengthEffect = EmptyStrengthWorld.Objects[0].Mesh.GetValue().Materials[0].Effect;
	EmptyStrengthEffect.Material.bDissolve = false;
	EmptyStrengthEffect.Material.Distortion.Strength.Keys.Reset();
	// 画像を一つも提供しない問い合わせ先。
	FTestAssetQuery NoEffectAssets;
	// 空曲線時にNoise参照が必要とされない結果。
	const TArray<FWorldIssue> EmptyStrengthIssues = NWorld::ValidateWorld( EmptyStrengthWorld, &NoEffectAssets );
	C.Check( CountWorldIssues( EmptyStrengthIssues, EWorldIssueCode::MissingAsset, EWorldIssueSeverity::Error ) == 0, "空のStrengthは0扱いとなりNoise画像を要求しない" );
	// 空のStrengthと空のTextureを持つ有効な歪み設定。
	FMeshMaterialEffectDefinition EmptyStrengthNoTexture;
	EmptyStrengthNoTexture.Material.bEnabled = true;
	EmptyStrengthNoTexture.Material.Distortion.bEnabled = true;
	// 0だけのキーを持ち、Textureを使わない歪み設定。
	FMeshMaterialEffectDefinition ZeroStrengthNoTexture = EmptyStrengthNoTexture;
	ZeroStrengthNoTexture.Material.Distortion.Strength.Keys.Add( { 0.0f, 0.0f } );
	// 正の強度キーがありTextureだけ欠けた歪み設定。
	FMeshMaterialEffectDefinition PositiveStrengthNoTexture = EmptyStrengthNoTexture;
	PositiveStrengthNoTexture.Material.Distortion.Strength.Keys.Add( { 0.0f, 0.25f } );
	C.Check( EmptyStrengthNoTexture.IsValidSettings() && ZeroStrengthNoTexture.IsValidSettings() && !PositiveStrengthNoTexture.IsValidSettings(), "空または0のStrengthならTexture不要で、正のStrengthには必要" );
	// 不正Texture型とNaN曲線値を含む歪みJSON。
	NlohmannJson InvalidDistortionJson = NlohmannJson::object();
	InvalidDistortionJson["Material"]["Enabled"] = true;
	InvalidDistortionJson["Material"]["Distortion"]["Enabled"] = true;
	InvalidDistortionJson["Material"]["Distortion"]["Texture"] = nullptr;
	InvalidDistortionJson["Material"]["Distortion"]["Strength"]["Interpolation"] = "Linear";
	InvalidDistortionJson["Material"]["Distortion"]["Strength"]["Keys"] = NlohmannJson::array();
	NlohmannJson InvalidStrengthKey = NlohmannJson::object();
	InvalidStrengthKey["Time"] = 0.5f;
	InvalidStrengthKey["Value"] = "not-a-number";
	InvalidDistortionJson["Material"]["Distortion"]["Strength"]["Keys"].push_back( InvalidStrengthKey );
	// 不正な歪み値を読み取る定義。
	FMeshMaterialEffectDefinition InvalidDistortion;
	// JSON保存・再読後の不正な歪み値。
	FMeshMaterialEffectDefinition InvalidDistortionAgain;
	const bool bInvalidDistortionRoundTrip = FMeshMaterialEffectDefinition::FromJson( InvalidDistortionJson, InvalidDistortion ) && FMeshMaterialEffectDefinition::FromJson( NlohmannJson::parse( InvalidDistortion.ToJson().dump() ), InvalidDistortionAgain );
	C.Check( bInvalidDistortionRoundTrip && !InvalidDistortion.IsValidSettings() && !InvalidDistortionAgain.IsValidSettings(), "Texture型違いとNaN曲線値はJSON往復後も不正" );
	// 参照pathを残したまま効果を無効にしたworld.
	FWorldDefinition DisabledWorld = ValidationWorld;
	// このworld内のdisabled材質効果。
	FMeshMaterialEffectDefinition& DisabledEffect = DisabledWorld.Objects[0].Mesh.GetValue().Materials[0].Effect;
	DisabledEffect.Material.bEnabled = false;
	// 欠けた画像参照を持つdisabled効果の問い合わせ先。
	FTestAssetQuery DisabledMissingQuery;
	// 無効化後もpathだけ残した効果を、両画像が欠けた状態で検査する。
	const TArray<FWorldIssue> DisabledAssets = NWorld::ValidateWorld( DisabledWorld, &DisabledMissingQuery );
	C.Check( CountWorldIssues( DisabledAssets, EWorldIssueCode::MissingAsset, EWorldIssueSeverity::Error ) == 0, "無効な効果に残る未存在Mask/Noise pathはWorldのasset errorにしない" );

	// Duration範囲外のmeshを含むworld。
	FWorldDefinition InvalidWorld = ValidationWorld;
	InvalidWorld.Objects[0].Mesh.GetValue().Materials[0].Effect.Duration = 0.0f;
	// mesh effectの値域エラーを含むvalidation結果。
	const TArray<FWorldIssue> InvalidIssues = NWorld::ValidateWorld( InvalidWorld, nullptr );
	C.Check( HasWorldIssue( InvalidIssues, EWorldIssueCode::BadMesh, EWorldIssueSeverity::Error ), "EffectのDuration範囲エラーはWorld validationへ伝わる" );
}
