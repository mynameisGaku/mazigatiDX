// SPDX-License-Identifier: MIT
#include "Blackboard.h"

#include "Library/Core/Log/Log.h"
#include "Library/Framework/AI/AILog.h"
#include "Library/Math/MathUtils.h"

namespace
{
	const FString EmptyText;
}

CBlackboard::CBlackboard( TSharedPtr<const FBlackboardDefinition> InDefinition )
	: Definition( InDefinition.IsValid() ? InDefinition : MakeShared<const FBlackboardDefinition>() )
{
	Values.SetNum( Definition->Keys.Num() );
	Revisions.SetNum( Definition->Keys.Num() );
	ResetToDefaults();
	ChangeCounter = 0;
	for ( uint32& Revision : Revisions )
	{
		Revision = 0;
	}
}

FBlackboardKey CBlackboard::FindKey( const FString& Name ) const
{
	return FBlackboardKey( Definition->FindKeyIndex( Name ) );
}

bool CBlackboard::IsValidKey( FBlackboardKey Key ) const
{
	return Key.Index >= 0 && Key.Index < Definition->Keys.Num();
}

EBlackboardKeyType CBlackboard::GetKeyType( FBlackboardKey Key ) const
{
	return IsValidKey( Key ) ? Definition->Keys[Key.Index].Type : EBlackboardKeyType::Bool;
}

const FString& CBlackboard::GetKeyName( FBlackboardKey Key ) const
{
	return IsValidKey( Key ) ? Definition->Keys[Key.Index].Name : EmptyText;
}

void CBlackboard::WarnOnce( int32 WarnKey, const FString& Message ) const
{
	if ( Warned.Contains( WarnKey ) )
	{
		return;
	}
	Warned.Add( WarnKey );
	LOG( LogAI, Warning, "Blackboard: %s", *Message );
}

bool CBlackboard::CheckKey( FBlackboardKey Key, EBlackboardKeyType Expected, const char* Operation ) const
{
	if ( !IsValidKey( Key ) )
	{
		WarnOnce( -1, FString::Printf( "%s: the key (index %d) does not exist.", Operation, Key.Index ) );
		return false;
	}
	const FBlackboardKeyDefinition& KeyDefinition = Definition->Keys[Key.Index];
	if ( KeyDefinition.Type != Expected )
	{
		WarnOnce( Key.Index, FString::Printf( "%s: the key '%s' is %s, not %s.", Operation, *KeyDefinition.Name, NBlackboard::ToString( KeyDefinition.Type ), NBlackboard::ToString( Expected ) ) );
		return false;
	}
	return true;
}

bool CBlackboard::GetBool( FBlackboardKey Key ) const
{
	return CheckKey( Key, EBlackboardKeyType::Bool, "GetBool" ) && Values[Key.Index].Integer != 0;
}

int32 CBlackboard::GetInt( FBlackboardKey Key ) const
{
	return CheckKey( Key, EBlackboardKeyType::Int, "GetInt" ) ? static_cast<int32>( Values[Key.Index].Integer ) : 0;
}

float32 CBlackboard::GetFloat( FBlackboardKey Key ) const
{
	return CheckKey( Key, EBlackboardKeyType::Float, "GetFloat" ) ? Values[Key.Index].Real : 0.0f;
}

FVector3 CBlackboard::GetVector( FBlackboardKey Key ) const
{
	return CheckKey( Key, EBlackboardKeyType::Vector3, "GetVector" ) ? Values[Key.Index].Vector : FVector3::Zero;
}

const FString& CBlackboard::GetName( FBlackboardKey Key ) const
{
	return CheckKey( Key, EBlackboardKeyType::Name, "GetName" ) ? Values[Key.Index].Text : EmptyText;
}

uint64 CBlackboard::GetObjectId( FBlackboardKey Key ) const
{
	return CheckKey( Key, EBlackboardKeyType::Object, "GetObjectId" ) ? Values[Key.Index].ObjectId : 0;
}

