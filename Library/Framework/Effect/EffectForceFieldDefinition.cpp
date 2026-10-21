// SPDX-License-Identifier: NOASSERTION
#include "EffectForceFieldDefinition.h"

#include <cmath>
#include <limits>
#include <utility>

#include "Library/Json/JsonHelpers.h"

namespace
{
	// EnabledのJSON型違いを保持するbit。
	constexpr uint8 InvalidEnabled = 1u << 0;
	// KindのJSON型違いを保持するbit。
	constexpr uint8 InvalidKind = 1u << 1;
	// AccelerationのJSON型違いを保持するbit。
	constexpr uint8 InvalidAcceleration = 1u << 2;
	// PositionのJSON型違いを保持するbit。
	constexpr uint8 InvalidPosition = 1u << 3;
	// RadiusのJSON型違いを保持するbit。
	constexpr uint8 InvalidRadius = 1u << 4;
	// StrengthのJSON型違いを保持するbit。
	constexpr uint8 InvalidStrength = 1u << 5;
	// field object自体が不正な場合に立てるbit一式。
	constexpr uint8 InvalidAll = InvalidEnabled | InvalidKind | InvalidAcceleration | InvalidPosition | InvalidRadius | InvalidStrength;
	// 未知の種類をUniformとして動かさないための値。
	constexpr uint8 InvalidKindValue = 0xFFu;

	// 読み込み時の問題を呼び出し元へ伝える。
	void Warn( TArray<FString>* Warnings, const char* Message )
	{
		if ( Warnings != nullptr ) Warnings->Add( Message );
	}

	// 三成分のJSON配列を読み、不正時はNaNを保持する。
	bool ReadVector( const NlohmannJson& Json, FVector3& Out )
	{
		// Validateで読み取り失敗を検出する値。
		const float32 Invalid = std::numeric_limits<float32>::quiet_NaN();
		Out = FVector3( Invalid, Invalid, Invalid );
		if ( !Json.is_array() || Json.size() != 3 || !Json[0].is_number() || !Json[1].is_number() || !Json[2].is_number() ) return false;
		try { Out = FVector3( Json[0].get<float32>(), Json[1].get<float32>(), Json[2].get<float32>() ); return true; }
		catch ( const nlohmann::json::exception& ) { return false; }
	}

	// JSON numberを読み、不正時はNaNを保持する。
	bool ReadFloat( const NlohmannJson& Json, float32& Out )
	{
		// Validateで読み取り失敗を検出する値。
		Out = std::numeric_limits<float32>::quiet_NaN();
		if ( !Json.is_number() ) return false;
		try { Out = Json.get<float32>(); return true; }
		catch ( const nlohmann::json::exception& ) { return false; }
	}

	// FNV-1a hashへbyte列を追加する。
	void MixValue( uint64& Hash, const void* Data, usize Size )
	{
		// 保存値のbyte列。
		const uint8* Bytes = static_cast<const uint8*>( Data );
		for ( usize Index = 0; Index < Size; ++Index ) { Hash ^= Bytes[Index]; Hash *= 1099511628211ull; }
	}

	// 値型の全byteをhashへ追加する。
	template<typename T>
	void MixValue( uint64& Hash, const T& Value )
	{
		MixValue( Hash, &Value, sizeof( T ) );
	}

	// FVector3の実成分だけをhashへ追加する。
	void MixVector( uint64& Hash, const FVector3& Value )
	{
		MixValue( Hash, Value.x );
		MixValue( Hash, Value.y );
		MixValue( Hash, Value.z );
	}
}

const char* ToString( FEffectForceFieldDefinition::EKind Kind )
{
	switch ( Kind )
	{
	case FEffectForceFieldDefinition::EKind::Uniform: return "Uniform";
	case FEffectForceFieldDefinition::EKind::Radial: return "Radial";
	}
	return "Invalid";
}

bool FEffectForceFieldDefinition::IsValidSettings() const
{
	const auto IsValidVector = []( const FVector3& Value )
	{
		// 各成分が許容範囲にあるか調べる。
		return std::isfinite( Value.x ) && std::isfinite( Value.y ) && std::isfinite( Value.z ) && std::fabs( Value.x ) <= MaxMagnitude && std::fabs( Value.y ) <= MaxMagnitude && std::fabs( Value.z ) <= MaxMagnitude;
	};
	return InvalidFieldTypes == 0 && ( Kind == EKind::Uniform || Kind == EKind::Radial ) && IsValidVector( Acceleration ) && IsValidVector( Position ) && std::isfinite( Radius ) && Radius > 0.0f && Radius <= MaxMagnitude && std::isfinite( Strength ) && std::fabs( Strength ) <= MaxMagnitude;
}

