// SPDX-License-Identifier: NOASSERTION
#include "EffectMeshDefinition.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "Library/Json/JsonHelpers.h"

namespace
{
	// 読めなかった項目を往復後も検出するbit。
	constexpr uint8 InvalidKind = 1u << 0;
	// 分割数の型違いを保持するbit。
	constexpr uint8 InvalidSegments = 1u << 1;
	// 内半径の型違いを保持するbit。
	constexpr uint8 InvalidInnerRadius = 1u << 2;
	// 回転配列の型違いを保持するbit。
	constexpr uint8 InvalidRotation = 1u << 3;
	// root objectの型違いをすべての項目へ残すbit。
	constexpr uint8 InvalidAll = InvalidKind | InvalidSegments | InvalidInnerRadius | InvalidRotation;
	// unknown kindをNoneの四角形へ誤って戻さない番兵値。
	constexpr uint8 InvalidKindValue = 0xFFu;

	// JSON項目の型違いを呼び出し元へ伝える。
	void Warn( TArray<FString>* Warnings, const char* Message )
	{
		if ( Warnings != nullptr ) Warnings->Add( Message );
	}

	// FNV-1aへ値のbyte列を追加する。
	template<typename T>
	void MixValue( uint64& Hash, const T& Value )
	{
		// hashへ混ぜる値のbyte列。
		const uint8* Bytes = reinterpret_cast<const uint8*>( &Value );
		for ( usize Index = 0; Index < sizeof( T ); ++Index ) { Hash ^= Bytes[Index]; Hash *= 1099511628211ull; }
	}

	// JSON numberをfloatへ読み、失敗はNaNで保持する。
	bool ReadFloat( const NlohmannJson& Json, float32& Out )
	{
		// 読み取り不能を保存検証で見つける値。
		Out = std::numeric_limits<float32>::quiet_NaN();
		if ( !Json.is_number() ) return false;
		try { Out = Json.get<float32>(); return true; }
		catch ( const nlohmann::json::exception& ) { return false; }
	}

	// 三成分の厳密な数値配列を読む。
	bool ReadRotation( const NlohmannJson& Json, FVector3& Out )
	{
		// 読み取り不能を保存検証で見つける成分。
		const float32 Invalid = std::numeric_limits<float32>::quiet_NaN();
		Out = FVector3( Invalid, Invalid, Invalid );
		if ( !Json.is_array() || Json.size() != 3 || !Json[0].is_number() || !Json[1].is_number() || !Json[2].is_number() ) return false;
		try { Out = FVector3( Json[0].get<float32>(), Json[1].get<float32>(), Json[2].get<float32>() ); return true; }
		catch ( const nlohmann::json::exception& ) { return false; }
	}
}

const char* ToString( FEffectMeshDefinition::EKind Kind )
{
	switch ( Kind )
	{
	case FEffectMeshDefinition::EKind::None: return "None";
	case FEffectMeshDefinition::EKind::Plane: return "Plane";
	case FEffectMeshDefinition::EKind::Ring: return "Ring";
	default: return "Invalid";
	}
}

bool FEffectMeshDefinition::IsDefault() const
{
	return InvalidFieldTypes == 0 && Kind == EKind::None && Segments == 48 && InnerRadius == 0.65f && RotationDegrees == FVector3::Zero;
}

bool FEffectMeshDefinition::IsValidSettings() const
{
	return InvalidFieldTypes == 0 && ( Kind == EKind::None || Kind == EKind::Plane || Kind == EKind::Ring ) && Segments >= 3 && Segments <= 128 && std::isfinite( InnerRadius ) && InnerRadius >= 0.05f && InnerRadius <= 0.95f && std::isfinite( RotationDegrees.x ) && std::isfinite( RotationDegrees.y ) && std::isfinite( RotationDegrees.z ) && std::fabs( RotationDegrees.x ) <= 360000.0f && std::fabs( RotationDegrees.y ) <= 360000.0f && std::fabs( RotationDegrees.z ) <= 360000.0f;
}

NlohmannJson FEffectMeshDefinition::ToJson() const
{
	// mesh設定の保存先object。
	NlohmannJson Json = NlohmannJson::object();
	Json["Kind"] = ( InvalidFieldTypes & InvalidKind ) != 0 ? NlohmannJson( nullptr ) : NlohmannJson( ToString( Kind ) );
	Json["Segments"] = ( InvalidFieldTypes & InvalidSegments ) != 0 ? NlohmannJson( nullptr ) : NlohmannJson( Segments );
	Json["InnerRadius"] = ( InvalidFieldTypes & InvalidInnerRadius ) != 0 || !std::isfinite( InnerRadius ) ? NlohmannJson( nullptr ) : NlohmannJson( InnerRadius );
	Json["Rotation"] = ( InvalidFieldTypes & InvalidRotation ) != 0 || !std::isfinite( RotationDegrees.x ) || !std::isfinite( RotationDegrees.y ) || !std::isfinite( RotationDegrees.z ) ? NlohmannJson( nullptr ) : NlohmannJson::array( { RotationDegrees.x, RotationDegrees.y, RotationDegrees.z } );
	return Json;
}

