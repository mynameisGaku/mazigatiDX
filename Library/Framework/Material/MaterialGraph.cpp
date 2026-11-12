// SPDX-License-Identifier: MIT
#include "MaterialGraph.h"

#include "Library/Core/Types/Numeric.h"

namespace
{
	constexpr uint32 DomainUI = 1u << static_cast<uint32>( EMaterialDomain::UI );
	constexpr uint32 DomainSurface = 1u << static_cast<uint32>( EMaterialDomain::Surface );
	constexpr uint32 DomainParticle = 1u << static_cast<uint32>( EMaterialDomain::Particle );
	constexpr uint32 DomainPost = 1u << static_cast<uint32>( EMaterialDomain::PostProcess );
	constexpr uint32 DomainAll = DomainUI | DomainSurface | DomainParticle | DomainPost;

	FMaterialPinInfo Pin( const char* Name, float32 Default = 0.0f )
	{
		FMaterialPinInfo Info;
		Info.Name = Name;
		Info.Default = Default;
		return Info;
	}

	FMaterialPinInfo UvPin( const char* Name = "UV" )
	{
		FMaterialPinInfo Info;
		Info.Name = Name;
		Info.Expect = EMaterialPinExpect::Float2;
		Info.bUvDefault = true;
		return Info;
	}

	FMaterialPinInfo Typed( const char* Name, EMaterialPinExpect Expect, float32 Default = 0.0f )
	{
		FMaterialPinInfo Info;
		Info.Name = Name;
		Info.Expect = Expect;
		Info.Default = Default;
		return Info;
	}

	FMaterialPinInfo Optional( const char* Name )
	{
		FMaterialPinInfo Info;
		Info.Name = Name;
		Info.bOptional = true;
		return Info;
	}

	FMaterialNodeInfo Make( EMaterialNodeKind Kind, const char* Name, const char* Category, uint32 DomainMask, const TArray<FMaterialPinInfo>& Pins = TArray<FMaterialPinInfo>() )
	{
		FMaterialNodeInfo Info;
		Info.Kind = Kind;
		Info.Name = Name;
		Info.Category = Category;
		Info.DomainMask = DomainMask;
		const int32 Count = NMath::Min( Pins.Num(), 11 );
		for ( int32 Index = 0; Index < Count; ++Index )
		{
			Info.Pins[Index] = Pins[Index];
		}
		Info.NumPins = Count;
		return Info;
	}

