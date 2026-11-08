// SPDX-License-Identifier: MIT
#include "MaterialDefinition.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Json/JsonObject.h"

namespace
{
	NlohmannJson ToJson( const FVector4& Value, int32 NumComponents )
	{
		NlohmannJson Array = NlohmannJson::array();
		const float32 Components[4] = { Value.x, Value.y, Value.z, Value.w };
		for ( int32 Index = 0; Index < NumComponents && Index < 4; ++Index )
		{
			Array.push_back( Components[Index] );
		}
		return Array;
	}

	FVector4 ReadVector( const NlohmannJson& Json, const FVector4& Default )
	{
		if ( !Json.is_array() )
		{
			return Default;
		}
		float32 Components[4] = { Default.x, Default.y, Default.z, Default.w };
		for ( size_t Index = 0; Index < Json.size() && Index < 4; ++Index )
		{
			if ( Json[Index].is_number() )
			{
				Components[Index] = Json[Index].get<float32>();
			}
		}
		return FVector4( Components[0], Components[1], Components[2], Components[3] );
	}

	FString ReadString( const NlohmannJson& Json, const char* Key )
	{
		const auto Found = Json.find( Key );
		return ( Found != Json.end() && Found->is_string() ) ? NJson::FromJsonString( Found->get<NJson::FJsonString>() ) : FString();
	}

	bool IsValueNode( EMaterialNodeKind Kind )
	{
		return Kind == EMaterialNodeKind::Constant || Kind == EMaterialNodeKind::ScalarParameter || Kind == EMaterialNodeKind::VectorParameter || Kind == EMaterialNodeKind::ParameterCollectionScalar
			   || Kind == EMaterialNodeKind::ParameterCollectionVector || Kind == EMaterialNodeKind::Panner || Kind == EMaterialNodeKind::Tiling;
	}
}

// FMaterialGraph の、JSON の読み書き（private の次の番号に触れるため）
struct FMaterialJson
{
	static NlohmannJson Write( const FMaterialGraph& Graph )
	{
		NlohmannJson Nodes = NlohmannJson::array();
		for ( int32 Index = 0; Index < Graph.Nodes.Num(); ++Index )
		{
			const FMaterialNode& Node = Graph.Nodes[Index];
			NlohmannJson Json = NlohmannJson::object();
			Json["Id"] = Node.Id;
			Json["Kind"] = NJson::ToJsonString( FString( NMaterialNodes::GetInfo( Node.Kind ).Name ) );
			if ( !Node.Name.IsEmpty() )
			{
				Json["Name"] = NJson::ToJsonString( Node.Name );
			}
			if ( !Node.ParameterCollection.IsEmpty() )
			{
				Json["ParameterCollection"] = NJson::ToJsonString( Node.ParameterCollection );
			}
			if ( !Node.TexturePath.IsEmpty() )
			{
				Json["TexturePath"] = NJson::ToJsonString( Node.TexturePath );
			}
			if ( IsValueNode( Node.Kind ) )
			{
				const int32 Count = Node.Kind == EMaterialNodeKind::Panner ? 2 : ( Node.Kind == EMaterialNodeKind::Tiling ? 4 : Node.ValueComponents );
				Json["Value"] = ToJson( Node.Value, Count );
			}
			if ( Node.Kind == EMaterialNodeKind::Constant || Node.Kind == EMaterialNodeKind::VectorParameter || Node.Kind == EMaterialNodeKind::ParameterCollectionVector )
			{
				Json["Components"] = Node.ValueComponents;
			}
			if ( Node.bColor )
			{
				Json["Color"] = true;
			}
			Json["Position"] = NlohmannJson::array( { Node.Position.x, Node.Position.y } );
			NlohmannJson Inputs = NlohmannJson::array();
			for ( int32 Pin = 0; Pin < Node.Inputs.Num(); ++Pin )
			{
				Inputs.push_back( Node.Inputs[Pin] );
			}
			Json["Inputs"] = Inputs;
			if ( !Node.PinDefaults.IsEmpty() )
			{
				NlohmannJson Defaults = NlohmannJson::array();
				for ( int32 Pin = 0; Pin < Node.PinDefaults.Num(); ++Pin )
				{
					Defaults.push_back( Node.PinDefaults[Pin] );
				}
				Json["PinDefaults"] = Defaults;
			}
			Nodes.push_back( Json );
		}
		return Nodes;
	}