bool FEffectMeshDefinition::FromJson( const NlohmannJson& Json, FEffectMeshDefinition& Out, TArray<FString>* Warnings )
{
	// 入力型違いを失わない一時定義。
	FEffectMeshDefinition Definition;
	if ( !Json.is_object() )
	{
		Definition.InvalidFieldTypes = InvalidAll;
		Warn( Warnings, "Mesh はobjectにしてください" );
		Out = std::move( Definition );
		return true;
	}
	if ( Json.contains( "Kind" ) )
	{
		if ( !Json["Kind"].is_string() ) { Definition.InvalidFieldTypes |= InvalidKind; Definition.Kind = static_cast<EKind>( InvalidKindValue ); Warn( Warnings, "Kindは文字列にしてください" ); }
		else
		{
			const FString Kind = NJson::GetOr<FString>( Json, "Kind", FString() );
			if ( Kind == "None" ) Definition.Kind = EKind::None;
			else if ( Kind == "Plane" ) Definition.Kind = EKind::Plane;
			else if ( Kind == "Ring" ) Definition.Kind = EKind::Ring;
			else { Definition.InvalidFieldTypes |= InvalidKind; Definition.Kind = static_cast<EKind>( InvalidKindValue ); Warn( Warnings, "KindはNone、Plane、Ringのいずれかにしてください" ); }
		}
	}
	if ( Json.contains( "Segments" ) )
	{
		if ( Json["Segments"].is_number_integer() )
		{
			try
			{
				if ( Json["Segments"].is_number_unsigned() )
				{
					// int32変換前に符号なし整数の範囲を調べる。
					const uint64 Value = Json["Segments"].get<uint64>();
					if ( Value < 3 || Value > 128 ) throw std::out_of_range( "segments range" );
					Definition.Segments = static_cast<int32>( Value );
				}
				else
				{
					// int32へ狭める前に符号付き整数の範囲を調べる。
					const int64 Value = Json["Segments"].get<int64>();
					if ( Value < 3 || Value > 128 ) throw std::out_of_range( "segments range" );
					Definition.Segments = static_cast<int32>( Value );
				}
			}
			catch ( const nlohmann::json::exception& ) { Definition.InvalidFieldTypes |= InvalidSegments; Warn( Warnings, "Segmentsは整数にしてください" ); }
			catch ( const std::out_of_range& ) { Definition.InvalidFieldTypes |= InvalidSegments; Warn( Warnings, "Segmentsは3から128にしてください" ); }
		}
		else { Definition.InvalidFieldTypes |= InvalidSegments; Warn( Warnings, "Segmentsは整数にしてください" ); }
	}
	if ( Json.contains( "InnerRadius" ) && !ReadFloat( Json["InnerRadius"], Definition.InnerRadius ) ) { Definition.InvalidFieldTypes |= InvalidInnerRadius; Warn( Warnings, "InnerRadiusは数値にしてください" ); }
	if ( Json.contains( "Rotation" ) && !ReadRotation( Json["Rotation"], Definition.RotationDegrees ) ) { Definition.InvalidFieldTypes |= InvalidRotation; Warn( Warnings, "Rotationは三つの数値配列にしてください" ); }
	Out = std::move( Definition );
	return true;
}

TArray<FString> FEffectMeshDefinition::Validate() const
{
	// 保存できないmesh値の一覧。
	TArray<FString> Issues;
	if ( ( InvalidFieldTypes & InvalidKind ) != 0 || ( Kind != EKind::None && Kind != EKind::Plane && Kind != EKind::Ring ) ) Issues.Add( "Mesh.KindはNone、Plane、Ringのいずれかにしてください" );
	if ( ( InvalidFieldTypes & InvalidSegments ) != 0 || Segments < 3 || Segments > 128 ) Issues.Add( "Mesh.Segmentsは3から128にしてください" );
	if ( ( InvalidFieldTypes & InvalidInnerRadius ) != 0 || !std::isfinite( InnerRadius ) || InnerRadius < 0.05f || InnerRadius > 0.95f ) Issues.Add( "Mesh.InnerRadiusは0.05から0.95にしてください" );
	if ( ( InvalidFieldTypes & InvalidRotation ) != 0 || !std::isfinite( RotationDegrees.x ) || !std::isfinite( RotationDegrees.y ) || !std::isfinite( RotationDegrees.z ) || std::fabs( RotationDegrees.x ) > 360000.0f || std::fabs( RotationDegrees.y ) > 360000.0f || std::fabs( RotationDegrees.z ) > 360000.0f ) Issues.Add( "Mesh.Rotationは有限で各軸の絶対値を360000度以下にしてください" );
	return Issues;
}

uint64 FEffectMeshDefinition::ComputeHash() const
{
	// mesh値と型違いを混ぜる識別値。
	uint64 Hash = 1469598103934665603ull;
	MixValue( Hash, static_cast<uint8>( Kind ) );
	MixValue( Hash, Segments );
	MixValue( Hash, InnerRadius );
	MixValue( Hash, RotationDegrees.x );
	MixValue( Hash, RotationDegrees.y );
	MixValue( Hash, RotationDegrees.z );
	MixValue( Hash, InvalidFieldTypes );
	return Hash;
}