int32 CBlackboard::GetEnum( FBlackboardKey Key ) const
{
	return CheckKey( Key, EBlackboardKeyType::Enum, "GetEnum" ) ? static_cast<int32>( Values[Key.Index].Integer ) : 0;
}

FString CBlackboard::GetEnumName( FBlackboardKey Key ) const
{
	if ( !CheckKey( Key, EBlackboardKeyType::Enum, "GetEnumName" ) )
	{
		return FString();
	}
	const int32 Index = static_cast<int32>( Values[Key.Index].Integer );
	const TArray<FString>& Options = Definition->Keys[Key.Index].EnumValues;
	return Index >= 0 && Index < Options.Num() ? Options[Index] : FString();
}

bool CBlackboard::Store( int32 Index, const FBlackboardValue& Value )
{
	const EBlackboardKeyType Type = Definition->Keys[Index].Type;
	if ( Values[Index].IsEqual( Type, Value ) )
	{
		return false;
	}
	Values[Index] = Value;
	++Revisions[Index];
	++ChangeCounter;
	return true;
}

bool CBlackboard::SetBool( FBlackboardKey Key, bool bValue )
{
	return CheckKey( Key, EBlackboardKeyType::Bool, "SetBool" ) && Store( Key.Index, FBlackboardValue::MakeBool( bValue ) );
}

bool CBlackboard::SetInt( FBlackboardKey Key, int32 Value )
{
	return CheckKey( Key, EBlackboardKeyType::Int, "SetInt" ) && Store( Key.Index, FBlackboardValue::MakeInt( Value ) );
}

bool CBlackboard::SetFloat( FBlackboardKey Key, float32 Value )
{
	if ( !CheckKey( Key, EBlackboardKeyType::Float, "SetFloat" ) )
	{
		return false;
	}
	if ( !NMath::IsFinite( Value ) )
	{
		WarnOnce( Key.Index, FString::Printf( "SetFloat: a non-finite value was written to the key '%s'. It was ignored.", *Definition->Keys[Key.Index].Name ) );
		return false;
	}
	return Store( Key.Index, FBlackboardValue::MakeFloat( Value ) );
}

bool CBlackboard::SetVector( FBlackboardKey Key, const FVector3& Value )
{
	if ( !CheckKey( Key, EBlackboardKeyType::Vector3, "SetVector" ) )
	{
		return false;
	}
	if ( !NMath::IsFinite( Value.x ) || !NMath::IsFinite( Value.y ) || !NMath::IsFinite( Value.z ) )
	{
		WarnOnce( Key.Index, FString::Printf( "SetVector: a non-finite value was written to the key '%s'. It was ignored.", *Definition->Keys[Key.Index].Name ) );
		return false;
	}
	return Store( Key.Index, FBlackboardValue::MakeVector( Value ) );
}

bool CBlackboard::SetName( FBlackboardKey Key, const FString& Value )
{
	return CheckKey( Key, EBlackboardKeyType::Name, "SetName" ) && Store( Key.Index, FBlackboardValue::MakeName( Value ) );
}

bool CBlackboard::SetObjectId( FBlackboardKey Key, uint64 Id )
{
	return CheckKey( Key, EBlackboardKeyType::Object, "SetObjectId" ) && Store( Key.Index, FBlackboardValue::MakeObject( Id ) );
}

bool CBlackboard::SetEnum( FBlackboardKey Key, int32 Index )
{
	if ( !CheckKey( Key, EBlackboardKeyType::Enum, "SetEnum" ) )
	{
		return false;
	}
	if ( Index < 0 || Index >= Definition->Keys[Key.Index].EnumValues.Num() )
	{
		WarnOnce( Key.Index, FString::Printf( "SetEnum: the choice %d does not exist in the key '%s'. It was ignored.", Index, *Definition->Keys[Key.Index].Name ) );
		return false;
	}
	return Store( Key.Index, FBlackboardValue::MakeEnum( Index ) );
}

