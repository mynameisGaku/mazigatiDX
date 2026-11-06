// SPDX-License-Identifier: MIT
#include "ParamSchema.h"

#include "Library/Core/Log/Log.h"
#include "Library/Framework/AI/AILog.h"
#include "Library/Json/JsonHelpers.h"
#include "Library/Math/MathUtils.h"

namespace
{
	const FString EmptyText;

	bool IsTextType( EParamType Type )
	{
		return Type == EParamType::String || Type == EParamType::Enum || Type == EParamType::BlackboardKey || Type == EParamType::Clip || Type == EParamType::AssetPath || Type == EParamType::Scalar;
	}
}

const char* ToString( EParamType Type )
{
	switch ( Type )
	{
	case EParamType::Bool:
		return "Bool";
	case EParamType::Int:
		return "Int";
	case EParamType::Float:
		return "Float";
	case EParamType::String:
		return "String";
	case EParamType::Enum:
		return "Enum";
	case EParamType::BlackboardKey:
		return "BlackboardKey";
	case EParamType::Clip:
		return "Clip";
	case EParamType::AssetPath:
		return "AssetPath";
	case EParamType::Scalar:
		return "Scalar";
	}
	return "Unknown";
}

//=============================================================================
// FParamSchema
//=============================================================================
FParamDefinition& FParamSchema::Add( const char* Name, EParamType Type )
{
	FParamDefinition Definition;
	Definition.Name = Name;
	Definition.Type = Type;
	Params.Add( Definition );
	return Params.Last();
}

FParamSchema& FParamSchema::Bool( const char* Name, bool bDefault )
{
	Add( Name, EParamType::Bool ).Default.bValue = bDefault;
	return *this;
}

FParamSchema& FParamSchema::Int( const char* Name, int32 Default )
{
	Add( Name, EParamType::Int ).Default.IntValue = Default;
	return *this;
}

FParamSchema& FParamSchema::Float( const char* Name, float32 Default )
{
	Add( Name, EParamType::Float ).Default.FloatValue = Default;
	return *this;
}

FParamSchema& FParamSchema::String( const char* Name, const char* Default )
{
	Add( Name, EParamType::String ).Default.TextValue = Default;
	return *this;
}

FParamSchema& FParamSchema::Enum( const char* Name, const char* Options, const char* Default )
{
	FParamDefinition& Definition = Add( Name, EParamType::Enum );
	FString Remaining = Options;
	while ( !Remaining.IsEmpty() )
	{
		const int32 Bar = Remaining.Find( "|" );
		if ( Bar == INDEX_NONE )
		{
			Definition.Options.Add( Remaining );
			break;
		}
		Definition.Options.Add( Remaining.Left( Bar ) );
		Remaining = Remaining.Mid( Bar + 1 );
	}
	Definition.Default.TextValue = Default;
	return *this;
}

FParamSchema& FParamSchema::BlackboardKey( const char* Name )
{
	Add( Name, EParamType::BlackboardKey );
	return *this;
}

FParamSchema& FParamSchema::Clip( const char* Name )
{
	Add( Name, EParamType::Clip );
	return *this;
}

FParamSchema& FParamSchema::AssetPath( const char* Name )
{
	Add( Name, EParamType::AssetPath );
	return *this;
}

FParamSchema& FParamSchema::Scalar( const char* Name )
{
	Add( Name, EParamType::Scalar );
	return *this;
}

FParamSchema& FParamSchema::Range( float32 Min, float32 Max )
{
	if ( !Params.IsEmpty() )
	{
		FParamDefinition& Definition = Params.Last();
		Definition.Min = Min;
		Definition.Max = Max;
		Definition.bHasRange = true;
	}
	return *this;
}

FParamSchema& FParamSchema::Hint( const char* Text )
{
	if ( !Params.IsEmpty() )
	{
		Params.Last().Hint = Text;
	}
	return *this;
}

FParamSchema& FParamSchema::Required()
{
	if ( !Params.IsEmpty() )
	{
		Params.Last().bRequired = true;
	}
	return *this;
}

int32 FParamSchema::FindIndex( const FString& Name ) const
{
	for ( int32 Index = 0; Index < Params.Num(); ++Index )
	{
		if ( Params[Index].Name == Name )
		{
			return Index;
		}
	}
	return -1;
}

