// SPDX-License-Identifier: NOASSERTION
#include "EffectUVDistortionDefinition.h"


#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Json/JsonHelpers.h"

namespace
{
	// 不正な Enabled 型を保存検査へ残すbit。
	constexpr uint8 InvalidEnabled = 1u << 0;
	// 不正な Texture 型を保存検査へ残すbit。
	constexpr uint8 InvalidTexture = 1u << 1;
	// 不正な UVScale 型を保存検査へ残すbit。
	constexpr uint8 InvalidUVScale = 1u << 2;
	// 不正な UVScroll 型を保存検査へ残すbit。
	constexpr uint8 InvalidUVScroll = 1u << 3;
	// 不正な Strength 根型を保存検査へ残すbit。
	constexpr uint8 InvalidStrength = 1u << 4;
	// object以外のroot型を示す全bit。
	constexpr uint8 InvalidAll = InvalidEnabled | InvalidTexture | InvalidUVScale | InvalidUVScroll | InvalidStrength;

	// FNV-1aへ値のバイト列を加える。
	template<typename T>
	void MixValue( uint64& Hash, const T& Value )
	{
		// 型の余白を含めない値のバイト列。
		const uint8* Bytes = reinterpret_cast<const uint8*>( &Value );
		for ( usize Index = 0; Index < sizeof( T ); ++Index )
		{
			Hash ^= Bytes[Index];
			Hash *= 1099511628211ull;
		}
	}

	// 厳密な二要素数値配列を読み、型違いをNaNとして残す。
	bool ReadVector2( const NlohmannJson& Json, FVector2& Out )
	{
		// 読めない成分を検証で見つける値。
		const float32 Invalid = TNumericLimits<float32>::QuietNaN();
		Out = FVector2( Invalid, Invalid );
		if ( !Json.is_array() || Json.size() != 2 || !Json[0].is_number() || !Json[1].is_number() ) return false;
		try
		{
			Out = FVector2( Json[0].get<float32>(), Json[1].get<float32>() );
			return true;
		}
		catch ( const nlohmann::json::exception& )
		{
			return false;
		}
	}

	// 型違いの項目名を警告一覧へ加える。
	void Warn( TArray<FString>* Warnings, const char* Message )
	{
		if ( Warnings != nullptr ) Warnings->Add( Message );
	}
}

bool FEffectUVDistortionDefinition::IsDefault() const
{
	return InvalidFieldTypes == 0 && !bEnabled && Texture.IsEmpty() && UVScale == FVector2( 1.0f, 1.0f ) && UVScroll == FVector2( 0.0f, 0.0f ) && Strength.Interpolation == FEffectScalarCurve::EInterpolation::Linear && Strength.Keys.IsEmpty();
}

bool FEffectUVDistortionDefinition::IsValidSettings() const
{
	if ( InvalidFieldTypes != 0 || ( bEnabled && Texture.IsEmpty() ) ) return false;
	if ( !NMath::IsFinite( UVScale.x ) || !NMath::IsFinite( UVScale.y ) || UVScale.x <= 0.0f || UVScale.y <= 0.0f || UVScale.x > 128.0f || UVScale.y > 128.0f ) return false;
	if ( !NMath::IsFinite( UVScroll.x ) || !NMath::IsFinite( UVScroll.y ) || NMath::Abs( UVScroll.x ) > 128.0f || NMath::Abs( UVScroll.y ) > 128.0f ) return false;
	if ( Strength.Interpolation != FEffectScalarCurve::EInterpolation::Linear && Strength.Interpolation != FEffectScalarCurve::EInterpolation::Step && Strength.Interpolation != FEffectScalarCurve::EInterpolation::Smooth ) return false;
	if ( Strength.Keys.Num() > FEffectScalarCurve::MaxKeys ) return false;
	// 32件以下のキーを直接検査し、記録中に一時配列を作らない。
	float32 PreviousTime = -1.0f;
	for ( const FEffectScalarCurve::FKey& Key : Strength.Keys )
	{
		if ( !NMath::IsFinite( Key.Time ) || !NMath::IsFinite( Key.Value ) || Key.Time < 0.0f || Key.Time > 1.0f || Key.Time <= PreviousTime || Key.Value < 0.0f || Key.Value > 1.0f ) return false;
		PreviousTime = Key.Time;
	}
	return true;
}

NlohmannJson FEffectUVDistortionDefinition::ToJson() const
{
	// Distortionの保存先object。
	NlohmannJson Json = NlohmannJson::object();
	Json["Enabled"] = ( InvalidFieldTypes & InvalidEnabled ) != 0 ? NlohmannJson( nullptr ) : NlohmannJson( bEnabled );
	Json["Texture"] = ( InvalidFieldTypes & InvalidTexture ) != 0 ? NlohmannJson( nullptr ) : NlohmannJson( Texture );
	Json["UVScale"] = ( InvalidFieldTypes & InvalidUVScale ) != 0 ? NlohmannJson( nullptr ) : NlohmannJson::array( { UVScale.x, UVScale.y } );
	Json["UVScroll"] = ( InvalidFieldTypes & InvalidUVScroll ) != 0 ? NlohmannJson( nullptr ) : NlohmannJson::array( { UVScroll.x, UVScroll.y } );
	Json["Strength"] = ( InvalidFieldTypes & InvalidStrength ) != 0 ? NlohmannJson( nullptr ) : Strength.ToJson();
	return Json;
}