bool CBlackboard::SetEnumByName( FBlackboardKey Key, const FString& Name )
{
	if ( !CheckKey( Key, EBlackboardKeyType::Enum, "SetEnumByName" ) )
	{
		return false;
	}
	const TArray<FString>& Options = Definition->Keys[Key.Index].EnumValues;
	for ( int32 Index = 0; Index < Options.Num(); ++Index )
	{
		if ( Options[Index] == Name )
		{
			return Store( Key.Index, FBlackboardValue::MakeEnum( Index ) );
		}
	}
	WarnOnce( Key.Index, FString::Printf( "SetEnumByName: the choice '%s' does not exist in the key '%s'. It was ignored.", *Name, *Definition->Keys[Key.Index].Name ) );
	return false;
}

bool CBlackboard::GetValue( FBlackboardKey Key, FBlackboardValue& OutValue ) const
{
	if ( !IsValidKey( Key ) )
	{
		return false;
	}
	OutValue = Values[Key.Index];
	return true;
}

bool CBlackboard::SetValue( FBlackboardKey Key, const FBlackboardValue& Value )
{
	if ( !IsValidKey( Key ) )
	{
		WarnOnce( -1, FString::Printf( "SetValue: the key (index %d) does not exist.", Key.Index ) );
		return false;
	}
	switch ( Definition->Keys[Key.Index].Type )
	{
	case EBlackboardKeyType::Bool:
		return SetBool( Key, Value.Integer != 0 );
	case EBlackboardKeyType::Int:
		return SetInt( Key, static_cast<int32>( Value.Integer ) );
	case EBlackboardKeyType::Float:
		return SetFloat( Key, Value.Real );
	case EBlackboardKeyType::Vector3:
		return SetVector( Key, Value.Vector );
	case EBlackboardKeyType::Name:
		return SetName( Key, Value.Text );
	case EBlackboardKeyType::Object:
		return SetObjectId( Key, Value.ObjectId );
	case EBlackboardKeyType::Enum:
		return SetEnum( Key, static_cast<int32>( Value.Integer ) );
	}
	return false;
}

uint32 CBlackboard::GetRevision( FBlackboardKey Key ) const
{
	return IsValidKey( Key ) ? Revisions[Key.Index] : 0;
}

void CBlackboard::ResetToDefaults()
{
	for ( int32 Index = 0; Index < Values.Num(); ++Index )
	{
		Store( Index, Definition->Keys[Index].Default );
	}
}

void CBlackboard::ClearTriggers()
{
	for ( int32 Index = 0; Index < Values.Num(); ++Index )
	{
		if ( Definition->Keys[Index].bTrigger && Values[Index].Integer != 0 )
		{
			Store( Index, FBlackboardValue::MakeBool( false ) );
		}
	}
}

int32 CBlackboard::CopyMatchingValuesFrom( const CBlackboard& Other )
{
	int32 NumCopied = 0;
	for ( int32 Index = 0; Index < Definition->Keys.Num(); ++Index )
	{
		const FBlackboardKeyDefinition& Key = Definition->Keys[Index];
		const int32 OtherIndex = Other.Definition->FindKeyIndex( Key.Name );
		if ( OtherIndex < 0 || Other.Definition->Keys[OtherIndex].Type != Key.Type )
		{
			continue;
		}
		FBlackboardValue Value = Other.Values[OtherIndex];
		// Enum は、選択肢の名前が同じものだけ引き継ぐ（番号がずれることがある）
		if ( Key.Type == EBlackboardKeyType::Enum )
		{
			const TArray<FString>& OtherOptions = Other.Definition->Keys[OtherIndex].EnumValues;
			const int64 OtherChoice = Value.Integer;
			if ( OtherChoice < 0 || OtherChoice >= OtherOptions.Num() )
			{
				continue;
			}
			int32 Mapped = -1;
			for ( int32 Choice = 0; Choice < Key.EnumValues.Num(); ++Choice )
			{
				if ( Key.EnumValues[Choice] == OtherOptions[static_cast<int32>( OtherChoice )] )
				{
					Mapped = Choice;
					break;
				}
			}
			if ( Mapped < 0 )
			{
				continue;
			}
			Value = FBlackboardValue::MakeEnum( Mapped );
		}
		Store( Index, Value );
		++NumCopied;
	}
	return NumCopied;
}