//=============================================================================
// FParamSet
//=============================================================================
FParamSet::FParamSet( const FParamSchema& InSchema )
	: Schema( InSchema )
{
	for ( const FParamDefinition& Definition : Schema.GetParams() )
	{
		Values.Add( Definition.Default );
	}
}

const FParamValue* FParamSet::Find( const char* Name, EParamType Expected1, EParamType Expected2 ) const
{
	const int32 Index = Schema.FindIndex( Name );
	if ( Index < 0 )
	{
		LOG( LogAI, Error, "Parameter '%s' is not in the schema (a bug in the part that reads it).", Name );
		return nullptr;
	}
	const EParamType Type = Schema.GetParams()[Index].Type;
	if ( Type != Expected1 && Type != Expected2 )
	{
		LOG( LogAI, Error, "Parameter '%s' is %s but was read as %s (a bug in the part that reads it).", Name, ToString( Type ), ToString( Expected1 ) );
		return nullptr;
	}
	return &Values[Index];
}

bool FParamSet::GetBool( const char* Name ) const
{
	const FParamValue* Value = Find( Name, EParamType::Bool, EParamType::Bool );
	return Value != nullptr && Value->bValue;
}

int32 FParamSet::GetInt( const char* Name ) const
{
	const FParamValue* Value = Find( Name, EParamType::Int, EParamType::Int );
	return Value != nullptr ? Value->IntValue : 0;
}

float32 FParamSet::GetFloat( const char* Name ) const
{
	const FParamValue* Value = Find( Name, EParamType::Float, EParamType::Float );
	return Value != nullptr ? Value->FloatValue : 0.0f;
}

const FString& FParamSet::GetText( const char* Name ) const
{
	const int32 Index = Schema.FindIndex( Name );
	if ( Index < 0 || !IsTextType( Schema.GetParams()[Index].Type ) )
	{
		LOG( LogAI, Error, "Parameter '%s' is not a text parameter in the schema (a bug in the part that reads it).", Name );
		return EmptyText;
	}
	return Values[Index].TextValue;
}

NlohmannJson FParamSet::GetJson( const char* Name ) const
{
	const int32 Index = Schema.FindIndex( Name );
	if ( Index < 0 || Schema.GetParams()[Index].Type != EParamType::Scalar )
	{
		LOG( LogAI, Error, "Parameter '%s' is not a Scalar parameter in the schema (a bug in the part that reads it).", Name );
		return NlohmannJson();
	}
	const FString& Text = Values[Index].TextValue;
	return Text.IsEmpty() ? NlohmannJson() : NlohmannJson::parse( *Text, nullptr, false );
}

