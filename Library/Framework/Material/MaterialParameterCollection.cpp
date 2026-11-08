// SPDX-License-Identifier: MIT
#include "MaterialParameterCollection.h"

#include "Library/Json/JsonObject.h"

int32 CMaterialParameterCollection::IndexOf( const FString& ParameterName ) const
{
	for ( int32 Index = 0; Index < Entries.Num(); ++Index )
	{
		if ( Entries[Index].Name == ParameterName )
		{
			return Index;
		}
	}
	return -1;
}

void CMaterialParameterCollection::Set( const FString& ParameterName, const FVector4& Value, bool bScalar )
{
	const int32 Index = IndexOf( ParameterName );
	if ( Index >= 0 )
	{
		if ( Entries[Index].Value == Value && Entries[Index].bScalar == bScalar )
		{
			return;
		}
		Entries[Index].Value = Value;
		Entries[Index].bScalar = bScalar;
	}
	else
	{
		FEntry Entry;
		Entry.Name = ParameterName;
		Entry.Value = Value;
		Entry.bScalar = bScalar;
		Entries.Add( Entry );
	}
	++Version;
}

void CMaterialParameterCollection::SetScalar( const FString& ParameterName, float32 Value )
{
	Set( ParameterName, FVector4( Value, 0.0f, 0.0f, 0.0f ), true );
}

void CMaterialParameterCollection::SetVector( const FString& ParameterName, const FVector4& Value )
{
	Set( ParameterName, Value, false );
}

bool CMaterialParameterCollection::TryGet( const FString& ParameterName, FVector4& OutValue ) const
{
	const int32 Index = IndexOf( ParameterName );
	if ( Index < 0 )
	{
		return false;
	}
	OutValue = Entries[Index].Value;
	return true;
}

float32 CMaterialParameterCollection::GetScalar( const FString& ParameterName, float32 Default ) const
{
	FVector4 Value;
	return TryGet( ParameterName, Value ) ? Value.x : Default;
}

bool CMaterialParameterCollection::Has( const FString& ParameterName ) const
{
	return IndexOf( ParameterName ) >= 0;
}

bool CMaterialParameterCollection::Remove( const FString& ParameterName )
{
	const int32 Index = IndexOf( ParameterName );
	if ( Index < 0 )
	{
		return false;
	}
	Entries.RemoveAt( Index );
	++Version;
	return true;
}

void CMaterialParameterCollection::Clear()
{
	if ( !Entries.IsEmpty() )
	{
		Entries.Empty();
		++Version;
	}
}

FString CMaterialParameterCollection::ToJson() const
{
	NlohmannJson Json = NlohmannJson::object();
	Json["Format"] = "gxmaterialparametercollection";
	Json["Version"] = 1;
	Json["Name"] = NJson::ToJsonString( Name );
	NlohmannJson Parameters = NlohmannJson::object();
	for ( int32 Index = 0; Index < Entries.Num(); ++Index )
	{
		const FEntry& Entry = Entries[Index];
		if ( Entry.bScalar )
		{
			Parameters[NJson::ToJsonString( Entry.Name )] = Entry.Value.x;
		}
		else
		{
			Parameters[NJson::ToJsonString( Entry.Name )] = NlohmannJson::array( { Entry.Value.x, Entry.Value.y, Entry.Value.z, Entry.Value.w } );
		}
	}
	Json["Parameters"] = Parameters;
	return NJson::FromJsonString( Json.dump( 2 ) );
}

bool CMaterialParameterCollection::FromJson( const FString& Text, CMaterialParameterCollection& OutParameterCollection, FString* OutError )
{
	auto Fail = [&]( const char* Message )
	{
		if ( OutError != nullptr )
		{
			*OutError = Message;
		}
		return false;
	};
	const NlohmannJson Json = NlohmannJson::parse( NJson::ToJsonString( Text ), nullptr, false );
	if ( Json.is_discarded() || !Json.is_object() )
	{
		return Fail( "the text is not a JSON object" );
	}
	const auto Format = Json.find( "Format" );
	if ( Format == Json.end() || !Format->is_string() || Format->get<NJson::FJsonString>() != "gxmaterialparametercollection" )
	{
		return Fail( "Format is not \"gxmaterialparametercollection\"" );
	}
	const auto Version = Json.find( "Version" );
	if ( Version == Json.end() || !Version->is_number_integer() || Version->get<int32>() > 1 )
	{
		return Fail( "the Version is missing or newer than this program understands (1)" );
	}
	CMaterialParameterCollection Result;
	const auto NameJson = Json.find( "Name" );
	if ( NameJson != Json.end() && NameJson->is_string() )
	{
		Result.Name = NJson::FromJsonString( NameJson->get<NJson::FJsonString>() );
	}
	const auto Parameters = Json.find( "Parameters" );
	if ( Parameters != Json.end() && Parameters->is_object() )
	{
		for ( auto Iterator = Parameters->begin(); Iterator != Parameters->end(); ++Iterator )
		{
			const FString ParameterName = NJson::FromJsonString( Iterator.key() );
			if ( Iterator.value().is_number() )
			{
				Result.SetScalar( ParameterName, Iterator.value().get<float32>() );
			}
			else if ( Iterator.value().is_array() )
			{
				float32 Components[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
				for ( size_t Index = 0; Index < Iterator.value().size() && Index < 4; ++Index )
				{
					if ( Iterator.value()[Index].is_number() )
					{
						Components[Index] = Iterator.value()[Index].get<float32>();
					}
				}
				Result.SetVector( ParameterName, FVector4( Components[0], Components[1], Components[2], Components[3] ) );
			}
			else
			{
				return Fail( "a parameter must be a number or an array of numbers" );
			}
		}
	}
	OutParameterCollection.Name = Result.Name;
	OutParameterCollection.Entries = Result.Entries;
	++OutParameterCollection.Version;
	return true;
}