bool FEffectUVDistortionDefinition::FromJson( const NlohmannJson& Json, FEffectUVDistortionDefinition& Out, TArray<FString>* Warnings )
{
	// 入力を検証可能な形に読み込む作業用定義。
	FEffectUVDistortionDefinition Definition;
	if ( !Json.is_object() )
	{
		Definition.InvalidFieldTypes = InvalidAll;
		Warn( Warnings, "Distortion は object にしてください" );
		Out = MoveTemp( Definition );
		return true;
	}
	if ( Json.contains( "Enabled" ) )
	{
		if ( Json["Enabled"].is_boolean() ) Definition.bEnabled = Json["Enabled"].get<bool>();
		else { Definition.InvalidFieldTypes |= InvalidEnabled; Warn( Warnings, "Enabled は true / false にしてください" ); }
	}
	if ( Json.contains( "Texture" ) )
	{
		if ( Json["Texture"].is_string() ) Definition.Texture = NJson::GetOr<FString>( Json, "Texture", FString() );
		else { Definition.InvalidFieldTypes |= InvalidTexture; Warn( Warnings, "Texture は文字列にしてください" ); }
	}
	if ( Json.contains( "UVScale" ) && !ReadVector2( Json["UVScale"], Definition.UVScale ) )
	{
		Definition.InvalidFieldTypes |= InvalidUVScale;
		Warn( Warnings, "UVScale は二つの数値配列にしてください" );
	}
	if ( Json.contains( "UVScroll" ) && !ReadVector2( Json["UVScroll"], Definition.UVScroll ) )
	{
		Definition.InvalidFieldTypes |= InvalidUVScroll;
		Warn( Warnings, "UVScroll は二つの数値配列にしてください" );
	}
	if ( Json.contains( "Strength" ) )
	{
		// Strength曲線の内部読込警告。
		TArray<FString> CurveWarnings;
		if ( !FEffectScalarCurve::FromJson( Json["Strength"], Definition.Strength, &CurveWarnings ) )
		{
			Definition.InvalidFieldTypes |= InvalidStrength;
			Definition.Strength.Interpolation = static_cast<FEffectScalarCurve::EInterpolation>( 0xFFu );
			Warn( Warnings, "Strength は曲線objectにしてください" );
		}
		// 曲線内部の警告をDistortionの項目名付きで渡す。
		for ( const FString& Message : CurveWarnings ) if ( Warnings != nullptr ) Warnings->Add( FString( "Strength: " ) + Message );
	}
	Out = MoveTemp( Definition );
	return true;
}

TArray<FString> FEffectUVDistortionDefinition::Validate() const
{
	// 見つかった設定の問題。
	TArray<FString> Issues;
	if ( ( InvalidFieldTypes & InvalidEnabled ) != 0 ) Issues.Add( "Enabled の型が不正です" );
	if ( ( InvalidFieldTypes & InvalidTexture ) != 0 ) Issues.Add( "Texture の型が不正です" );
	if ( ( InvalidFieldTypes & InvalidUVScale ) != 0 ) Issues.Add( "UVScale の型が不正です" );
	if ( ( InvalidFieldTypes & InvalidUVScroll ) != 0 ) Issues.Add( "UVScroll の型が不正です" );
	if ( ( InvalidFieldTypes & InvalidStrength ) != 0 ) Issues.Add( "Strength の型が不正です" );
	if ( bEnabled && Texture.IsEmpty() ) Issues.Add( "有効なUV歪みにはTextureが必要です" );
	if ( !NMath::IsFinite( UVScale.x ) || !NMath::IsFinite( UVScale.y ) || UVScale.x <= 0.0f || UVScale.y <= 0.0f || UVScale.x > 128.0f || UVScale.y > 128.0f ) Issues.Add( "UVScaleは有限の0より大きく128以下にしてください" );
	if ( !NMath::IsFinite( UVScroll.x ) || !NMath::IsFinite( UVScroll.y ) || NMath::Abs( UVScroll.x ) > 128.0f || NMath::Abs( UVScroll.y ) > 128.0f ) Issues.Add( "UVScrollは有限で各成分の絶対値を128以下にしてください" );
	for ( const FString& Message : Strength.Validate() ) Issues.Add( FString( "Strength: " ) + Message );
	for ( const FEffectScalarCurve::FKey& Key : Strength.Keys ) if ( NMath::IsFinite( Key.Value ) && ( Key.Value < 0.0f || Key.Value > 1.0f ) ) Issues.Add( "Strengthの値は0から1にしてください" );
	return Issues;
}

uint64 FEffectUVDistortionDefinition::ComputeHash() const
{
	// Distortionの安定した識別hash。
	uint64 Hash = 1469598103934665603ull;
	MixValue( Hash, bEnabled );
	for ( int32 Index = 0; Index < Texture.Len(); ++Index ) MixValue( Hash, static_cast<uint32>( Texture[Index] ) );
	MixValue( Hash, 0xFFFFFFFFu );
	MixValue( Hash, UVScale.x );
	MixValue( Hash, UVScale.y );
	MixValue( Hash, UVScroll.x );
	MixValue( Hash, UVScroll.y );
	MixValue( Hash, Strength.ComputeHash() );
	MixValue( Hash, InvalidFieldTypes );
	return Hash;
}
