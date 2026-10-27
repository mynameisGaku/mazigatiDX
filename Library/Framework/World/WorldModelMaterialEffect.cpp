// SPDX-License-Identifier: MIT
#include "WorldModelMaterialEffect.h"


#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"

namespace
{
	// 材質番号の型違いを保持するbit。
	constexpr uint8 InvalidMaterialIndex = 1u << 0;
	// 効果rootの型違いを保持するbit。
	constexpr uint8 InvalidEffect = 1u << 1;
	// 両面設定の型違いを保持するbit。
	constexpr uint8 InvalidTwoSided = 1u << 2;
	// object以外のrootを示す全field bit。
	constexpr uint8 InvalidAll = InvalidMaterialIndex | InvalidEffect | InvalidTwoSided;

	// FNV-1aへ値のバイト列を加える。
	template<typename T>
	void MixValue( uint64& Hash, const T& Value )
	{
		// hashへ混ぜるbyte列の先頭。
		const uint8* Bytes = reinterpret_cast<const uint8*>( &Value );
		// hashへ混ぜるbyteの位置。
		for ( usize Index = 0; Index < sizeof( T ); ++Index )
		{
			Hash ^= Bytes[Index];
			Hash *= 1099511628211ull;
		}
	}

	// JSON整数を範囲確認付きでint32へ読み取る。
	bool ReadMaterialIndex( const NlohmannJson& Json, int32& Out )
	{
		if ( Json.is_number_unsigned() )
		{
			try
			{
				// 符号なし入力の値。
				const uint64 Number = Json.get<uint64>();
				if ( Number > static_cast<uint64>( TNumericLimits<int32>::Max() ) ) return false;
				Out = static_cast<int32>( Number );
				return true;
			}
			catch ( const nlohmann::json::exception& )
			{
				return false;
			}
		}
		if ( !Json.is_number_integer() ) return false;
		try
		{
			// 符号付き入力の値。
			const int64 Number = Json.get<int64>();
			if ( Number < static_cast<int64>( TNumericLimits<int32>::Min() ) || Number > static_cast<int64>( TNumericLimits<int32>::Max() ) ) return false;
			Out = static_cast<int32>( Number );
			return true;
		}
		catch ( const nlohmann::json::exception& )
		{
			return false;
		}
	}
}

NlohmannJson FWorldModelMaterialEffect::ToJson() const
{
	if ( bInvalidRoot ) return nullptr;
	// bindingの保存先object。
	NlohmannJson Json = NlohmannJson::object();
	Json["MaterialIndex"] = ( InvalidFieldTypes & InvalidMaterialIndex ) != 0 ? NlohmannJson( nullptr ) : NlohmannJson( MaterialIndex );
	Json["Effect"] = ( InvalidFieldTypes & InvalidEffect ) != 0 ? NlohmannJson( nullptr ) : Effect.ToJson();
	Json["TwoSided"] = ( InvalidFieldTypes & InvalidTwoSided ) != 0 ? NlohmannJson( nullptr ) : NlohmannJson( bTwoSided );
	return Json;
}

bool FWorldModelMaterialEffect::FromJson( const NlohmannJson& Json, FWorldModelMaterialEffect& Out, TArray<FString>* Warnings )
{
	// 検証可能な値を組み立てる作業用binding。
	FWorldModelMaterialEffect Definition;
	if ( !Json.is_object() )
	{
		Definition.bInvalidRoot = true;
		Definition.InvalidFieldTypes = InvalidAll;
		if ( Warnings != nullptr ) Warnings->Add( "MaterialEffect はobjectにしてください" );
		Out = MoveTemp( Definition );
		return true;
	}
	if ( Json.contains( "MaterialIndex" ) && !ReadMaterialIndex( Json["MaterialIndex"], Definition.MaterialIndex ) )
	{
		Definition.InvalidFieldTypes |= InvalidMaterialIndex;
		if ( Warnings != nullptr ) Warnings->Add( "MaterialEffect.MaterialIndex はint32範囲の整数にしてください" );
	}
	if ( Json.contains( "Effect" ) && !FMeshMaterialEffectDefinition::FromJson( Json["Effect"], Definition.Effect, Warnings ) )
	{
		Definition.InvalidFieldTypes |= InvalidEffect;
		if ( Warnings != nullptr ) Warnings->Add( "MaterialEffect.Effect はobjectにしてください" );
	}
	if ( Json.contains( "TwoSided" ) )
	{
		if ( Json["TwoSided"].is_boolean() ) Definition.bTwoSided = Json["TwoSided"].get<bool>();
		else
		{
			Definition.InvalidFieldTypes |= InvalidTwoSided;
			if ( Warnings != nullptr ) Warnings->Add( "MaterialEffect.TwoSided はtrue/falseにしてください" );
		}
	}
	Out = MoveTemp( Definition );
	return true;
}

TArray<FString> FWorldModelMaterialEffect::Validate() const
{
	// binding内で見つかった問題。
	TArray<FString> Issues;
	if ( bInvalidRoot ) Issues.Add( "MaterialEffectのroot型がobjectではありません" );
	if ( ( InvalidFieldTypes & InvalidMaterialIndex ) != 0 ) Issues.Add( "MaterialEffect.MaterialIndexの型または範囲が不正です" );
	if ( ( InvalidFieldTypes & InvalidEffect ) != 0 ) Issues.Add( "MaterialEffect.Effectの型が不正です" );
	if ( ( InvalidFieldTypes & InvalidTwoSided ) != 0 ) Issues.Add( "MaterialEffect.TwoSidedの型が不正です" );
	if ( MaterialIndex < 0 ) Issues.Add( "MaterialEffect.MaterialIndexは0以上にしてください" );
	for ( const FString& Issue : Effect.Validate() ) Issues.Add( "MaterialEffect.Effect: " + Issue );
	return Issues;
}

bool FWorldModelMaterialEffect::IsValidSettings() const
{
	return Validate().IsEmpty();
}

uint64 FWorldModelMaterialEffect::ComputeHash() const
{
	// binding全体の識別値。
	uint64 Hash = 1469598103934665603ull;
	MixValue( Hash, MaterialIndex );
	MixValue( Hash, Effect.ComputeHash() );
	MixValue( Hash, bTwoSided );
	MixValue( Hash, bInvalidRoot );
	MixValue( Hash, InvalidFieldTypes );
	return Hash;
}

bool FWorldModelMaterialEffect::IsSameAs( const FWorldModelMaterialEffect& Other ) const
{
	return bInvalidRoot == Other.bInvalidRoot && InvalidFieldTypes == Other.InvalidFieldTypes && MaterialIndex == Other.MaterialIndex && Effect.IsSameAs( Other.Effect ) && bTwoSided == Other.bTwoSided;
}