FVector3 FEffectForceFieldDefinition::EvaluateAcceleration( const FVector3& WorldPosition ) const
{
	if ( !bEnabled || !IsValidSettings() || !std::isfinite( WorldPosition.x ) || !std::isfinite( WorldPosition.y ) || !std::isfinite( WorldPosition.z ) ) return FVector3::Zero;
	if ( Kind == EKind::Uniform ) return Acceleration;
	// World位置からfield中心までの差。
	const double DeltaX = static_cast<double>( WorldPosition.x ) - Position.x;
	const double DeltaY = static_cast<double>( WorldPosition.y ) - Position.y;
	const double DeltaZ = static_cast<double>( WorldPosition.z ) - Position.z;
	// 二乗和のoverflowを避けるdouble距離。
	const double Distance = std::sqrt( DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ );
	if ( !std::isfinite( Distance ) || Distance == 0.0 || Distance >= static_cast<double>( Radius ) ) return FVector3::Zero;
	// 半径に比例して弱まる加速度の大きさ。
	const double Magnitude = static_cast<double>( Strength ) * ( 1.0 - Distance / Radius );
	// 中心からの向きへ大きさを適用した結果。
	const FVector3 Result( static_cast<float32>( DeltaX / Distance * Magnitude ), static_cast<float32>( DeltaY / Distance * Magnitude ), static_cast<float32>( DeltaZ / Distance * Magnitude ) );
	if ( !std::isfinite( Result.x ) || !std::isfinite( Result.y ) || !std::isfinite( Result.z ) ) return FVector3::Zero;
	return Result;
}

NlohmannJson FEffectForceFieldDefinition::ToJson() const
{
	// 保存するfield object。
	NlohmannJson Json = NlohmannJson::object();
	Json["Enabled"] = ( InvalidFieldTypes & InvalidEnabled ) != 0 ? NlohmannJson( nullptr ) : NlohmannJson( bEnabled );
	Json["Kind"] = ( InvalidFieldTypes & InvalidKind ) != 0 || ( Kind != EKind::Uniform && Kind != EKind::Radial ) ? NlohmannJson( nullptr ) : NlohmannJson( ToString( Kind ) );
	Json["Acceleration"] = ( InvalidFieldTypes & InvalidAcceleration ) != 0 || !std::isfinite( Acceleration.x ) || !std::isfinite( Acceleration.y ) || !std::isfinite( Acceleration.z ) ? NlohmannJson( nullptr ) : NlohmannJson::array( { Acceleration.x, Acceleration.y, Acceleration.z } );
	Json["Position"] = ( InvalidFieldTypes & InvalidPosition ) != 0 || !std::isfinite( Position.x ) || !std::isfinite( Position.y ) || !std::isfinite( Position.z ) ? NlohmannJson( nullptr ) : NlohmannJson::array( { Position.x, Position.y, Position.z } );
	Json["Radius"] = ( InvalidFieldTypes & InvalidRadius ) != 0 || !std::isfinite( Radius ) ? NlohmannJson( nullptr ) : NlohmannJson( Radius );
	Json["Strength"] = ( InvalidFieldTypes & InvalidStrength ) != 0 || !std::isfinite( Strength ) ? NlohmannJson( nullptr ) : NlohmannJson( Strength );
	return Json;
}

