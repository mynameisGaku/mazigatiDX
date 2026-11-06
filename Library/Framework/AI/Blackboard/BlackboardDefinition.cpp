// SPDX-License-Identifier: MIT
#include "BlackboardDefinition.h"

#include "Library/Json/JsonHelpers.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr int32 CurrentVersion = 1;

	bool IsFinite( float32 Value )
	{
		return NMath::IsFinite( Value );
	}
}

int32 FBlackboardDefinition::FindKeyIndex( const FString& Name ) const
{
	for ( int32 Index = 0; Index < Keys.Num(); ++Index )
	{
		if ( Keys[Index].Name == Name )
		{
			return Index;
		}
	}
	return -1;
}

NlohmannJson FBlackboardDefinition::ValueToJson( const FBlackboardKeyDefinition& Key, const FBlackboardValue& Value )
{
	switch ( Key.Type )
	{
	case EBlackboardKeyType::Bool:
		return Value.Integer != 0;
	case EBlackboardKeyType::Int:
		return Value.Integer;
	case EBlackboardKeyType::Float:
		return NJson::Round4( Value.Real );
	case EBlackboardKeyType::Vector3:
		return NJson::ToJson( Value.Vector );
	case EBlackboardKeyType::Name:
		return Value.Text;
	case EBlackboardKeyType::Object:
		return Value.ObjectId;
	case EBlackboardKeyType::Enum:
		return Value.Integer >= 0 && Value.Integer < Key.EnumValues.Num() ? NlohmannJson( Key.EnumValues[static_cast<int32>( Value.Integer )] ) : NlohmannJson( Value.Integer );
	}
	return NlohmannJson();
}

bool FBlackboardDefinition::ValueFromJson( const FBlackboardKeyDefinition& Key, const NlohmannJson& Json, FBlackboardValue& OutValue )
{
	switch ( Key.Type )
	{
	case EBlackboardKeyType::Bool:
		if ( !Json.is_boolean() )
		{
			return false;
		}
		OutValue = FBlackboardValue::MakeBool( Json.get<bool>() );
		return true;
	case EBlackboardKeyType::Int:
		if ( !Json.is_number() )
		{
			return false;
		}
		OutValue = FBlackboardValue::MakeInt( Json.get<int32>() );
		return true;
	case EBlackboardKeyType::Float:
	{
		if ( !Json.is_number() )
		{
			return false;
		}
		const float32 Real = Json.get<float32>();
		if ( !IsFinite( Real ) )
		{
			return false;
		}
		OutValue = FBlackboardValue::MakeFloat( Real );
		return true;
	}
	case EBlackboardKeyType::Vector3:
	{
		if ( !Json.is_array() || Json.size() < 3 || !Json[0].is_number() || !Json[1].is_number() || !Json[2].is_number() )
		{
			return false;
		}
		const FVector3 Vector( Json[0].get<float32>(), Json[1].get<float32>(), Json[2].get<float32>() );
		if ( !IsFinite( Vector.x ) || !IsFinite( Vector.y ) || !IsFinite( Vector.z ) )
		{
			return false;
		}
		OutValue = FBlackboardValue::MakeVector( Vector );
		return true;
	}
	case EBlackboardKeyType::Name:
		if ( !Json.is_string() )
		{
			return false;
		}
		OutValue = FBlackboardValue::MakeName( Json.get<FString>() );
		return true;
	case EBlackboardKeyType::Object:
		if ( !Json.is_number_unsigned() && !Json.is_number_integer() )
		{
			return false;
		}
		OutValue = FBlackboardValue::MakeObject( Json.get<uint64>() );
		return true;
	case EBlackboardKeyType::Enum:
	{
		if ( Json.is_string() )
		{
			const FString Name = Json.get<FString>();
			for ( int32 Index = 0; Index < Key.EnumValues.Num(); ++Index )
			{
				if ( Key.EnumValues[Index] == Name )
				{
					OutValue = FBlackboardValue::MakeEnum( Index );
					return true;
				}
			}
			return false;
		}
		if ( Json.is_number_integer() )
		{
			const int64 Index = Json.get<int64>();
			if ( Index < 0 || Index >= Key.EnumValues.Num() )
			{
				return false;
			}
			OutValue = FBlackboardValue::MakeEnum( static_cast<int32>( Index ) );
			return true;
		}
		return false;
	}
	}
	return false;
}

