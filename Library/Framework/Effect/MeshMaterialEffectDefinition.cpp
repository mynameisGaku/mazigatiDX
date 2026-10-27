// SPDX-License-Identifier: NOASSERTION
#include "MeshMaterialEffectDefinition.h"


#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Json/JsonHelpers.h"

namespace
{
	// 材質rootの型違いを保持するbit。
	constexpr uint8 InvalidMaterial = 1u << 0;
	// Durationの型違いを保持するbit。
	constexpr uint8 InvalidDuration = 1u << 1;
	// Loopの型違いを保持するbit。
	constexpr uint8 InvalidLoop = 1u << 2;
	// Opacityの型違いを保持するbit。
	constexpr uint8 InvalidOpacity = 1u << 3;
	// Blendの型違いまたは未知名を保持するbit。
	constexpr uint8 InvalidBlend = 1u << 4;
	// object以外のrootを示す全field bit。
	constexpr uint8 InvalidAll = InvalidMaterial | InvalidDuration | InvalidLoop | InvalidOpacity | InvalidBlend;

	// FNV-1aへ値のバイト列を加える。
	template<typename T>
	void MixValue( uint64& Hash, const T& Value )
	{
		// 型の余白を含めず、値そのもののbyteを読む。
		const uint8* Bytes = reinterpret_cast<const uint8*>( &Value );
		// hashへ混ぜるbyteの位置。
		for ( usize Index = 0; Index < sizeof( T ); ++Index )
		{
			Hash ^= Bytes[Index];
			Hash *= 1099511628211ull;
		}
	}

	// 線形曲線が既定の空曲線か調べる。
	bool IsDefaultCurve( const FEffectScalarCurve& Curve )
	{
		return Curve.Interpolation == FEffectScalarCurve::EInterpolation::Linear && Curve.Keys.IsEmpty();
	}

	// 内包材質が既定値か、各保存fieldを比較する。
	bool IsDefaultMaterial( const FEffectMaterialDefinition& Material )
	{
		return !Material.bEnabled && !Material.bDissolve && Material.MaskTexture.IsEmpty() && Material.UVScale == FVector2( 1.0f, 1.0f ) && Material.UVScroll == FVector2( 0.0f, 0.0f ) && Material.MaskScale == 1.0f && IsDefaultCurve( Material.Dissolve ) && IsDefaultCurve( Material.Emission ) && Material.EdgeWidth == 0.08f && Material.EdgeColor.r == 255 && Material.EdgeColor.g == 128 && Material.EdgeColor.b == 32 && Material.EdgeColor.a == 255 && Material.EdgeEmission == 2.0f && Material.Distortion.IsDefault();
	}

	// float32を丸めず読み、型違いをNaNとして残す。
	bool ReadFloat( const NlohmannJson& Json, float32& Out )
	{
		// 読めない値を後の有限値検査で見つける。
		Out = TNumericLimits<float32>::QuietNaN();
		if ( !Json.is_number() ) return false;
		try
		{
			Out = Json.get<float32>();
			return true;
		}
		catch ( const nlohmann::json::exception& )
		{
			return false;
		}
	}

	// Alpha/Addだけを保存名から読み取る。
	bool ParseBlend( const FString& Text, EDrawBlend& Out )
	{
		if ( Text.Equals( "Alpha", ESearchCase::IgnoreCase ) ) Out = EDrawBlend::Alpha;
		else if ( Text.Equals( "Add", ESearchCase::IgnoreCase ) ) Out = EDrawBlend::Add;
		else return false;
		return true;
	}

	// Blendの保存名を返し、許可外値をInvalidにする。
	const char* BlendName( EDrawBlend Blend )
	{
		if ( Blend == EDrawBlend::Alpha ) return "Alpha";
		if ( Blend == EDrawBlend::Add ) return "Add";
		return "Invalid";
	}

	// 曲線の補間設定と全キーを直接比較する。
	bool SameCurve( const FEffectScalarCurve& A, const FEffectScalarCurve& B )
	{
		if ( A.Interpolation != B.Interpolation || A.Keys.Num() != B.Keys.Num() ) return false;
		// 直接照合する曲線キーの番号。
		for ( int32 Index = 0; Index < A.Keys.Num(); ++Index ) if ( A.Keys[Index].Time != B.Keys[Index].Time || A.Keys[Index].Value != B.Keys[Index].Value ) return false;
		return true;
	}

