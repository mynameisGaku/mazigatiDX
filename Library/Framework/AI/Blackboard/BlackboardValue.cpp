// SPDX-License-Identifier: MIT
#include "BlackboardValue.h"

const char* NBlackboard::ToString( EBlackboardKeyType Type )
{
	switch ( Type )
	{
	case EBlackboardKeyType::Bool:
		return "Bool";
	case EBlackboardKeyType::Int:
		return "Int";
	case EBlackboardKeyType::Float:
		return "Float";
	case EBlackboardKeyType::Vector3:
		return "Vector3";
	case EBlackboardKeyType::Name:
		return "Name";
	case EBlackboardKeyType::Object:
		return "Object";
	case EBlackboardKeyType::Enum:
		return "Enum";
	}
	return "Unknown";
}

bool NBlackboard::TryParseKeyType( const FString& Text, EBlackboardKeyType& OutType )
{
	static const EBlackboardKeyType Types[] =
	{
		EBlackboardKeyType::Bool,
		EBlackboardKeyType::Int,
		EBlackboardKeyType::Float,
		EBlackboardKeyType::Vector3,
		EBlackboardKeyType::Name,
		EBlackboardKeyType::Object,
		EBlackboardKeyType::Enum,
	};
	for ( const EBlackboardKeyType Type : Types )
	{
		if ( Text == ToString( Type ) )
		{
			OutType = Type;
			return true;
		}
	}
	return false;
}

FBlackboardValue FBlackboardValue::MakeBool( bool bValue )
{
	FBlackboardValue Value;
	Value.Integer = bValue ? 1 : 0;
	return Value;
}

FBlackboardValue FBlackboardValue::MakeInt( int32 InValue )
{
	FBlackboardValue Value;
	Value.Integer = InValue;
	return Value;
}

FBlackboardValue FBlackboardValue::MakeFloat( float32 InValue )
{
	FBlackboardValue Value;
	Value.Real = InValue;
	return Value;
}

FBlackboardValue FBlackboardValue::MakeVector( const FVector3& InValue )
{
	FBlackboardValue Value;
	Value.Vector = InValue;
	return Value;
}

FBlackboardValue FBlackboardValue::MakeName( const FString& InValue )
{
	FBlackboardValue Value;
	Value.Text = InValue;
	return Value;
}

FBlackboardValue FBlackboardValue::MakeObject( uint64 Id )
{
	FBlackboardValue Value;
	Value.ObjectId = Id;
	return Value;
}

FBlackboardValue FBlackboardValue::MakeEnum( int32 Index )
{
	FBlackboardValue Value;
	Value.Integer = Index;
	return Value;
}

bool FBlackboardValue::IsEqual( EBlackboardKeyType Type, const FBlackboardValue& Other ) const
{
	switch ( Type )
	{
	case EBlackboardKeyType::Bool:
	case EBlackboardKeyType::Int:
	case EBlackboardKeyType::Enum:
		return Integer == Other.Integer;
	case EBlackboardKeyType::Float:
		return Real == Other.Real;
	case EBlackboardKeyType::Vector3:
		return Vector.x == Other.Vector.x && Vector.y == Other.Vector.y && Vector.z == Other.Vector.z;
	case EBlackboardKeyType::Name:
		return Text == Other.Text;
	case EBlackboardKeyType::Object:
		return ObjectId == Other.ObjectId;
	}
	return false;
}