//=============================================================================
// NAI
//=============================================================================
FParamSet NAI::ResolveParams( const FParamSchema& Schema, const NlohmannJson* ParamsJson, const FString& Where, TArray<FAIIssue>* Issues )
{
	FParamSet Result( Schema );
	const bool bHasObject = ParamsJson != nullptr && ParamsJson->is_object();
	if ( ParamsJson != nullptr && !ParamsJson->is_null() && !bHasObject )
	{
		NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where, "Params is not an object. The defaults are used." );
	}

	for ( int32 Index = 0; Index < Schema.GetNumParams(); ++Index )
	{
		const FParamDefinition& Definition = Schema.GetParams()[Index];
		FParamValue& Out = Result.GetValues()[Index];
		const FString ParamWhere = Where + "/" + Definition.Name;

		const NlohmannJson* Item = nullptr;
		if ( bHasObject )
		{
			const auto It = ParamsJson->find( *Definition.Name );
			if ( It != ParamsJson->end() && !It->is_null() )
			{
				Item = &*It;
			}
		}
		if ( Item == nullptr )
		{
			if ( Definition.bRequired )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Error, ParamWhere, "The required parameter is missing." );
			}
			continue;
		}

		bool bValid = true;
		switch ( Definition.Type )
		{
		case EParamType::Bool:
			bValid = Item->is_boolean();
			if ( bValid )
			{
				Out.bValue = Item->get<bool>();
			}
			break;
		case EParamType::Int:
			bValid = Item->is_number();
			if ( bValid )
			{
				float64 Number = Item->get<float64>();
				if ( Definition.bHasRange )
				{
					const float64 Clamped = NMath::Clamp( Number, static_cast<float64>( Definition.Min ), static_cast<float64>( Definition.Max ) );
					if ( Clamped != Number )
					{
						NAI::AddIssue( Issues, EAIIssueSeverity::Info, ParamWhere, FString::Printf( "The value %g was clamped to %g.", Number, Clamped ) );
						Number = Clamped;
					}
				}
				Out.IntValue = static_cast<int32>( NMath::Clamp( Number, -2147483648.0, 2147483647.0 ) );
			}
			break;
		case EParamType::Float:
			bValid = Item->is_number() && NMath::IsFinite( Item->get<float32>() );
			if ( bValid )
			{
				float32 Number = Item->get<float32>();
				if ( Definition.bHasRange )
				{
					const float32 Clamped = NMath::Clamp( Number, Definition.Min, Definition.Max );
					if ( Clamped != Number )
					{
						NAI::AddIssue( Issues, EAIIssueSeverity::Info, ParamWhere, FString::Printf( "The value %g was clamped to %g.", static_cast<float64>( Number ), static_cast<float64>( Clamped ) ) );
						Number = Clamped;
					}
				}
				Out.FloatValue = Number;
			}
			break;
		case EParamType::Enum:
			bValid = Item->is_string() && Definition.Options.Contains( Item->get<FString>() );
			if ( bValid )
			{
				Out.TextValue = Item->get<FString>();
			}
			break;
		case EParamType::Scalar:
			bValid = Item->is_boolean() || Item->is_string() || ( Item->is_number() && NMath::IsFinite( Item->get<float32>() ) );
			if ( bValid )
			{
				Out.TextValue = NJson::FromJsonString( Item->dump() );
			}
			break;
		case EParamType::String:
		case EParamType::BlackboardKey:
		case EParamType::Clip:
		case EParamType::AssetPath:
			bValid = Item->is_string();
			if ( bValid )
			{
				Out.TextValue = Item->get<FString>();
			}
			break;
		}

		if ( !bValid )
		{
			NAI::AddIssue( Issues, Definition.bRequired ? EAIIssueSeverity::Error : EAIIssueSeverity::Warning, ParamWhere, FString::Printf( "The value does not fit %s. The default is used.", ToString( Definition.Type ) ) );
		}
	}

	// 未知の名前
	if ( bHasObject )
	{
		for ( auto It = ParamsJson->begin(); It != ParamsJson->end(); ++It )
		{
			if ( Schema.FindIndex( NJson::FromJsonString( It.key() ) ) < 0 )
			{
				NAI::AddIssue( Issues, EAIIssueSeverity::Warning, Where + "/" + NJson::FromJsonString( It.key() ), "Unknown parameter. It was ignored." );
			}
		}
	}
	return Result;
}

NlohmannJson NAI::ParamsToJson( const FParamSchema& Schema, const FParamSet& Params )
{
	NlohmannJson Json = NlohmannJson::object();
	for ( int32 Index = 0; Index < Schema.GetNumParams(); ++Index )
	{
		const FParamDefinition& Definition = Schema.GetParams()[Index];
		const FParamValue& Value = Params.GetValues()[Index];
		switch ( Definition.Type )
		{
		case EParamType::Bool:
			if ( Value.bValue != Definition.Default.bValue || Definition.bRequired )
			{
				Json[*Definition.Name] = Value.bValue;
			}
			break;
		case EParamType::Int:
			if ( Value.IntValue != Definition.Default.IntValue || Definition.bRequired )
			{
				Json[*Definition.Name] = Value.IntValue;
			}
			break;
		case EParamType::Float:
			if ( Value.FloatValue != Definition.Default.FloatValue || Definition.bRequired )
			{
				Json[*Definition.Name] = NJson::Round4( Value.FloatValue );
			}
			break;
		case EParamType::Scalar:
			if ( !Value.TextValue.IsEmpty() )
			{
				Json[*Definition.Name] = NlohmannJson::parse( *Value.TextValue, nullptr, false );
			}
			break;
		case EParamType::Enum:
		case EParamType::String:
		case EParamType::BlackboardKey:
		case EParamType::Clip:
		case EParamType::AssetPath:
			if ( Value.TextValue != Definition.Default.TextValue || Definition.bRequired )
			{
				Json[*Definition.Name] = Value.TextValue;
			}
			break;
		}
	}
	return Json;
}