bool FEffectForceFieldDefinition::FromJson( const NlohmannJson& Json, FEffectForceFieldDefinition& Out, TArray<FString>* Warnings )
{
	// 入力の不正状態も保持する読み込み先。
	FEffectForceFieldDefinition Definition;
	if ( !Json.is_object() )
	{
		Definition.InvalidFieldTypes = InvalidAll;
		Warn( Warnings, "力場はオブジェクトにしてください" );
		Out = std::move( Definition );
		return true;
	}
	if ( Json.contains( "Enabled" ) )
	{
		if ( Json["Enabled"].is_boolean() ) Definition.bEnabled = Json["Enabled"].get<bool>();
		else { Definition.InvalidFieldTypes |= InvalidEnabled; Warn( Warnings, "Enabledはtrueまたはfalseにしてください" ); }
	}
	if ( Json.contains( "Kind" ) )
	{
		if ( !Json["Kind"].is_string() ) { Definition.InvalidFieldTypes |= InvalidKind; Definition.Kind = static_cast<EKind>( InvalidKindValue ); Warn( Warnings, "KindはUniformまたはRadialにしてください" ); }
		else
		{
			// JSONに書かれたfieldの種類。
			const FString Kind = NJson::GetOr<FString>( Json, "Kind", FString() );
			if ( Kind == "Uniform" ) Definition.Kind = EKind::Uniform;
			else if ( Kind == "Radial" ) Definition.Kind = EKind::Radial;
			else { Definition.InvalidFieldTypes |= InvalidKind; Definition.Kind = static_cast<EKind>( InvalidKindValue ); Warn( Warnings, "KindはUniformまたはRadialにしてください" ); }
		}
	}
	if ( Json.contains( "Acceleration" ) && !ReadVector( Json["Acceleration"], Definition.Acceleration ) ) { Definition.InvalidFieldTypes |= InvalidAcceleration; Warn( Warnings, "Accelerationは三つの数値配列にしてください" ); }
	if ( Json.contains( "Position" ) && !ReadVector( Json["Position"], Definition.Position ) ) { Definition.InvalidFieldTypes |= InvalidPosition; Warn( Warnings, "Positionは三つの数値配列にしてください" ); }
	if ( Json.contains( "Radius" ) && !ReadFloat( Json["Radius"], Definition.Radius ) ) { Definition.InvalidFieldTypes |= InvalidRadius; Warn( Warnings, "Radiusは数値にしてください" ); }
	if ( Json.contains( "Strength" ) && !ReadFloat( Json["Strength"], Definition.Strength ) ) { Definition.InvalidFieldTypes |= InvalidStrength; Warn( Warnings, "Strengthは数値にしてください" ); }
	Out = std::move( Definition );
	return true;
}

TArray<FString> FEffectForceFieldDefinition::Validate() const
{
	// 保存前に修正が必要な値。
	TArray<FString> Issues;
	if ( ( InvalidFieldTypes & InvalidEnabled ) != 0 ) Issues.Add( "ForceField.Enabledはtrueまたはfalseにしてください" );
	if ( ( InvalidFieldTypes & InvalidKind ) != 0 || ( Kind != EKind::Uniform && Kind != EKind::Radial ) ) Issues.Add( "ForceField.KindはUniformまたはRadialにしてください" );
	const auto ValidateVector = [&]( const FVector3& Value, uint8 InvalidBit, const char* Name )
	{
		// 未使用fieldを含む三成分の保存状態。
		if ( ( InvalidFieldTypes & InvalidBit ) != 0 || !std::isfinite( Value.x ) || !std::isfinite( Value.y ) || !std::isfinite( Value.z ) || std::fabs( Value.x ) > MaxMagnitude || std::fabs( Value.y ) > MaxMagnitude || std::fabs( Value.z ) > MaxMagnitude ) Issues.Add( FString( "ForceField." ) + Name + "は有限で各成分の絶対値を1000000以下にしてください" );
	};
	ValidateVector( Acceleration, InvalidAcceleration, "Acceleration" );
	ValidateVector( Position, InvalidPosition, "Position" );
	if ( ( InvalidFieldTypes & InvalidRadius ) != 0 || !std::isfinite( Radius ) || Radius <= 0.0f || Radius > MaxMagnitude ) Issues.Add( "ForceField.Radiusは0より大きく1000000以下にしてください" );
	if ( ( InvalidFieldTypes & InvalidStrength ) != 0 || !std::isfinite( Strength ) || std::fabs( Strength ) > MaxMagnitude ) Issues.Add( "ForceField.Strengthは有限で絶対値を1000000以下にしてください" );
	return Issues;
}

uint64 FEffectForceFieldDefinition::ComputeHash() const
{
	// field内容から計算するFNV-1a hash。
	uint64 Hash = 1469598103934665603ull;
	// hash対象のboolを固定幅へ変換した値。
	const uint8 Enabled = bEnabled ? 1u : 0u;
	// 未知値も識別できるkindのbyte値。
	const uint8 KindValue = static_cast<uint8>( Kind );
	MixValue( Hash, Enabled );
	MixValue( Hash, KindValue );
	MixVector( Hash, Acceleration );
	MixVector( Hash, Position );
	MixValue( Hash, Radius );
	MixValue( Hash, Strength );
	MixValue( Hash, InvalidFieldTypes );
	return Hash;
}