	static bool Read( const NlohmannJson& Nodes, const NlohmannJson& Outputs, FMaterialGraph& Graph, FString* OutError )
	{
		Graph = FMaterialGraph();
		int32 MaxId = 0;
		if ( Nodes.is_array() )
		{
			for ( size_t Index = 0; Index < Nodes.size(); ++Index )
			{
				const NlohmannJson& Json = Nodes[Index];
				if ( !Json.is_object() )
				{
					continue;
				}
				FMaterialNode Node;
				const auto Id = Json.find( "Id" );
				if ( Id == Json.end() || !Id->is_number_integer() )
				{
					if ( OutError != nullptr )
					{
						*OutError = FString::Printf( "node %d has no integer Id", static_cast<int32>( Index ) );
					}
					return false;
				}
				Node.Id = Id->get<int32>();
				const FString KindName = ReadString( Json, "Kind" );
				if ( !NMaterialNodes::FindKind( KindName, Node.Kind ) )
				{
					if ( OutError != nullptr )
					{
						*OutError = FString::Printf( "node %d: the kind '%s' is not known", Node.Id, *KindName );
					}
					return false;
				}
				for ( int32 Existing = 0; Existing < Graph.Nodes.Num(); ++Existing )
				{
					if ( Graph.Nodes[Existing].Id == Node.Id )
					{
						if ( OutError != nullptr )
						{
							*OutError = FString::Printf( "the node Id %d appears twice", Node.Id );
						}
						return false;
					}
				}
				Node.Name = ReadString( Json, "Name" );
				Node.ParameterCollection = ReadString( Json, "ParameterCollection" );
				Node.TexturePath = ReadString( Json, "TexturePath" );
				Node.Value = ReadVector( Json.contains( "Value" ) ? Json["Value"] : NlohmannJson(), FVector4( 0.0f, 0.0f, 0.0f, 0.0f ) );
				const auto Components = Json.find( "Components" );
				Node.ValueComponents = ( Components != Json.end() && Components->is_number_integer() ) ? NMath::Clamp( Components->get<int32>(), 1, 4 ) : ( Node.Kind == EMaterialNodeKind::VectorParameter || Node.Kind == EMaterialNodeKind::ParameterCollectionVector ? 4 : 1 );
				const auto Color = Json.find( "Color" );
				Node.bColor = Color != Json.end() && Color->is_boolean() && Color->get<bool>();
				const FVector4 Position = ReadVector( Json.contains( "Position" ) ? Json["Position"] : NlohmannJson(), FVector4( 0.0f, 0.0f, 0.0f, 0.0f ) );
				Node.Position = FVector2( Position.x, Position.y );
				const int32 NumPins = NMaterialNodes::GetInfo( Node.Kind ).NumPins;
				for ( int32 Pin = 0; Pin < NumPins; ++Pin )
				{
					Node.Inputs.Add( -1 );
				}
				if ( Json.contains( "Inputs" ) && Json["Inputs"].is_array() )
				{
					for ( size_t Pin = 0; Pin < Json["Inputs"].size() && static_cast<int32>( Pin ) < NumPins; ++Pin )
					{
						Node.Inputs[static_cast<int32>( Pin )] = Json["Inputs"][Pin].is_number_integer() ? Json["Inputs"][Pin].get<int32>() : -1;
					}
				}
				if ( Json.contains( "PinDefaults" ) && Json["PinDefaults"].is_array() )
				{
					for ( size_t Pin = 0; Pin < Json["PinDefaults"].size(); ++Pin )
					{
						Node.PinDefaults.Add( Json["PinDefaults"][Pin].is_number() ? Json["PinDefaults"][Pin].get<float32>() : 0.0f );
					}
				}
				MaxId = NMath::Max( MaxId, Node.Id );
				Graph.Nodes.Add( Node );
			}
		}
		Graph.NextId = MaxId + 1;
		if ( Outputs.is_object() )
		{
			for ( auto Iterator = Outputs.begin(); Iterator != Outputs.end(); ++Iterator )
			{
				if ( Iterator.value().is_number_integer() )
				{
					FMaterialOutputLink Link;
					Link.Pin = NJson::FromJsonString( Iterator.key() );
					Link.SourceNode = Iterator.value().get<int32>();
					Graph.Outputs.Add( Link );
				}
			}
		}
		return true;
	}
};

