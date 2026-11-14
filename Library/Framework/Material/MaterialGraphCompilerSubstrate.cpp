// SPDX-License-Identifier: MIT
#include "MaterialGraphCompiler.h"

#include "Library/Core/Types/Numeric.h"

namespace NMaterialGraphCompiler
{
	int32 FCompiler::EnsurePass( FResult& Slab, int32 Node )
	{
		if ( Slab.Pass >= 0 )
		{
			return Slab.Pass;
		}
		const int32 Normal = EmitInput( EMaterialInput::WorldNormal, EMaterialValueType::Float3, Node );
		const int32 View = EmitInput( EMaterialInput::CameraVector, EMaterialValueType::Float3, Node );
		const int32 Facing = EmitUnary( EMaterialUnary::Saturate, EmitBinary( EMaterialBinary::Dot, Normal, View, Node ), Node );
		const int32 Grazing = EmitBinary( EMaterialBinary::Power, EmitUnary( EMaterialUnary::OneMinus, Facing, Node ), EmitScalar( 5.0f, Node ), Node );
		const int32 Metallic = EmitUnary( EMaterialUnary::Saturate, Slab.Fields[NMaterialSlabField::Metallic], Node );
		const int32 Roughness = EmitUnary( EMaterialUnary::Saturate, Slab.Fields[NMaterialSlabField::Roughness], Node );
		const int32 SpecularColor = EmitLerp( Slab.Fields[NMaterialSlabField::F0], Slab.Fields[NMaterialSlabField::BaseColor], Metallic, Node );
		const int32 Amount = EmitBinary( EMaterialBinary::Multiply, Grazing, EmitUnary( EMaterialUnary::OneMinus, Roughness, Node ), Node );
		const int32 Fresnel = EmitUnary( EMaterialUnary::Saturate, EmitLerp( SpecularColor, Splat( EmitScalar( 1.0f, Node ), 3, Node ), Amount, Node ), Node );
		Slab.Pass = EmitBinary( EMaterialBinary::Multiply, EmitUnary( EMaterialUnary::OneMinus, Fresnel, Node ), Splat( EmitUnary( EMaterialUnary::OneMinus, Metallic, Node ), 3, Node ), Node );
		return Slab.Pass;
	}

	void FCompiler::AddDefaultLitSlab()
	{
		auto Out = [&]( const char* Pin )
		{
			const FMaterialOutputValue* Output = Program.FindOutput( Pin );
			return Output != nullptr ? Output->Value : -1;
		};
		FMaterialSubstrateSlab Entry;
		Entry.Fields[NMaterialSlabField::BaseColor] = Out( "BaseColor" );
		Entry.Fields[NMaterialSlabField::F0] = Splat( EmitBinary( EMaterialBinary::Multiply, Out( "Specular" ), EmitScalar( 0.08f, -1 ), -1 ), 3, -1 );
		Entry.Fields[NMaterialSlabField::Roughness] = Out( "Roughness" );
		Entry.Fields[NMaterialSlabField::Metallic] = Out( "Metallic" );
		Entry.Fields[NMaterialSlabField::Emissive] = Out( "Emissive" );
		Entry.Fields[NMaterialSlabField::Opacity] = Out( "Opacity" );
		Entry.Fields[NMaterialSlabField::ClearCoat] = EmitScalar( 0.0f, -1 );
		Entry.Fields[NMaterialSlabField::ClearCoatRoughness] = EmitScalar( 0.1f, -1 );
		Entry.Fields[NMaterialSlabField::Subsurface] = Fallback( -1, EMaterialValueType::Float3 );
		Entry.Fields[NMaterialSlabField::Anisotropy] = EmitScalar( 0.0f, -1 );
		Entry.Fields[NMaterialSlabField::ThinFilm] = EmitScalar( 0.0f, -1 );
		Entry.Fields[NMaterialSlabField::Normal] = Out( "Normal" );
		Entry.Weight = EmitScalar( 1.0f, -1 );
		Program.SubstrateSlabs.Add( Entry );
	}

	FResult FCompiler::SlabInput( const FMaterialNode& Node, int32 Pin )
	{
		const int32 Source = Pin < Node.Inputs.Num() ? Node.Inputs[Pin] : -1;
		FResult Fallbacks;
		Fallbacks.bSlab = true;
		Fallbacks.Type = EMaterialValueType::Slab;
		for ( int32 Field = 0; Field < NumSlabFields; ++Field )
		{
			Fallbacks.Fields[Field] = Fallback( Node.Id, SlabFieldType( Field ) );
		}
		Fallbacks.SetSingleLeaf( EmitScalar( 1.0f, Node.Id ) );
		if ( Source < 0 )
		{
			Report( EMaterialSeverity::Error, FString::Printf( "the pin '%s' needs a Substrate slab", NMaterialNodes::GetInfo( Node.Kind ).Pins[Pin].Name ), Node.Id );
			return Fallbacks;
		}
		const FResult Result = CompileNode( Source );
		if ( !Result.bSlab )
		{
			Report( EMaterialSeverity::Error, FString::Printf( "the pin '%s' needs a Substrate slab, but got a %s", NMaterialNodes::GetInfo( Node.Kind ).Pins[Pin].Name, NMaterial::ToString( Result.Type ) ), Node.Id );
			return Fallbacks;
		}
		return Result;
	}

	void FCompiler::CompileSlabOutput( int32 Link )
	{
		FResult Slab;
		if ( Link >= 0 )
		{
			Slab = CompileNode( Link );
			if ( !Slab.bSlab )
			{
				Report( EMaterialSeverity::Error, "the output 'Surface' needs a Substrate slab (SubstrateSlab, SubstrateMix or SubstrateLayer)", Link );
			}
		}
		if ( !Slab.bSlab )
		{
			// 層が無ければ、SubstrateSlab の既定の値
			const FMaterialNodeInfo& Info = NMaterialNodes::GetInfo( EMaterialNodeKind::SubstrateSlab );
			for ( int32 Field = 0; Field < NumSlabFields; ++Field )
			{
				const int32 Components = NMaterial::NumComponents( SlabFieldType( Field ) );
				const int32 Scalar = EmitScalar( Info.Pins[Field].Default, -1 );
				Slab.Fields[Field] = Components == 1 ? Scalar : Splat( Scalar, Components, -1 );
			}
			Slab.SetSingleLeaf( EmitScalar( 1.0f, -1 ) );
		}
		for ( int32 Field = 0; Field < NumSlabFields; ++Field )
		{
			FMaterialOutputValue Output;
			Output.Pin = NMaterialSlabField::GetName( Field );
			Output.Type = SlabFieldType( Field );
			Output.Value = Slab.Fields[Field];
			Program.Outputs.Add( Output );
		}
		// フルの評価では、層ごとに、陰影を計算する
		if ( Program.bFullSubstrate )
		{
			for ( int32 Leaf = 0; Leaf < Slab.NumLeaves; ++Leaf )
			{
				FMaterialSubstrateSlab Entry;
				Entry.Weight = Slab.LeafWeights[Leaf];
				Entry.Throughput = Slab.LeafThroughputs[Leaf];
				Entry.DiffuseScale = Slab.LeafDiffuseScales[Leaf];
				for ( int32 Field = 0; Field < NumSlabFields; ++Field )
				{
					Entry.Fields[Field] = Slab.LeafFields[Leaf][Field];
				}
				Program.SubstrateSlabs.Add( Entry );
			}
		}
	}
}