NlohmannJson FBlackboardDefinition::ToJson() const
{
	NlohmannJson Json = NlohmannJson::object();
	Json["Version"] = CurrentVersion;
	NlohmannJson Array = NlohmannJson::array();
	for ( const FBlackboardKeyDefinition& Key : Keys )
	{
		NlohmannJson Item = NlohmannJson::object();
		Item["Name"] = Key.Name;
		Item["Type"] = NBlackboard::ToString( Key.Type );
		if ( Key.Type == EBlackboardKeyType::Enum )
		{
			NlohmannJson Values = NlohmannJson::array();
			for ( const FString& Value : Key.EnumValues )
			{
				Values.push_back( Value );
			}
			Item["Values"] = Values;
		}
		// 既定値が型の初期値と同じなら、書かない（ファイルを短く保つ）
		if ( !Key.Default.IsEqual( Key.Type, FBlackboardValue() ) )
		{
			Item["Default"] = ValueToJson( Key, Key.Default );
		}
		if ( Key.bTrigger )
		{
			Item["Trigger"] = true;
		}
		Array.push_back( Item );
	}
	Json["Keys"] = Array;
	return Json;
}

bool FBlackboardDefinition::FromJson( const NlohmannJson& Json, FBlackboardDefinition& OutDefinition, TArray<FAIIssue>* Issues )
{
	if ( !Json.is_object() )
	{
		return false;
	}

	FBlackboardDefinition Definition;
	const auto KeysIt = Json.find( "Keys" );
	if ( KeysIt != Json.end() && KeysIt->is_array() )
	{
		int32 ItemIndex = 0;
		for ( const NlohmannJson& Item : *KeysIt )
		{
			const FString Where = FString::Printf( "Keys[%d]", ItemIndex++ );
			if ( !Item.is_object() )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The key is not an object. It was skipped." );
				continue;
			}

			FBlackboardKeyDefinition Key;
			Key.Name = NJson::GetOr<FString>( Item, "Name", FString() );
			if ( Key.Name.IsEmpty() )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "The key has no Name. It was skipped." );
				continue;
			}
			if ( Definition.FindKeyIndex( Key.Name ) >= 0 )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, FString::Printf( "The key '%s' is defined twice. The later one was skipped.", *Key.Name ) );
				continue;
			}
			if ( !NBlackboard::TryParseKeyType( NJson::GetOr<FString>( Item, "Type", FString() ), Key.Type ) )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, FString::Printf( "The key '%s' has an unknown Type. It was skipped.", *Key.Name ) );
				continue;
			}

			if ( Key.Type == EBlackboardKeyType::Enum )
			{
				const auto ValuesIt = Item.find( "Values" );
				if ( ValuesIt != Item.end() && ValuesIt->is_array() )
				{
					for ( const NlohmannJson& Value : *ValuesIt )
					{
						if ( Value.is_string() && !Key.EnumValues.Contains( Value.get<FString>() ) )
						{
							Key.EnumValues.Add( Value.get<FString>() );
						}
					}
				}
				if ( Key.EnumValues.IsEmpty() )
				{
					NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, FString::Printf( "The Enum key '%s' has no Values. It was skipped.", *Key.Name ) );
					continue;
				}
			}

			const auto DefaultIt = Item.find( "Default" );
			if ( DefaultIt != Item.end() && !DefaultIt->is_null() )
			{
				if ( !ValueFromJson( Key, *DefaultIt, Key.Default ) )
				{
					NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, FString::Printf( "The Default of the key '%s' does not fit its type. The initial value is used.", *Key.Name ) );
					Key.Default = FBlackboardValue();
				}
			}

			Key.bTrigger = NJson::GetOr<bool>( Item, "Trigger", false );
			if ( Key.bTrigger && Key.Type != EBlackboardKeyType::Bool )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, FString::Printf( "Only a Bool key can be a Trigger. '%s' is not a trigger.", *Key.Name ) );
				Key.bTrigger = false;
			}
			Definition.Keys.Add( Key );
		}
	}

	OutDefinition = Definition;
	return true;
}