	// 歪み設定の可視fieldと不正型markerをJSON構造で比較する。
	bool SameDistortion( const FEffectUVDistortionDefinition& A, const FEffectUVDistortionDefinition& B )
	{
		if ( A.bEnabled != B.bEnabled || A.Texture != B.Texture || A.UVScale != B.UVScale || A.UVScroll != B.UVScroll || !SameCurve( A.Strength, B.Strength ) ) return false;
		return A.ToJson() == B.ToJson();
	}

	// FEffectMaterialDefinitionの全fieldをhashでなく構造比較する。
	bool SameMaterial( const FEffectMaterialDefinition& A, const FEffectMaterialDefinition& B )
	{
		return A.bEnabled == B.bEnabled && A.bDissolve == B.bDissolve && A.MaskTexture == B.MaskTexture && A.UVScale == B.UVScale && A.UVScroll == B.UVScroll && A.MaskScale == B.MaskScale && SameCurve( A.Dissolve, B.Dissolve ) && SameCurve( A.Emission, B.Emission ) && A.EdgeWidth == B.EdgeWidth && A.EdgeColor.r == B.EdgeColor.r && A.EdgeColor.g == B.EdgeColor.g && A.EdgeColor.b == B.EdgeColor.b && A.EdgeColor.a == B.EdgeColor.a && A.EdgeEmission == B.EdgeEmission && SameDistortion( A.Distortion, B.Distortion );
	}
}

bool FMeshMaterialEffectDefinition::IsDefault() const
{
	return !bInvalidRoot && InvalidFieldTypes == 0 && IsDefaultMaterial( Material ) && Duration == 2.0f && bLoop && Opacity == 1.0f && Blend == EDrawBlend::Alpha;
}

bool FMeshMaterialEffectDefinition::IsValidSettings() const
{
	return Validate().IsEmpty();
}

NlohmannJson FMeshMaterialEffectDefinition::ToJson() const
{
	if ( bInvalidRoot ) return nullptr;
	// 材質効果の保存先object。
	NlohmannJson Json = NlohmannJson::object();
	Json["Material"] = ( InvalidFieldTypes & InvalidMaterial ) != 0 ? NlohmannJson( nullptr ) : Material.ToJson();
	Json["Duration"] = ( InvalidFieldTypes & InvalidDuration ) != 0 ? NlohmannJson( nullptr ) : NlohmannJson( Duration );
	Json["Loop"] = ( InvalidFieldTypes & InvalidLoop ) != 0 ? NlohmannJson( nullptr ) : NlohmannJson( bLoop );
	Json["Opacity"] = ( InvalidFieldTypes & InvalidOpacity ) != 0 ? NlohmannJson( nullptr ) : NlohmannJson( Opacity );
	Json["Blend"] = ( InvalidFieldTypes & InvalidBlend ) != 0 || ( Blend != EDrawBlend::Alpha && Blend != EDrawBlend::Add ) ? NlohmannJson( nullptr ) : NlohmannJson( BlendName( Blend ) );
	return Json;
}

bool FMeshMaterialEffectDefinition::FromJson( const NlohmannJson& Json, FMeshMaterialEffectDefinition& Out, TArray<FString>* Warnings )
{
	// 入力を検証可能な形で組み立てる作業用定義。
	FMeshMaterialEffectDefinition Definition;
	if ( !Json.is_object() )
	{
		Definition.bInvalidRoot = true;
		Definition.InvalidFieldTypes = InvalidAll;
		if ( Warnings != nullptr ) Warnings->Add( "Effect はobjectにしてください" );
		Out = MoveTemp( Definition );
		return true;
	}
	if ( Json.contains( "Material" ) )
	{
		if ( !FEffectMaterialDefinition::FromJson( Json["Material"], Definition.Material, Warnings ) )
		{
			Definition.InvalidFieldTypes |= InvalidMaterial;
			if ( Warnings != nullptr ) Warnings->Add( "Effect.Material はobjectにしてください" );
		}
	}
	if ( Json.contains( "Duration" ) && !ReadFloat( Json["Duration"], Definition.Duration ) )
	{
		Definition.InvalidFieldTypes |= InvalidDuration;
		if ( Warnings != nullptr ) Warnings->Add( "Effect.Duration は数値にしてください" );
	}
	if ( Json.contains( "Loop" ) )
	{
		if ( Json["Loop"].is_boolean() ) Definition.bLoop = Json["Loop"].get<bool>();
		else { Definition.InvalidFieldTypes |= InvalidLoop; if ( Warnings != nullptr ) Warnings->Add( "Effect.Loop はtrue/falseにしてください" ); }
	}
	if ( Json.contains( "Opacity" ) && !ReadFloat( Json["Opacity"], Definition.Opacity ) )
	{
		Definition.InvalidFieldTypes |= InvalidOpacity;
		if ( Warnings != nullptr ) Warnings->Add( "Effect.Opacity は数値にしてください" );
	}
	if ( Json.contains( "Blend" ) )
	{
		if ( Json["Blend"].is_string() )
		{
			// JSONに保存された合成名。
			const FString Text = NJson::GetOr<FString>( Json, "Blend", FString() );
			if ( !ParseBlend( Text, Definition.Blend ) ) Definition.InvalidFieldTypes |= InvalidBlend;
		}
		else Definition.InvalidFieldTypes |= InvalidBlend;
		if ( ( Definition.InvalidFieldTypes & InvalidBlend ) != 0 && Warnings != nullptr ) Warnings->Add( "Effect.Blend はAlphaまたはAddにしてください" );
	}
	Out = MoveTemp( Definition );
	return true;
}