FString FMaterialDefinition::ToJson() const
{
	NlohmannJson Json = NlohmannJson::object();
	Json["Format"] = "gxmaterial";
	Json["Version"] = 1;
	Json["Name"] = NJson::ToJsonString( Name );
	Json["Domain"] = NMaterial::ToString( Domain );
	if ( Domain == EMaterialDomain::Surface )
	{
		Json["Shading"] = NMaterial::ToString( Shading );
	}
	if ( !CustomHlsl.IsEmpty() )
	{
		Json["CustomHlsl"] = NJson::ToJsonString( CustomHlsl );
		NlohmannJson Declarations = NlohmannJson::array();
		for ( int32 Index = 0; Index < Parameters.Num(); ++Index )
		{
			const FMaterialParameterDecl& Decl = Parameters[Index];
			NlohmannJson Entry = NlohmannJson::object();
			Entry["Name"] = NJson::ToJsonString( Decl.Name );
			Entry["Type"] = NMaterial::ToString( Decl.Type );
			if ( Decl.Type == EMaterialValueType::Texture )
			{
				Entry["Path"] = NJson::ToJsonString( Decl.Path );
			}
			else
			{
				Entry["Value"] = ::ToJson( Decl.Value, NMaterial::NumComponents( Decl.Type ) );
				if ( Decl.bColor )
				{
					Entry["Color"] = true;
				}
			}
			Declarations.push_back( Entry );
		}
		Json["Parameters"] = Declarations;
	}
	else
	{
		Json["Nodes"] = FMaterialJson::Write( Graph );
		NlohmannJson Outputs = NlohmannJson::object();
		for ( int32 Index = 0; Index < Graph.Outputs.Num(); ++Index )
		{
			Outputs[NJson::ToJsonString( Graph.Outputs[Index].Pin )] = Graph.Outputs[Index].SourceNode;
		}
		Json["Outputs"] = Outputs;
	}
	return NJson::FromJsonString( Json.dump( 2 ) );
}

bool FMaterialDefinition::FromJson( const FString& Text, FMaterialDefinition& OutDefinition, FString* OutError )
{
	auto Fail = [&]( const FString& Message )
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
	if ( ReadString( Json, "Format" ) != "gxmaterial" )
	{
		return Fail( "Format is not \"gxmaterial\"" );
	}
	const auto Version = Json.find( "Version" );
	if ( Version == Json.end() || !Version->is_number_integer() || Version->get<int32>() > 1 )
	{
		return Fail( "the Version is missing or newer than this program understands (1)" );
	}
	FMaterialDefinition Result;
	Result.Name = ReadString( Json, "Name" );
	if ( !NMaterial::FromString( ReadString( Json, "Domain" ), Result.Domain ) )
	{
		return Fail( FString::Printf( "the Domain '%s' is not known (UI, Surface, Particle, PostProcess)", *ReadString( Json, "Domain" ) ) );
	}
	if ( Json.contains( "Shading" ) && !NMaterial::FromString( ReadString( Json, "Shading" ), Result.Shading ) )
	{
		return Fail( FString::Printf( "the Shading '%s' is not known (Unlit, DefaultLit, Substrate)", *ReadString( Json, "Shading" ) ) );
	}
	Result.CustomHlsl = ReadString( Json, "CustomHlsl" );
	if ( Json.contains( "Parameters" ) && Json["Parameters"].is_array() )
	{
		for ( size_t Index = 0; Index < Json["Parameters"].size(); ++Index )
		{
			const NlohmannJson& Entry = Json["Parameters"][Index];
			FMaterialParameterDecl Decl;
			Decl.Name = ReadString( Entry, "Name" );
			if ( Decl.Name.IsEmpty() || !NMaterial::FromString( ReadString( Entry, "Type" ), Decl.Type ) || Decl.Type == EMaterialValueType::Slab )
			{
				return Fail( FString::Printf( "parameter %d has no Name or a Type that is not known (Float, Float2, Float3, Float4, Texture)", static_cast<int32>( Index ) ) );
			}
			Decl.Path = ReadString( Entry, "Path" );
			Decl.Value = ReadVector( Entry.contains( "Value" ) ? Entry["Value"] : NlohmannJson(), FVector4( 0.0f, 0.0f, 0.0f, 0.0f ) );
			Decl.bColor = Entry.contains( "Color" ) && Entry["Color"].is_boolean() && Entry["Color"].get<bool>();
			Result.Parameters.Add( Decl );
		}
	}
	if ( Result.CustomHlsl.IsEmpty() )
	{
		if ( !FMaterialJson::Read( Json.contains( "Nodes" ) ? Json["Nodes"] : NlohmannJson(), Json.contains( "Outputs" ) ? Json["Outputs"] : NlohmannJson(), Result.Graph, OutError ) )
		{
			return false;
		}
	}
	OutDefinition = Result;
	return true;
}