	const FMaterialNodeInfo* BuildTable()
	{
		static FMaterialNodeInfo Table[static_cast<int32>( EMaterialNodeKind::Count )];
		auto Set = [&]( const FMaterialNodeInfo& Info ) { Table[static_cast<int32>( Info.Kind )] = Info; };
		using K = EMaterialNodeKind;

		Set( Make( K::Constant, "Constant", "Values", DomainAll ) );
		Set( Make( K::ScalarParameter, "ScalarParameter", "Values", DomainAll ) );
		Set( Make( K::VectorParameter, "VectorParameter", "Values", DomainAll ) );
		Set( Make( K::ParameterCollectionScalar, "ParameterCollectionScalar", "Values", DomainAll ) );
		Set( Make( K::ParameterCollectionVector, "ParameterCollectionVector", "Values", DomainAll ) );
		Set( Make( K::TextureSample, "TextureSample", "Values", DomainAll, { UvPin() } ) );

		Set( Make( K::TexCoord, "TexCoord", "Inputs", DomainAll ) );
		Set( Make( K::Time, "Time", "Inputs", DomainAll ) );
		Set( Make( K::Size, "Size", "Inputs", DomainUI | DomainParticle | DomainPost ) );
		Set( Make( K::HoverAmount, "HoverAmount", "Inputs", DomainUI ) );
		Set( Make( K::PressAmount, "PressAmount", "Inputs", DomainUI ) );
		Set( Make( K::Value01, "Value01", "Inputs", DomainUI ) );
		Set( Make( K::Enabled, "Enabled", "Inputs", DomainUI ) );
		Set( Make( K::VertexColor, "VertexColor", "Inputs", DomainUI | DomainSurface | DomainParticle ) );
		Set( Make( K::WorldPosition, "WorldPosition", "Inputs", DomainSurface ) );
		Set( Make( K::WorldNormal, "WorldNormal", "Inputs", DomainSurface ) );
		Set( Make( K::CameraVector, "CameraVector", "Inputs", DomainSurface ) );
		Set( Make( K::SceneColor, "SceneColor", "Inputs", DomainPost, { UvPin() } ) );
		Set( Make( K::SceneDepth, "SceneDepth", "Inputs", DomainPost, { UvPin() } ) );
		Set( Make( K::ParticleLife, "ParticleLife", "Inputs", DomainParticle ) );
		Set( Make( K::ParticleAge, "ParticleAge", "Inputs", DomainParticle ) );
		Set( Make( K::ParticleRandom, "ParticleRandom", "Inputs", DomainParticle ) );
		Set( Make( K::ParticleSpeed, "ParticleSpeed", "Inputs", DomainParticle ) );

		Set( Make( K::Add, "Add", "Math", DomainAll, { Pin( "A", 0.0f ), Pin( "B", 0.0f ) } ) );
		Set( Make( K::Subtract, "Subtract", "Math", DomainAll, { Pin( "A", 0.0f ), Pin( "B", 0.0f ) } ) );
		Set( Make( K::Multiply, "Multiply", "Math", DomainAll, { Pin( "A", 1.0f ), Pin( "B", 1.0f ) } ) );
		Set( Make( K::Divide, "Divide", "Math", DomainAll, { Pin( "A", 1.0f ), Pin( "B", 1.0f ) } ) );
		Set( Make( K::Min, "Min", "Math", DomainAll, { Pin( "A", 0.0f ), Pin( "B", 0.0f ) } ) );
		Set( Make( K::Max, "Max", "Math", DomainAll, { Pin( "A", 0.0f ), Pin( "B", 0.0f ) } ) );
		Set( Make( K::Power, "Power", "Math", DomainAll, { Pin( "Base", 1.0f ), Pin( "Exponent", 1.0f ) } ) );
		Set( Make( K::Mod, "Mod", "Math", DomainAll, { Pin( "A", 0.0f ), Pin( "B", 1.0f ) } ) );
		Set( Make( K::Step, "Step", "Math", DomainAll, { Pin( "Edge", 0.5f ), Pin( "Value", 0.0f ) } ) );
		Set( Make( K::Dot, "Dot", "Math", DomainAll, { Pin( "A", 0.0f ), Pin( "B", 0.0f ) } ) );
		Set( Make( K::Distance, "Distance", "Math", DomainAll, { Pin( "A", 0.0f ), Pin( "B", 0.0f ) } ) );

		Set( Make( K::Lerp, "Lerp", "Math", DomainAll, { Pin( "A", 0.0f ), Pin( "B", 1.0f ), Pin( "Alpha", 0.5f ) } ) );
		Set( Make( K::Clamp, "Clamp", "Math", DomainAll, { Pin( "Value", 0.0f ), Pin( "Min", 0.0f ), Pin( "Max", 1.0f ) } ) );
		Set( Make( K::SmoothStep, "SmoothStep", "Math", DomainAll, { Pin( "Min", 0.0f ), Pin( "Max", 1.0f ), Pin( "Value", 0.0f ) } ) );

		Set( Make( K::Abs, "Abs", "Math", DomainAll, { Pin( "Input", 0.0f ) } ) );
		Set( Make( K::Saturate, "Saturate", "Math", DomainAll, { Pin( "Input", 0.0f ) } ) );
		Set( Make( K::OneMinus, "OneMinus", "Math", DomainAll, { Pin( "Input", 0.0f ) } ) );
		Set( Make( K::Negate, "Negate", "Math", DomainAll, { Pin( "Input", 0.0f ) } ) );
		Set( Make( K::Sin, "Sin", "Math", DomainAll, { Pin( "Input", 0.0f ) } ) );
		Set( Make( K::Cos, "Cos", "Math", DomainAll, { Pin( "Input", 0.0f ) } ) );
		Set( Make( K::Frac, "Frac", "Math", DomainAll, { Pin( "Input", 0.0f ) } ) );
		Set( Make( K::Floor, "Floor", "Math", DomainAll, { Pin( "Input", 0.0f ) } ) );
		Set( Make( K::Ceil, "Ceil", "Math", DomainAll, { Pin( "Input", 0.0f ) } ) );
		Set( Make( K::Sqrt, "Sqrt", "Math", DomainAll, { Pin( "Input", 0.0f ) } ) );
		Set( Make( K::Length, "Length", "Math", DomainAll, { Pin( "Input", 0.0f ) } ) );
		Set( Make( K::Normalize, "Normalize", "Math", DomainAll, { Pin( "Input", 1.0f ) } ) );

		Set( Make( K::Append, "Append", "Utility", DomainAll, { Pin( "A", 0.0f ), Pin( "B", 0.0f ), Optional( "C" ), Optional( "D" ) } ) );
		Set( Make( K::Mask, "Mask", "Utility", DomainAll, { Pin( "Input", 0.0f ) } ) );
		Set( Make( K::Panner, "Panner", "Utility", DomainAll, { UvPin() } ) );
		Set( Make( K::Tiling, "Tiling", "Utility", DomainAll, { UvPin() } ) );
		Set( Make( K::Noise, "Noise", "Utility", DomainAll, { UvPin(), Typed( "Scale", EMaterialPinExpect::Float, 8.0f ) } ) );
		Set( Make( K::Checker, "Checker", "Utility", DomainAll, { UvPin(), Typed( "Count", EMaterialPinExpect::Float, 8.0f ) } ) );
		Set( Make( K::Fresnel, "Fresnel", "Utility", DomainSurface, { Typed( "Exponent", EMaterialPinExpect::Float, 3.0f ) } ) );

		Set( Make( K::SubstrateSlab, "SubstrateSlab", "Substrate", DomainSurface,
				   { Typed( "DiffuseColor", EMaterialPinExpect::Float3, 0.8f ), Typed( "F0", EMaterialPinExpect::Float3, 0.04f ), Typed( "Roughness", EMaterialPinExpect::Float, 0.5f ),
					 Typed( "Metallic", EMaterialPinExpect::Float, 0.0f ), Typed( "Emissive", EMaterialPinExpect::Float3, 0.0f ), Typed( "Opacity", EMaterialPinExpect::Float, 1.0f ),
					 Typed( "ClearCoat", EMaterialPinExpect::Float, 0.0f ), Typed( "ClearCoatRoughness", EMaterialPinExpect::Float, 0.1f ), Typed( "Subsurface", EMaterialPinExpect::Float3, 0.0f ),
					 Typed( "Anisotropy", EMaterialPinExpect::Float, 0.0f ), Typed( "ThinFilm", EMaterialPinExpect::Float, 0.0f ) } ) );
		Set( Make( K::SubstrateMix, "SubstrateMix", "Substrate", DomainSurface, { Typed( "A", EMaterialPinExpect::Slab ), Typed( "B", EMaterialPinExpect::Slab ), Typed( "Mix", EMaterialPinExpect::Float, 0.5f ) } ) );
		return Table;
	}
}

