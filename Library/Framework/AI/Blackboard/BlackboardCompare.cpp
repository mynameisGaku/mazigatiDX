// SPDX-License-Identifier: MIT
#include "BlackboardCompare.h"

const char* NAI::ToString( EAICompareOp Op )
{
	switch ( Op )
	{
	case EAICompareOp::Equal:
		return "==";
	case EAICompareOp::NotEqual:
		return "!=";
	case EAICompareOp::Less:
		return "<";
	case EAICompareOp::LessEqual:
		return "<=";
	case EAICompareOp::Greater:
		return ">";
	case EAICompareOp::GreaterEqual:
		return ">=";
	}
	return "==";
}

bool NAI::TryParseCompareOp( const FString& Text, EAICompareOp& OutOp )
{
	static const EAICompareOp Ops[] =
	{
		EAICompareOp::Equal,
		EAICompareOp::NotEqual,
		EAICompareOp::Less,
		EAICompareOp::LessEqual,
		EAICompareOp::Greater,
		EAICompareOp::GreaterEqual,
	};
	for ( const EAICompareOp Op : Ops )
	{
		if ( Text == ToString( Op ) )
		{
			OutOp = Op;
			return true;
		}
	}
	return false;
}

bool NAI::SupportsOrdering( EBlackboardKeyType Type )
{
	return Type == EBlackboardKeyType::Int || Type == EBlackboardKeyType::Float || Type == EBlackboardKeyType::Enum;
}

bool NAI::CompareValues( EBlackboardKeyType Type, const FBlackboardValue& Current, EAICompareOp Op, const FBlackboardValue& Expected )
{
	if ( Op == EAICompareOp::Equal )
	{
		return Current.IsEqual( Type, Expected );
	}
	if ( Op == EAICompareOp::NotEqual )
	{
		return !Current.IsEqual( Type, Expected );
	}

	// 大小の比較は、数だけ
	float64 A = 0.0;
	float64 B = 0.0;
	if ( Type == EBlackboardKeyType::Float )
	{
		A = Current.Real;
		B = Expected.Real;
	}
	else if ( Type == EBlackboardKeyType::Int || Type == EBlackboardKeyType::Enum )
	{
		A = static_cast<float64>( Current.Integer );
		B = static_cast<float64>( Expected.Integer );
	}
	else
	{
		return false;
	}
	switch ( Op )
	{
	case EAICompareOp::Less:
		return A < B;
	case EAICompareOp::LessEqual:
		return A <= B;
	case EAICompareOp::Greater:
		return A > B;
	case EAICompareOp::GreaterEqual:
		return A >= B;
	default:
		return false;
	}
}

bool NAI::PrepareCompare( const FBlackboardKeyDefinition& Key, EAICompareOp Op, const NlohmannJson& Value, FBlackboardValue& OutCompare, FString& OutReason )
{
	if ( !FBlackboardDefinition::ValueFromJson( Key, Value, OutCompare ) )
	{
		OutReason = FString::Printf( "The Value does not fit the %s key '%s'.", NBlackboard::ToString( Key.Type ), *Key.Name );
		return false;
	}
	if ( Op != EAICompareOp::Equal && Op != EAICompareOp::NotEqual && !SupportsOrdering( Key.Type ) )
	{
		OutReason = FString::Printf( "The %s key '%s' can only be compared with == and !=.", NBlackboard::ToString( Key.Type ), *Key.Name );
		return false;
	}
	return true;
}
