// SPDX-License-Identifier: MIT
#include "DebugViews.h"

#include "Library/Core/Types/CString.h"
#include "Library/Math/MathUtils.h"

namespace
{
	int32 FindIndex( const TArray<FDebugView>& Views, const FString& Name )
	{
		for ( int32 Index = 0; Index < Views.Num(); ++Index )
		{
			if ( Views[Index].Name.Equals( Name, ESearchCase::IgnoreCase ) )
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	bool TryParseBool( const FString& Text, bool& OutValue )
	{
		if ( Text.Equals( "on", ESearchCase::IgnoreCase ) || Text.Equals( "true", ESearchCase::IgnoreCase ) || Text == "1" )
		{
			OutValue = true;
			return true;
		}
		if ( Text.Equals( "off", ESearchCase::IgnoreCase ) || Text.Equals( "false", ESearchCase::IgnoreCase ) || Text == "0" )
		{
			OutValue = false;
			return true;
		}
		return false;
	}

	// 文字列全体が 1 つの数のときだけ true（前後の空白は許す）
	bool TryParseNumber( const FString& Text, float64& OutValue )
	{
		const char* Start = *Text;
		char* End = nullptr;
		NCString::ClearRangeError();
		const float64 Value = NCString::ToFloat64( Start, &End );
		if ( End == Start || NCString::HasRangeError() )
		{
			return false;
		}
		while ( *End == ' ' || *End == '\t' )
		{
			++End;
		}
		if ( *End != '\0' || !NMath::IsFinite( Value ) )
		{
			return false;
		}
		OutValue = Value;
		return true;
	}
}

FDebugViewRegistry& FDebugViewRegistry::Global()
{
	static FDebugViewRegistry Instance;
	return Instance;
}

bool FDebugViewRegistry::Register( const FDebugView& View )
{
	if ( View.Name.IsEmpty() || View.pEnabled == nullptr )
	{
		return false;
	}
	for ( const FDebugViewOption& Option : View.Options )
	{
		if ( Option.Name == nullptr || Option.Name[0] == '\0' || Option.Value == nullptr )
		{
			return false;
		}
	}
	const int32 Existing = FindIndex( Views, View.Name );
	if ( Existing != INDEX_NONE )
	{
		Views[Existing] = View;
	}
	else
	{
		Views.Add( View );
	}
	return true;
}

bool FDebugViewRegistry::Unregister( const FString& Name )
{
	const int32 Existing = FindIndex( Views, Name );
	if ( Existing == INDEX_NONE )
	{
		return false;
	}
	Views.RemoveAt( Existing );
	return true;
}

const FDebugView* FDebugViewRegistry::Find( const FString& Name ) const
{
	const int32 Existing = FindIndex( Views, Name );
	return Existing != INDEX_NONE ? &Views[Existing] : nullptr;
}

bool FDebugViewRegistry::IsEnabled( const FString& Name ) const
{
	const FDebugView* View = Find( Name );
	return View != nullptr && *View->pEnabled;
}

bool FDebugViewRegistry::SetEnabled( const FString& Name, bool bEnabled ) const
{
	const FDebugView* View = Find( Name );
	if ( View == nullptr )
	{
		return false;
	}
	*View->pEnabled = bEnabled;
	return true;
}

bool FDebugViewRegistry::Toggle( const FString& Name, bool* OutNow ) const
{
	const FDebugView* View = Find( Name );
	if ( View == nullptr )
	{
		return false;
	}
	*View->pEnabled = !*View->pEnabled;
	if ( OutNow != nullptr )
	{
		*OutNow = *View->pEnabled;
	}
	return true;
}

const FDebugViewOption* FDebugViewRegistry::FindOption( const FDebugView& View, const FString& OptionName )
{
	for ( const FDebugViewOption& Option : View.Options )
	{
		if ( OptionName.Equals( Option.Name, ESearchCase::IgnoreCase ) )
		{
			return &Option;
		}
	}
	return nullptr;
}

FString FDebugViewRegistry::GetKeyLabel( EKey Key )
{
	if ( Key >= EKey::F1 && Key <= EKey::F12 )
	{
		return "F" + FString::FromInt( static_cast<int32>( Key ) - static_cast<int32>( EKey::F1 ) + 1 );
	}
	return "key";
}

FString FDebugViewRegistry::GetOptionText( const FDebugViewOption& Option )
{
	switch ( Option.Type )
	{
	case FDebugViewOption::EType::Bool:
		return *static_cast<const bool*>( Option.Value ) ? "on" : "off";
	case FDebugViewOption::EType::Int:
		return FString::FromInt( *static_cast<const int32*>( Option.Value ) );
	default:
		return FString::FromFloat( static_cast<float64>( *static_cast<const float32*>( Option.Value ) ), 3 );
	}
}

bool FDebugViewRegistry::SetOption( const FString& ViewName, const FString& OptionName, const FString& Text, FString* OutError ) const
{
	const auto Fail = [&]( const FString& Message )
	{
		if ( OutError != nullptr )
		{
			*OutError = Message;
		}
		return false;
	};
	const FDebugView* View = Find( ViewName );
	if ( View == nullptr )
	{
		return Fail( "no debug view named '" + ViewName + "'" );
	}
	const FDebugViewOption* Option = FindOption( *View, OptionName );
	if ( Option == nullptr )
	{
		return Fail( "the view '" + View->Name + "' has no option '" + OptionName + "'" );
	}
	if ( Option->Type == FDebugViewOption::EType::Bool )
	{
		bool bValue = false;
		if ( !TryParseBool( Text, bValue ) )
		{
			return Fail( "'" + Text + "' is not on / off (the option '" + OptionName + "' is a switch)" );
		}
		*static_cast<bool*>( Option->Value ) = bValue;
		return true;
	}
	float64 Number = 0.0;
	if ( !TryParseNumber( Text, Number ) )
	{
		return Fail( "'" + Text + "' is not a number (the option '" + OptionName + "')" );
	}
	const float64 Clamped = NMath::Clamp( Number, static_cast<float64>( Option->Min ), static_cast<float64>( Option->Max ) );
	if ( Option->Type == FDebugViewOption::EType::Int )
	{
		*static_cast<int32*>( Option->Value ) = static_cast<int32>( NMath::Round( Clamped ) );
	}
	else
	{
		*static_cast<float32*>( Option->Value ) = static_cast<float32>( Clamped );
	}
	return true;
}
