// SPDX-License-Identifier: NOASSERTION
#include "EffectAppearanceDefinition.h"


#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"

namespace
{
	// FNV-1a へ値のバイトだけを加える。
	template<typename T>
	void MixValue( uint64& Hash, const T& Value )
	{
		// 型の余白を含めない、今回の値のバイト列。
		const uint8* Bytes = reinterpret_cast<const uint8*>( &Value );
		for ( usize Index = 0; Index < sizeof( T ); ++Index )
		{
			Hash ^= Bytes[Index];
			Hash *= 1099511628211ull;
		}
	}
}

float32 FEffectAppearanceDefinition::EvaluateSizeScale( float32 Time ) const
{
	// 曲線評価を 0..32 に制限し、直接壊れた定義も有限値にする。
	// 寿命位置の大きさ倍率。
	const float32 Value = Size.Evaluate( Time, 1.0f );
	return NMath::IsFinite( Value ) ? NMath::Clamp( Value, 0.0f, 32.0f ) : 1.0f;
}

FColor FEffectAppearanceDefinition::EvaluateColorScale( float32 Time ) const
{
	// 曲線評価を RGBA ごとに 0..1 へ収める。
	// 寿命位置の RGBA 倍率。
	const FColor Value = Color.Evaluate( Time, FColor::White );
	return FColor( NMath::IsFinite( Value.r ) ? NMath::Clamp( Value.r, 0.0f, 1.0f ) : 1.0f, NMath::IsFinite( Value.g ) ? NMath::Clamp( Value.g, 0.0f, 1.0f ) : 1.0f, NMath::IsFinite( Value.b ) ? NMath::Clamp( Value.b, 0.0f, 1.0f ) : 1.0f, NMath::IsFinite( Value.a ) ? NMath::Clamp( Value.a, 0.0f, 1.0f ) : 1.0f );
}

NlohmannJson FEffectAppearanceDefinition::ToJson() const
{
	// 表示倍率の保存先。
	NlohmannJson Json = NlohmannJson::object();
	Json["Size"] = Size.ToJson();
	Json["Color"] = Color.ToJson();
	return Json;
}

bool FEffectAppearanceDefinition::FromJson( const NlohmannJson& Json, FEffectAppearanceDefinition& Out, TArray<FString>* Warnings )
{
	if ( !Json.is_object() ) return false;
	// 入力から組み立てる表示倍率。
	FEffectAppearanceDefinition Appearance;
	if ( Json.contains( "Size" ) )
	{
		// Size 曲線だけで見つかった読み込み警告。
		TArray<FString> CurveWarnings;
		if ( !FEffectScalarCurve::FromJson( Json["Size"], Appearance.Size, &CurveWarnings ) )
		{
			Appearance.Size.Interpolation = static_cast<FEffectScalarCurve::EInterpolation>( 0xFFu );
			if ( Warnings != nullptr ) Warnings->Add( "Size は曲線オブジェクトにしてください" );
		}
		for ( const FString& Warning : CurveWarnings ) if ( Warnings != nullptr ) Warnings->Add( FString( "Size: " ) + Warning );
	}
	if ( Json.contains( "Color" ) )
	{
		// Color 曲線だけで見つかった読み込み警告。
		TArray<FString> CurveWarnings;
		if ( !FEffectColorCurve::FromJson( Json["Color"], Appearance.Color, &CurveWarnings ) )
		{
			Appearance.Color.Interpolation = static_cast<FEffectScalarCurve::EInterpolation>( 0xFFu );
			if ( Warnings != nullptr ) Warnings->Add( "Color は曲線オブジェクトにしてください" );
		}
		for ( const FString& Warning : CurveWarnings ) if ( Warnings != nullptr ) Warnings->Add( FString( "Color: " ) + Warning );
	}
	Out = MoveTemp( Appearance );
	return true;
}

TArray<FString> FEffectAppearanceDefinition::Validate() const
{
	// 見つかった表示倍率の問題。
	TArray<FString> Issues;
	for ( const FString& Issue : Size.Validate() ) Issues.Add( FString( "Size: " ) + Issue );
	for ( const FEffectScalarCurve::FKey& Key : Size.Keys ) if ( NMath::IsFinite( Key.Value ) && ( Key.Value < 0.0f || Key.Value > 32.0f ) ) Issues.Add( "Size の値は 0 から 32 にしてください" );
	for ( const FString& Issue : Color.Validate() ) Issues.Add( FString( "Color: " ) + Issue );
	return Issues;
}

uint64 FEffectAppearanceDefinition::ComputeHash() const
{
	// FNV-1a の初期値。
	uint64 Hash = 1469598103934665603ull;
	MixValue( Hash, Size.ComputeHash() );
	MixValue( Hash, Color.ComputeHash() );
	return Hash;
}
