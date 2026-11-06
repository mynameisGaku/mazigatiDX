// SPDX-License-Identifier: MIT
#include "BlackboardEdit.h"

#include "Library/Framework/Asset/Definition/DefinitionEditing.h"
#include "Library/Math/MathUtils.h"

namespace
{
	TArray<FString> KeyNames( const FBlackboardDefinition& Definition, int32 ExceptIndex )
	{
		TArray<FString> Names;
		for ( int32 Index = 0; Index < Definition.Keys.Num(); ++Index )
		{
			if ( Index != ExceptIndex )
			{
				Names.Add( Definition.Keys[Index].Name );
			}
		}
		return Names;
	}

	bool IsValidKey( const FBlackboardDefinition& Definition, int32 KeyIndex )
	{
		return KeyIndex >= 0 && KeyIndex < Definition.Keys.Num();
	}

	bool IsEnumKey( const FBlackboardDefinition& Definition, int32 KeyIndex )
	{
		return IsValidKey( Definition, KeyIndex ) && Definition.Keys[KeyIndex].Type == EBlackboardKeyType::Enum;
	}
}

int32 NBlackboardEdit::AddKey( TBlackboardDocument& Document, const FString& Name, EBlackboardKeyType Type )
{
	FBlackboardKeyDefinition Key;
	Key.Name = NDefinitionEdit::MakeUniqueName( KeyNames( Document.Get(), INDEX_NONE ), Name, "Key" );
	Key.Type = Type;
	FBlackboardDefinition& Definition = Document.BeginEdit();
	return Definition.Keys.Add( Key );
}

void NBlackboardEdit::RemoveKey( TBlackboardDocument& Document, int32 KeyIndex )
{
	if ( !IsValidKey( Document.Get(), KeyIndex ) )
	{
		return;
	}
	Document.BeginEdit().Keys.RemoveAt( KeyIndex );
}

FString NBlackboardEdit::RenameKey( TBlackboardDocument& Document, int32 KeyIndex, const FString& Name )
{
	if ( !IsValidKey( Document.Get(), KeyIndex ) )
	{
		return FString();
	}
	const FString Unique = NDefinitionEdit::MakeUniqueName( KeyNames( Document.Get(), KeyIndex ), Name, "Key" );
	if ( Unique != Document.Get().Keys[KeyIndex].Name )
	{
		Document.BeginEdit().Keys[KeyIndex].Name = Unique;
	}
	return Unique;
}

void NBlackboardEdit::MoveKey( TBlackboardDocument& Document, int32 From, int32 To )
{
	const int32 Num = Document.Get().Keys.Num();
	if ( From < 0 || From >= Num || To < 0 || To >= Num || From == To )
	{
		return;
	}
	TArray<FBlackboardKeyDefinition>& Keys = Document.BeginEdit().Keys;
	FBlackboardKeyDefinition Moved = Keys[From];
	Keys.RemoveAt( From );
	Keys.Insert( Moved, To );
}

void NBlackboardEdit::SetKeyType( TBlackboardDocument& Document, int32 KeyIndex, EBlackboardKeyType Type )
{
	if ( !IsValidKey( Document.Get(), KeyIndex ) || Document.Get().Keys[KeyIndex].Type == Type )
	{
		return;
	}
	FBlackboardKeyDefinition& Key = Document.BeginEdit().Keys[KeyIndex];
	Key.Type = Type;
	Key.Default = FBlackboardValue();
	if ( Type != EBlackboardKeyType::Enum )
	{
		Key.EnumValues.Reset();
	}
	if ( Type != EBlackboardKeyType::Bool )
	{
		Key.bTrigger = false;
	}
}

void NBlackboardEdit::SetKeyTrigger( TBlackboardDocument& Document, int32 KeyIndex, bool bTrigger )
{
	if ( !IsValidKey( Document.Get(), KeyIndex ) || Document.Get().Keys[KeyIndex].Type != EBlackboardKeyType::Bool || Document.Get().Keys[KeyIndex].bTrigger == bTrigger )
	{
		return;
	}
	Document.BeginEdit().Keys[KeyIndex].bTrigger = bTrigger;
}

void NBlackboardEdit::SetKeyDefault( TBlackboardDocument& Document, int32 KeyIndex, const FBlackboardValue& Value, const char* CoalesceKey )
{
	if ( !IsValidKey( Document.Get(), KeyIndex ) )
	{
		return;
	}
	const FBlackboardKeyDefinition& Current = Document.Get().Keys[KeyIndex];
	if ( Current.Default.IsEqual( Current.Type, Value ) )
	{
		return;
	}
	Document.BeginEdit( CoalesceKey ).Keys[KeyIndex].Default = Value;
}

int32 NBlackboardEdit::AddEnumValue( TBlackboardDocument& Document, int32 KeyIndex, const FString& Name )
{
	if ( !IsEnumKey( Document.Get(), KeyIndex ) )
	{
		return INDEX_NONE;
	}
	const FString Unique = NDefinitionEdit::MakeUniqueName( Document.Get().Keys[KeyIndex].EnumValues, Name, "Value" );
	return Document.BeginEdit().Keys[KeyIndex].EnumValues.Add( Unique );
}

void NBlackboardEdit::RemoveEnumValue( TBlackboardDocument& Document, int32 KeyIndex, int32 ValueIndex )
{
	if ( !IsEnumKey( Document.Get(), KeyIndex ) || ValueIndex < 0 || ValueIndex >= Document.Get().Keys[KeyIndex].EnumValues.Num() )
	{
		return;
	}
	FBlackboardKeyDefinition& Key = Document.BeginEdit().Keys[KeyIndex];
	Key.EnumValues.RemoveAt( ValueIndex );
	if ( Key.Default.Integer == ValueIndex )
	{
		Key.Default.Integer = 0;
	}
	else if ( Key.Default.Integer > ValueIndex )
	{
		--Key.Default.Integer;
	}
}

FString NBlackboardEdit::RenameEnumValue( TBlackboardDocument& Document, int32 KeyIndex, int32 ValueIndex, const FString& Name )
{
	if ( !IsEnumKey( Document.Get(), KeyIndex ) || ValueIndex < 0 || ValueIndex >= Document.Get().Keys[KeyIndex].EnumValues.Num() )
	{
		return FString();
	}
	TArray<FString> Others;
	const TArray<FString>& Values = Document.Get().Keys[KeyIndex].EnumValues;
	for ( int32 Index = 0; Index < Values.Num(); ++Index )
	{
		if ( Index != ValueIndex )
		{
			Others.Add( Values[Index] );
		}
	}
	const FString Unique = NDefinitionEdit::MakeUniqueName( Others, Name, "Value" );
	if ( Unique != Values[ValueIndex] )
	{
		Document.BeginEdit().Keys[KeyIndex].EnumValues[ValueIndex] = Unique;
	}
	return Unique;
}