const FMaterialNodeInfo& NMaterialNodes::GetInfo( EMaterialNodeKind Kind )
{
	static const FMaterialNodeInfo* Table = BuildTable();
	const int32 Index = static_cast<int32>( Kind );
	return Table[Index >= 0 && Index < static_cast<int32>( EMaterialNodeKind::Count ) ? Index : 0];
}

bool NMaterialNodes::FindKind( const FString& Name, EMaterialNodeKind& OutKind )
{
	for ( int32 Index = 0; Index < static_cast<int32>( EMaterialNodeKind::Count ); ++Index )
	{
		const EMaterialNodeKind Kind = static_cast<EMaterialNodeKind>( Index );
		if ( Name == GetInfo( Kind ).Name )
		{
			OutKind = Kind;
			return true;
		}
	}
	return false;
}

bool NMaterialNodes::IsAvailable( EMaterialNodeKind Kind, EMaterialDomain Domain )
{
	return ( GetInfo( Kind ).DomainMask & ( 1u << static_cast<uint32>( Domain ) ) ) != 0;
}

TArray<FMaterialOutputPin> NMaterialNodes::GetOutputPins( EMaterialDomain Domain, EMaterialShading Shading )
{
	auto MakePin = []( const char* Name, EMaterialValueType Type, float32 Default, bool bSceneColor = false )
	{
		FMaterialOutputPin Pin;
		Pin.Name = Name;
		Pin.Type = Type;
		Pin.Default = Default;
		Pin.bDefaultIsSceneColor = bSceneColor;
		return Pin;
	};
	TArray<FMaterialOutputPin> Pins;
	switch ( Domain )
	{
	case EMaterialDomain::UI:
	case EMaterialDomain::Particle:
		Pins.Add( MakePin( "Color", EMaterialValueType::Float3, 1.0f ) );
		Pins.Add( MakePin( "Opacity", EMaterialValueType::Float, 1.0f ) );
		break;
	case EMaterialDomain::PostProcess:
		Pins.Add( MakePin( "Color", EMaterialValueType::Float3, 0.0f, true ) );
		break;
	default:
		if ( Shading == EMaterialShading::Unlit )
		{
			Pins.Add( MakePin( "Emissive", EMaterialValueType::Float3, 0.0f ) );
			Pins.Add( MakePin( "Opacity", EMaterialValueType::Float, 1.0f ) );
		}
		else if ( Shading == EMaterialShading::DefaultLit )
		{
			Pins.Add( MakePin( "BaseColor", EMaterialValueType::Float3, 0.8f ) );
			Pins.Add( MakePin( "Emissive", EMaterialValueType::Float3, 0.0f ) );
			Pins.Add( MakePin( "Opacity", EMaterialValueType::Float, 1.0f ) );
			Pins.Add( MakePin( "Roughness", EMaterialValueType::Float, 0.5f ) );
			Pins.Add( MakePin( "Metallic", EMaterialValueType::Float, 0.0f ) );
			Pins.Add( MakePin( "Specular", EMaterialValueType::Float, 0.5f ) );
		}
		else
		{
			Pins.Add( MakePin( "Surface", EMaterialValueType::Slab, 0.0f ) );
		}
		break;
	}
	return Pins;
}