TArray<FString> FMeshMaterialEffectDefinition::Validate() const
{
	// 検証で見つかった問題。
	TArray<FString> Issues;
	if ( bInvalidRoot ) Issues.Add( "Effectのroot型がobjectではありません" );
	if ( ( InvalidFieldTypes & InvalidMaterial ) != 0 ) Issues.Add( "Effect.Materialの型が不正です" );
	if ( ( InvalidFieldTypes & InvalidDuration ) != 0 ) Issues.Add( "Effect.Durationの型が不正です" );
	if ( ( InvalidFieldTypes & InvalidLoop ) != 0 ) Issues.Add( "Effect.Loopの型が不正です" );
	if ( ( InvalidFieldTypes & InvalidOpacity ) != 0 ) Issues.Add( "Effect.Opacityの型が不正です" );
	if ( ( InvalidFieldTypes & InvalidBlend ) != 0 ) Issues.Add( "Effect.Blendの値が不正です" );
	if ( !NMath::IsFinite( Duration ) || Duration <= 0.0f || Duration > 3600.0f ) Issues.Add( "Effect.Durationは有限の0より大きく3600以下にしてください" );
	if ( !NMath::IsFinite( Opacity ) || Opacity < 0.0f || Opacity > 1.0f ) Issues.Add( "Effect.Opacityは有限の0から1にしてください" );
	if ( Blend != EDrawBlend::Alpha && Blend != EDrawBlend::Add ) Issues.Add( "Effect.BlendはAlphaまたはAddにしてください" );
	// 正の強度キーがない歪みは実行時に強度0となり、画像を使わない。
	FEffectMaterialDefinition MaterialForValidation = Material;
	bool bHasPositiveDistortionStrength = false;
	for ( const FEffectScalarCurve::FKey& Key : MaterialForValidation.Distortion.Strength.Keys ) bHasPositiveDistortionStrength = bHasPositiveDistortionStrength || Key.Value > 0.0f;
	if ( !bHasPositiveDistortionStrength ) MaterialForValidation.Distortion.bEnabled = false;
	// 内包材質の問題をEffectの項目名付きで返す。
	for ( const FString& Issue : MaterialForValidation.Validate() ) Issues.Add( FString( "Effect.Material: " ) + Issue );
	return Issues;
}

uint64 FMeshMaterialEffectDefinition::ComputeHash() const
{
	// Effect全体の識別値。
	uint64 Hash = 1469598103934665603ull;
	MixValue( Hash, Material.ComputeHash() );
	MixValue( Hash, Duration );
	MixValue( Hash, bLoop );
	MixValue( Hash, Opacity );
	MixValue( Hash, static_cast<int32>( Blend ) );
	MixValue( Hash, bInvalidRoot );
	MixValue( Hash, InvalidFieldTypes );
	return Hash;
}

bool FMeshMaterialEffectDefinition::IsSameAs( const FMeshMaterialEffectDefinition& Other ) const
{
	return bInvalidRoot == Other.bInvalidRoot && InvalidFieldTypes == Other.InvalidFieldTypes && SameMaterial( Material, Other.Material ) && Duration == Other.Duration && bLoop == Other.bLoop && Opacity == Other.Opacity && Blend == Other.Blend;
}
