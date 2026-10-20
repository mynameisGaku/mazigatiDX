// SPDX-License-Identifier: NOASSERTION
#include "EffectMaterialDefinition.h"

#include <cmath>
#include <limits>

#include "Library/Json/JsonHelpers.h"
#include "Library/Framework/World/WorldDefinition.h"

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

	// 数値を丸めず FVector2 へ読む。配列や成分が不正なら false にする。
	bool ReadVector2( const NlohmannJson& Json, FVector2& Out )
	{
		const float32 Invalid = std::numeric_limits<float32>::quiet_NaN(); // 不正値を検証へ残す値。
		Out = FVector2( Invalid, Invalid );
		if ( !Json.is_array() || Json.size() < 2 || !Json[0].is_number() || !Json[1].is_number() ) return false;
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

	// 数値を丸めず float32 へ読む。型違い・変換失敗は false にする。
	bool ReadFloat( const NlohmannJson& Json, float32& Out )
	{
		// 読めない値が検証で見つかるよう、失敗値を NaN にする。
		Out = std::numeric_limits<float32>::quiet_NaN();
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
}

NlohmannJson FEffectMaterialDefinition::ToJson() const
{
	NlohmannJson Json = NlohmannJson::object(); // 材質の保存先。
	Json["Enabled"] = bEnabled;
	Json["DissolveEnabled"] = bDissolve;
	if ( !MaskTexture.IsEmpty() ) Json["MaskTexture"] = MaskTexture;
	Json["UVScale"] = NlohmannJson::array( { UVScale.x, UVScale.y } );
	Json["UVScroll"] = NlohmannJson::array( { UVScroll.x, UVScroll.y } );
	Json["MaskScale"] = MaskScale;
	Json["Dissolve"] = Dissolve.ToJson();
	Json["Emission"] = Emission.ToJson();
	Json["EdgeWidth"] = EdgeWidth;
	Json["EdgeColor"] = NWorld::FormatColor( EdgeColor );
	Json["EdgeEmission"] = EdgeEmission;
	if ( !Distortion.IsDefault() ) Json["Distortion"] = Distortion.ToJson();
	return Json;
}

bool FEffectMaterialDefinition::FromJson( const NlohmannJson& Json, FEffectMaterialDefinition& Out, TArray<FString>* Warnings )
{
	if ( !Json.is_object() ) return false;
	FEffectMaterialDefinition Material; // 入力を読んで組み立てる材質。
	bool bReadError = false; // 型違いを既定値のまま保存させない印。
	if ( Json.contains( "Enabled" ) )
	{
		if ( Json["Enabled"].is_boolean() ) Material.bEnabled = Json["Enabled"].get<bool>();
		else { bReadError = true; if ( Warnings != nullptr ) Warnings->Add( "Enabled は true / false にしてください" ); }
	}
	if ( Json.contains( "DissolveEnabled" ) )
	{
		if ( Json["DissolveEnabled"].is_boolean() ) Material.bDissolve = Json["DissolveEnabled"].get<bool>();
		else { bReadError = true; if ( Warnings != nullptr ) Warnings->Add( "DissolveEnabled は true / false にしてください" ); }
	}
	if ( Json.contains( "MaskTexture" ) )
	{
		if ( Json["MaskTexture"].is_string() ) Material.MaskTexture = NJson::GetOr<FString>( Json, "MaskTexture", FString() );
		else { bReadError = true; if ( Warnings != nullptr ) Warnings->Add( "MaskTexture は文字列にしてください" ); }
	}
	if ( Json.contains( "UVScale" ) && !ReadVector2( Json["UVScale"], Material.UVScale ) ) { bReadError = true; if ( Warnings != nullptr ) Warnings->Add( "UVScale の値が不正です。保存前に修正してください" ); }
	if ( Json.contains( "UVScroll" ) && !ReadVector2( Json["UVScroll"], Material.UVScroll ) ) { bReadError = true; if ( Warnings != nullptr ) Warnings->Add( "UVScroll の値が不正です。保存前に修正してください" ); }
	if ( Json.contains( "MaskScale" ) && !ReadFloat( Json["MaskScale"], Material.MaskScale ) ) { bReadError = true; if ( Warnings != nullptr ) Warnings->Add( "MaskScale の値が不正です。保存前に修正してください" ); }
	if ( Json.contains( "Dissolve" ) )
	{
		TArray<FString> CurveWarnings; // Dissolve 曲線だけで見つかった読み込み警告。
		if ( !FEffectScalarCurve::FromJson( Json["Dissolve"], Material.Dissolve, &CurveWarnings ) )
		{
			bReadError = true;
			Material.Dissolve.Interpolation = static_cast<FEffectScalarCurve::EInterpolation>( 0xFFu );
			if ( Warnings != nullptr ) Warnings->Add( "Dissolve はオブジェクトにしてください" );
		}
		for ( const FString& Warning : CurveWarnings ) if ( Warnings != nullptr ) Warnings->Add( FString( "Dissolve: " ) + Warning );
	}
	if ( Json.contains( "Emission" ) )
	{
		TArray<FString> CurveWarnings; // Emission 曲線だけで見つかった読み込み警告。
		if ( !FEffectScalarCurve::FromJson( Json["Emission"], Material.Emission, &CurveWarnings ) )
		{
			bReadError = true;
			Material.Emission.Interpolation = static_cast<FEffectScalarCurve::EInterpolation>( 0xFFu );
			if ( Warnings != nullptr ) Warnings->Add( "Emission はオブジェクトにしてください" );
		}
		for ( const FString& Warning : CurveWarnings ) if ( Warnings != nullptr ) Warnings->Add( FString( "Emission: " ) + Warning );
	}
	if ( Json.contains( "EdgeWidth" ) && !ReadFloat( Json["EdgeWidth"], Material.EdgeWidth ) ) { bReadError = true; if ( Warnings != nullptr ) Warnings->Add( "EdgeWidth の値が不正です。保存前に修正してください" ); }
	if ( Json.contains( "EdgeColor" ) )
	{
		FString Text; // 色として保存された文字列。
		if ( !Json["EdgeColor"].is_string() ) bReadError = true;
		else Text = NJson::GetOr<FString>( Json, "EdgeColor", FString() );
		if ( !NWorld::TryParseColor( Text, Material.EdgeColor ) ) { bReadError = true; if ( Warnings != nullptr ) Warnings->Add( "EdgeColor の値が不正です。保存前に修正してください" ); }
	}
	if ( Json.contains( "EdgeEmission" ) && !ReadFloat( Json["EdgeEmission"], Material.EdgeEmission ) ) { bReadError = true; if ( Warnings != nullptr ) Warnings->Add( "EdgeEmission の値が不正です。保存前に修正してください" ); }
	if ( Json.contains( "Distortion" ) )
	{
		// UV歪みの読み込み警告を材質内の項目名付きで渡す。
		TArray<FString> DistortionWarnings;
		FEffectUVDistortionDefinition::FromJson( Json["Distortion"], Material.Distortion, &DistortionWarnings );
		for ( const FString& Message : DistortionWarnings ) if ( Warnings != nullptr ) Warnings->Add( FString( "Distortion: " ) + Message );
	}
	if ( bReadError ) Material.Dissolve.Interpolation = static_cast<FEffectScalarCurve::EInterpolation>( 0xFFu );
	Out = std::move( Material );
	return true;
}

TArray<FString> FEffectMaterialDefinition::Validate() const
{
	TArray<FString> Issues; // 見つかった材質の問題。
	if ( !std::isfinite( UVScale.x ) || !std::isfinite( UVScale.y ) || UVScale.x <= 0.0f || UVScale.y <= 0.0f || UVScale.x > 128.0f || UVScale.y > 128.0f ) Issues.Add( "UVScale は有限の 0 より大きく 128 以下にしてください" );
	if ( !std::isfinite( UVScroll.x ) || !std::isfinite( UVScroll.y ) || std::fabs( UVScroll.x ) > 128.0f || std::fabs( UVScroll.y ) > 128.0f ) Issues.Add( "UVScroll は有限で各成分の絶対値を 128 以下にしてください" );
	if ( !std::isfinite( MaskScale ) || MaskScale <= 0.0f || MaskScale > 128.0f ) Issues.Add( "MaskScale は有限の 0 より大きく 128 以下にしてください" );
	for ( const FString& Issue : Dissolve.Validate() ) Issues.Add( "Dissolve: " + Issue );
	for ( const FEffectScalarCurve::FKey& Key : Dissolve.Keys ) if ( std::isfinite( Key.Value ) && ( Key.Value < 0.0f || Key.Value > 1.0f ) ) Issues.Add( "Dissolve の値は 0 から 1 にしてください" );
	for ( const FString& Issue : Emission.Validate() ) Issues.Add( "Emission: " + Issue );
	for ( const FEffectScalarCurve::FKey& Key : Emission.Keys ) if ( std::isfinite( Key.Value ) && ( Key.Value < 0.0f || Key.Value > 32.0f ) ) Issues.Add( "Emission の値は 0 から 32 にしてください" );
	for ( const FString& Issue : Distortion.Validate() ) Issues.Add( FString( "Distortion: " ) + Issue );
	if ( !std::isfinite( EdgeWidth ) || EdgeWidth < 0.0f || EdgeWidth > 1.0f ) Issues.Add( "EdgeWidth は有限の 0 から 1 にしてください" );
	if ( !std::isfinite( EdgeEmission ) || EdgeEmission < 0.0f || EdgeEmission > 32.0f ) Issues.Add( "EdgeEmission は有限の 0 から 32 にしてください" );
	return Issues;
}

uint64 FEffectMaterialDefinition::ComputeHash() const
{
	uint64 Hash = 1469598103934665603ull; // FNV-1a の初期値。
	MixValue( Hash, bEnabled );
	MixValue( Hash, bDissolve );
	for ( int32 Index = 0; Index < MaskTexture.Len(); ++Index ) MixValue( Hash, static_cast<uint32>( MaskTexture[Index] ) );
	MixValue( Hash, 0xFFFFFFFFu );
	MixValue( Hash, UVScale.x );
	MixValue( Hash, UVScale.y );
	MixValue( Hash, UVScroll.x );
	MixValue( Hash, UVScroll.y );
	MixValue( Hash, MaskScale );
	MixValue( Hash, Dissolve.ComputeHash() );
	MixValue( Hash, Emission.ComputeHash() );
	MixValue( Hash, EdgeWidth );
	MixValue( Hash, EdgeColor.r );
	MixValue( Hash, EdgeColor.g );
	MixValue( Hash, EdgeColor.b );
	MixValue( Hash, EdgeColor.a );
	MixValue( Hash, EdgeEmission );
	// 既定の歪み設定は混ぜず、従来の材質hashを維持する。
	if ( !Distortion.IsDefault() )
	{
		MixValue( Hash, uint32( 0x44535452u ) );
		MixValue( Hash, Distortion.ComputeHash() );
	}
	return Hash;
}