int32 FMaterialGraph::AddNode( EMaterialNodeKind Kind, const FString& Name, const FVector4& Value, const FVector2& Position )
{
	FMaterialNode Node;
	Node.Id = NextId++;
	Node.Kind = Kind;
	Node.Name = Name;
	Node.Value = Value;
	Node.Position = Position;
	const FMaterialNodeInfo& Info = NMaterialNodes::GetInfo( Kind );
	for ( int32 Pin = 0; Pin < Info.NumPins; ++Pin )
	{
		Node.Inputs.Add( -1 );
	}
	if ( Kind == EMaterialNodeKind::VectorParameter || Kind == EMaterialNodeKind::ParameterCollectionVector )
	{
		Node.ValueComponents = 4;
	}
	Nodes.Add( Node );
	return Node.Id;
}

FMaterialNode* FMaterialGraph::FindNode( int32 NodeId )
{
	for ( int32 Index = 0; Index < Nodes.Num(); ++Index )
	{
		if ( Nodes[Index].Id == NodeId )
		{
			return &Nodes[Index];
		}
	}
	return nullptr;
}

const FMaterialNode* FMaterialGraph::FindNode( int32 NodeId ) const
{
	for ( int32 Index = 0; Index < Nodes.Num(); ++Index )
	{
		if ( Nodes[Index].Id == NodeId )
		{
			return &Nodes[Index];
		}
	}
	return nullptr;
}

bool FMaterialGraph::RemoveNode( int32 NodeId )
{
	int32 Found = -1;
	for ( int32 Index = 0; Index < Nodes.Num(); ++Index )
	{
		if ( Nodes[Index].Id == NodeId )
		{
			Found = Index;
		}
		else
		{
			// このノードへの線を外す
			for ( int32 Pin = 0; Pin < Nodes[Index].Inputs.Num(); ++Pin )
			{
				if ( Nodes[Index].Inputs[Pin] == NodeId )
				{
					Nodes[Index].Inputs[Pin] = -1;
				}
			}
		}
	}
	if ( Found < 0 )
	{
		return false;
	}
	Nodes.RemoveAt( Found );
	for ( int32 Index = Outputs.Num() - 1; Index >= 0; --Index )
	{
		if ( Outputs[Index].SourceNode == NodeId )
		{
			Outputs.RemoveAt( Index );
		}
	}
	return true;
}

bool FMaterialGraph::Connect( int32 NodeId, int32 Pin, int32 SourceId )
{
	FMaterialNode* Node = FindNode( NodeId );
	if ( Node == nullptr || Pin < 0 || Pin >= Node->Inputs.Num() || NodeId == SourceId )
	{
		return false;
	}
	if ( SourceId >= 0 && FindNode( SourceId ) == nullptr )
	{
		return false;
	}
	Node->Inputs[Pin] = SourceId;
	return true;
}

void FMaterialGraph::SetOutput( const FString& Pin, int32 SourceId )
{
	for ( int32 Index = 0; Index < Outputs.Num(); ++Index )
	{
		if ( Outputs[Index].Pin == Pin )
		{
			if ( SourceId < 0 )
			{
				Outputs.RemoveAt( Index );
			}
			else
			{
				Outputs[Index].SourceNode = SourceId;
			}
			return;
		}
	}
	if ( SourceId >= 0 )
	{
		FMaterialOutputLink Link;
		Link.Pin = Pin;
		Link.SourceNode = SourceId;
		Outputs.Add( Link );
	}
}

int32 FMaterialGraph::GetOutput( const FString& Pin ) const
{
	for ( int32 Index = 0; Index < Outputs.Num(); ++Index )
	{
		if ( Outputs[Index].Pin == Pin )
		{
			return Outputs[Index].SourceNode;
		}
	}
	return -1;
}
