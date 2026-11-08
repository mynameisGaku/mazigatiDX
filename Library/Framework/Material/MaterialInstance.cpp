// SPDX-License-Identifier: MIT
#include "MaterialInstance.h"

#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Json/JsonObject.h"

FString FMaterialInstanceDefinition::ToJson() const
{
	NlohmannJson Json = NlohmannJson::object();
	Json["Format"] = "gxmaterialinstance";
	Json["Version"] = 1;
	Json["Parent"] = NJson::ToJsonString( ParentPath );
	NlohmannJson Values = NlohmannJson::object();
	for ( int32 Index = 0; Index < Overrides.Num(); ++Index )
	{
		const FMaterialParameterDecl& Decl = Overrides[Index];
		const FString Key = Decl.Name;
		if ( Decl.Type == EMaterialValueType::Texture )
		{
			Values[NJson::ToJsonString( Key )] = NJson::ToJsonString( Decl.Path );
		}
		else if ( Decl.Type == EMaterialValueType::Float )
		{
			Values[NJson::ToJsonString( Key )] = Decl.Value.x;
		}
		else
		{
			const float32 Components[4] = { Decl.Value.x, Decl.Value.y, Decl.Value.z, Decl.Value.w };
			NlohmannJson Array = NlohmannJson::array();
			for ( int32 Component = 0; Component < NMaterial::NumComponents( Decl.Type ); ++Component )
			{
				Array.push_back( Components[Component] );
			}
			Values[NJson::ToJsonString( Key )] = Array;
		}
	}
	Json["Overrides"] = Values;
	return NJson::FromJsonString( Json.dump( 2 ) );
}

bool FMaterialInstanceDefinition::FromJson( const FString& Text, FMaterialInstanceDefinition& OutDefinition, FString* OutError )
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
	if ( Format == Json.end() || !Format->is_string() || Format->get<NJson::FJsonString>() != "gxmaterialinstance" )
	{
		return Fail( "Format is not \"gxmaterialinstance\"" );
	}
	const auto Version = Json.find( "Version" );
	if ( Version == Json.end() || !Version->is_number_integer() || Version->get<int32>() > 1 )
	{
		return Fail( "the Version is missing or newer than this program understands (1)" );
	}
	const auto Parent = Json.find( "Parent" );
	if ( Parent == Json.end() || !Parent->is_string() || Parent->get<NJson::FJsonString>().empty() )
	{
		return Fail( "Parent (the path of the parent material) is missing" );
	}
	FMaterialInstanceDefinition Result;
	Result.ParentPath = NJson::FromJsonString( Parent->get<NJson::FJsonString>() );
	const auto Overrides = Json.find( "Overrides" );
	if ( Overrides != Json.end() && Overrides->is_object() )
	{
		for ( auto Iterator = Overrides->begin(); Iterator != Overrides->end(); ++Iterator )
		{
			FMaterialParameterDecl Decl;
			Decl.Name = NJson::FromJsonString( Iterator.key() );
			const NlohmannJson& Value = Iterator.value();
			if ( Value.is_number() )
			{
				Decl.Type = EMaterialValueType::Float;
				Decl.Value = FVector4( Value.get<float32>(), 0.0f, 0.0f, 0.0f );
			}
			else if ( Value.is_string() )
			{
				Decl.Type = EMaterialValueType::Texture;
				Decl.Path = NJson::FromJsonString( Value.get<NJson::FJsonString>() );
			}
			else if ( Value.is_array() && Value.size() >= 1 && Value.size() <= 4 )
			{
				Decl.Type = NMaterial::TypeFromComponents( static_cast<int32>( Value.size() ) );
				float32 Components[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				for ( size_t Index = 0; Index < Value.size(); ++Index )
				{
					if ( !Value[Index].is_number() )
					{
						return Fail( "an override array must contain only numbers" );
					}
					Components[Index] = Value[Index].get<float32>();
				}
				Decl.Value = FVector4( Components[0], Components[1], Components[2], Components[3] );
			}
			else
			{
				return Fail( "an override must be a number, an array of 1 to 4 numbers, or a texture path (string)" );
			}
			Result.Overrides.Add( Decl );
		}
	}
	OutDefinition = Result;
	return true;
}

void FMaterialInstanceDefinition::Apply( CMaterial& Instance, CAssetSubsystem* Assets, TArray<FString>* OutMissing ) const
{
	for ( int32 Index = 0; Index < Overrides.Num(); ++Index )
	{
		const FMaterialParameterDecl& Decl = Overrides[Index];
		bool bOk = false;
		if ( Decl.Type == EMaterialValueType::Texture )
		{
			TSharedPtr<IImage> Image;
			if ( Assets != nullptr && !Decl.Path.IsEmpty() )
			{
				Image = Assets->LoadTexture( Decl.Path );
			}
			bOk = Instance.SetTexture( Decl.Name, Image );
		}
		else if ( Decl.Type == EMaterialValueType::Float )
		{
			bOk = Instance.SetScalar( Decl.Name, Decl.Value.x );
		}
		else
		{
			bOk = Instance.SetVector( Decl.Name, Decl.Value );
		}
		if ( !bOk && OutMissing != nullptr )
		{
			OutMissing->Add( Decl.Name );
		}
	}
}
