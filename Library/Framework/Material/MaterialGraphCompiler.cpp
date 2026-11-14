// SPDX-License-Identifier: MIT
#include "MaterialGraphCompiler.h"

#include "Library/Core/Types/Numeric.h"

namespace NMaterialGraphCompiler
{
	EMaterialValueType SlabFieldType( int32 Field )
	{
		return ( Field == 0 || Field == 1 || Field == 4 || Field == 8 || Field == 11 ) ? EMaterialValueType::Float3 : EMaterialValueType::Float;
	}

	FCompiler::FCompiler( const FMaterialDefinition& InDefinition, const FMaterialCapabilities& InCapabilities )
		: Definition( InDefinition )
		, Graph( InDefinition.Graph )
		, Capabilities( InCapabilities )
	{
		Program.Domain = InDefinition.Domain;
		Program.Shading = InDefinition.Shading;
		Program.bFullSubstrate = InCapabilities.bFullSubstrate && InDefinition.Domain == EMaterialDomain::Surface && InDefinition.Shading != EMaterialShading::Unlit;
	}

	FMaterialProgram FCompiler::Run()
	{
		if ( !Definition.CustomHlsl.IsEmpty() )
		{
			return RunCustom();
		}
		const TArray<FMaterialOutputPin> Pins = NMaterialNodes::GetOutputPins( Definition.Domain, Definition.Shading );
		// 知らない出力ピンへの線は、注意して無視する
		for ( int32 Index = 0; Index < Graph.Outputs.Num(); ++Index )
		{
			bool bKnown = false;
			for ( int32 Pin = 0; Pin < Pins.Num(); ++Pin )
			{
				bKnown = bKnown || Graph.Outputs[Index].Pin == Pins[Pin].Name;
			}
			if ( !bKnown )
			{
				Report( EMaterialSeverity::Warning, FString::Printf( "the output pin '%s' does not exist in the %s domain (ignored)", *Graph.Outputs[Index].Pin, NMaterial::ToString( Definition.Domain ) ), Graph.Outputs[Index].SourceNode );
			}
		}
		// PostProcess: シーンの色は、いつも t0
		if ( Definition.Domain == EMaterialDomain::PostProcess )
		{
			AddTextureSlot( "SceneColor", FString(), EMaterialTextureSource::SceneColor );
		}
		for ( int32 Index = 0; Index < Pins.Num(); ++Index )
		{
			CompileOutput( Pins[Index] );
		}
		if ( Program.bFullSubstrate && Definition.Shading == EMaterialShading::DefaultLit )
		{
			AddDefaultLitSlab();
		}
		Finish();
		return Program;
	}

	void FCompiler::Report( EMaterialSeverity Severity, const FString& Message, int32 NodeId )
	{
		FMaterialDiagnostic Diagnostic;
		Diagnostic.Severity = Severity;
		Diagnostic.Message = Message;
		Diagnostic.NodeId = NodeId;
		Program.Diagnostics.Add( Diagnostic );
	}

	void FCompiler::Finish()
	{
		Program.bValid = !NMaterial::HasError( Program.Diagnostics );
	}

	FMaterialProgram FCompiler::RunCustom()
	{
		Program.bCustomHlsl = true;
		for ( int32 Index = 0; Index < Definition.Parameters.Num(); ++Index )
		{
			const FMaterialParameterDecl& Decl = Definition.Parameters[Index];
			if ( Decl.Type == EMaterialValueType::Texture )
			{
				AddTextureSlot( Decl.Name, Decl.Path, EMaterialTextureSource::Parameter );
			}
			else
			{
				FindOrAddParam( Decl.Name, FString(), Decl.Type, Decl.Value, Decl.bColor, -1 );
			}
		}
		if ( Definition.Domain == EMaterialDomain::PostProcess )
		{
			// 直指定でも、ポストプロセスは、シーンの色を t0 に置く（宣言した画像は、そのあと）。置き場の並びを、グラフと同じにするため、先頭へ入れ直す
			FMaterialTextureSlot Scene;
			Scene.Name = "SceneColor";
			Scene.Source = EMaterialTextureSource::SceneColor;
			Program.Textures.Insert( Scene, 0 );
			for ( int32 Index = 0; Index < Program.Textures.Num(); ++Index )
			{
				Program.Textures[Index].Register = Index;
			}
		}
		Report( EMaterialSeverity::Info, "this material uses a direct HLSL pixel shader (not portable to other backends)" );
		Finish();
		return Program;
	}

	int32 FCompiler::Emit( EMaterialOp Op, EMaterialValueType Type, int32 Node, int32 A, int32 B, int32 C, uint8 Sub )
	{
		FMaterialInstruction Instruction;
		Instruction.Op = Op;
		Instruction.Type = Type;
		Instruction.Node = Node;
		Instruction.A = A;
		Instruction.B = B;
		Instruction.C = C;
		Instruction.Sub = Sub;
		Program.Code.Add( Instruction );
		return Program.Code.Num() - 1;
	}

	EMaterialValueType FCompiler::TypeOf( int32 Value ) const
	{
		return Program.Code[Value].Type;
	}

	int32 FCompiler::NumOf( int32 Value ) const
	{
		return NMaterial::NumComponents( TypeOf( Value ) );
	}

	int32 FCompiler::EmitConst( const FVector4& Value, int32 NumComponents, int32 Node )
	{
		const int32 Index = Emit( EMaterialOp::Const, NMaterial::TypeFromComponents( NumComponents ), Node );
		Program.Code[Index].Constant = Value;
		return Index;
	}

	int32 FCompiler::EmitScalar( float32 Value, int32 Node )
	{
		return EmitConst( FVector4( Value, 0.0f, 0.0f, 0.0f ), 1, Node );
	}

	int32 FCompiler::Fallback( int32 Node, EMaterialValueType Type )
	{
		const int32 Components = NMath::Max( 1, NMaterial::NumComponents( Type ) );
		return EmitConst( FVector4( 0.0f, 0.0f, 0.0f, 0.0f ), Components, Node );
	}

	int32 FCompiler::Splat( int32 Value, int32 Components, int32 Node )
	{
		return Emit( EMaterialOp::Splat, NMaterial::TypeFromComponents( Components ), Node, Value );
	}

	int32 FCompiler::MaskTo( int32 Value, int32 Components, int32 Node )
	{
		const int32 Index = Emit( EMaterialOp::Mask, NMaterial::TypeFromComponents( Components ), Node, Value );
		Program.Code[Index].NumSwizzle = Components;
		for ( int32 Component = 0; Component < 4; ++Component )
		{
			Program.Code[Index].Swizzle[Component] = static_cast<uint8>( Component );
		}
		return Index;
	}

	int32 FCompiler::Coerce( int32 Value, int32 Components, int32 Node, const char* What )
	{
		const EMaterialValueType Type = TypeOf( Value );
		const int32 Have = NMaterial::NumComponents( Type );
		if ( Have == 0 )
		{
			Report( EMaterialSeverity::Error, FString::Printf( "%s needs a number or a vector, but got a %s", What, NMaterial::ToString( Type ) ), Node );
			return Fallback( Node, NMaterial::TypeFromComponents( Components ) );
		}
		if ( Have == Components )
		{
			return Value;
		}
		if ( Have == 1 )
		{
			return Splat( Value, Components, Node );
		}
		if ( Have > Components )
		{
			if ( !( Have == 4 && Components == 3 ) )
			{
				Report( EMaterialSeverity::Warning, FString::Printf( "%s: a %s is cut down to %d component(s)", What, NMaterial::ToString( Type ), Components ), Node );
			}
			return MaskTo( Value, Components, Node );
		}
		Report( EMaterialSeverity::Error, FString::Printf( "%s needs %d component(s), but got a %s", What, Components, NMaterial::ToString( Type ) ), Node );
		return Fallback( Node, NMaterial::TypeFromComponents( Components ) );
	}

	bool FCompiler::Unify( int32& A, int32& B, int32 Node, const char* What )
	{
		const int32 NumA = NumOf( A );
		const int32 NumB = NumOf( B );
		if ( NumA == 0 || NumB == 0 )
		{
			Report( EMaterialSeverity::Error, FString::Printf( "%s needs numbers or vectors", What ), Node );
			return false;
		}
		if ( NumA == NumB )
		{
			return true;
		}
		if ( NumA == 1 )
		{
			A = Splat( A, NumB, Node );
			return true;
		}
		if ( NumB == 1 )
		{
			B = Splat( B, NumA, Node );
			return true;
		}
		Report( EMaterialSeverity::Error, FString::Printf( "%s: a %s and a %s cannot be combined (the number of components differs)", What, NMaterial::ToString( TypeOf( A ) ), NMaterial::ToString( TypeOf( B ) ) ), Node );
		return false;
	}

	int32 FCompiler::FindOrAddParam( const FString& Name, const FString& ParameterCollection, EMaterialValueType Type, const FVector4& Default, bool bColor, int32 Node )
	{
		for ( int32 Index = 0; Index < Program.Params.Num(); ++Index )
		{
			const FMaterialParamSlot& Slot = Program.Params[Index];
			if ( Slot.Name == Name && Slot.ParameterCollection == ParameterCollection )
			{
				if ( Slot.Type != Type )
				{
					Report( EMaterialSeverity::Error, FString::Printf( "the parameter '%s' is used as a %s and as a %s", *Name, NMaterial::ToString( Slot.Type ), NMaterial::ToString( Type ) ), Node );
				}
				return Index;
			}
		}
		if ( Program.Params.Num() >= Capabilities.MaxParameterVectors )
		{
			Report( EMaterialSeverity::Error, FString::Printf( "there are more than %d parameters", Capabilities.MaxParameterVectors ), Node );
			return NMath::Max( 0, Program.Params.Num() - 1 );
		}
		FMaterialParamSlot Slot;
		Slot.Name = Name;
		Slot.ParameterCollection = ParameterCollection;
		Slot.Type = Type;
		Slot.Index = Program.Params.Num();
		Slot.Default = Default;
		Slot.bColor = bColor;
		Program.Params.Add( Slot );
		return Slot.Index;
	}

	int32 FCompiler::AddTextureSlot( const FString& Name, const FString& Path, EMaterialTextureSource Source )
	{
		for ( int32 Index = 0; Index < Program.Textures.Num(); ++Index )
		{
			if ( Program.Textures[Index].Name == Name && Program.Textures[Index].Source == Source )
			{
				if ( Program.Textures[Index].DefaultPath.IsEmpty() )
				{
					Program.Textures[Index].DefaultPath = Path;
				}
				return Index;
			}
		}
		FMaterialTextureSlot Slot;
		Slot.Name = Name;
		Slot.DefaultPath = Path;
		Slot.Source = Source;
		Slot.Register = Program.Textures.Num();
		Program.Textures.Add( Slot );
		return Program.Textures.Num() - 1;
	}

	int32 FCompiler::TextureSlotFor( const FString& Name, const FString& Path, int32 Node )
	{
		const int32 Before = Program.Textures.Num();
		const int32 Slot = AddTextureSlot( Name, Path, EMaterialTextureSource::Parameter );
		if ( Program.Textures.Num() > Before && Program.Textures.Num() > Capabilities.MaxTextures )
		{
			Report( EMaterialSeverity::Error, FString::Printf( "there are more than %d textures", Capabilities.MaxTextures ), Node );
		}
		return Slot;
	}

	float32 FCompiler::PinDefault( const FMaterialNode& Node, int32 Pin ) const
	{
		if ( Pin < Node.PinDefaults.Num() )
		{
			return Node.PinDefaults[Pin];
		}
		return NMaterialNodes::GetInfo( Node.Kind ).Pins[Pin].Default;
	}

	int32 FCompiler::UvValue( int32 Node )
	{
		if ( CachedUv < 0 )
		{
			CachedUv = Emit( EMaterialOp::Input, EMaterialValueType::Float2, Node, -1, -1, -1, static_cast<uint8>( EMaterialInput::Uv ) );
		}
		return CachedUv;
	}

	int32 FCompiler::PinValue( const FMaterialNode& Node, int32 Pin, bool* bOutConnected )
	{
		const FMaterialPinInfo& Info = NMaterialNodes::GetInfo( Node.Kind ).Pins[Pin];
		const int32 Source = Pin < Node.Inputs.Num() ? Node.Inputs[Pin] : -1;
		if ( bOutConnected != nullptr )
		{
			*bOutConnected = Source >= 0;
		}
		int32 Value = -1;
		if ( Source >= 0 )
		{
			const FResult Result = CompileNode( Source );
			if ( Result.bSlab )
			{
				if ( Info.Expect != EMaterialPinExpect::Slab )
				{
					Report( EMaterialSeverity::Error, FString::Printf( "the pin '%s' cannot take a Substrate slab", Info.Name ), Node.Id );
					return Fallback( Node.Id );
				}
				return -1;
			}
			Value = Result.Value;
		}
		else if ( Info.bUvDefault )
		{
			Value = UvValue( Node.Id );
		}
		else if ( Info.Expect == EMaterialPinExpect::Slab )
		{
			Report( EMaterialSeverity::Error, FString::Printf( "the pin '%s' needs a Substrate slab", Info.Name ), Node.Id );
			return Fallback( Node.Id );
		}
		else
		{
			Value = EmitScalar( PinDefault( Node, Pin ), Node.Id );
		}
		if ( Source >= 0 && Info.Expect == EMaterialPinExpect::Slab )
		{
			Report( EMaterialSeverity::Error, FString::Printf( "the pin '%s' needs a Substrate slab", Info.Name ), Node.Id );
			return Fallback( Node.Id );
		}
		switch ( Info.Expect )
		{
		case EMaterialPinExpect::Float:
			return Coerce( Value, 1, Node.Id, Info.Name );
		case EMaterialPinExpect::Float2:
			return Coerce( Value, 2, Node.Id, Info.Name );
		case EMaterialPinExpect::Float3:
			return Coerce( Value, 3, Node.Id, Info.Name );
		default:
			return Value;
		}
	}

	FResult FCompiler::ScalarResult( int32 Value ) const
	{
		FResult Result;
		Result.Value = Value;
		Result.Type = TypeOf( Value );
		return Result;
	}

	int32 FCompiler::EmitUnary( EMaterialUnary Operation, int32 Input, int32 Node )
	{
		EMaterialValueType Type = TypeOf( Input );
		if ( Operation == EMaterialUnary::Length )
		{
			Type = EMaterialValueType::Float;
		}
		return Emit( EMaterialOp::Unary, Type, Node, Input, -1, -1, static_cast<uint8>( Operation ) );
	}

	int32 FCompiler::EmitBinary( EMaterialBinary Operation, int32 A, int32 B, int32 Node )
	{
		EMaterialValueType Type = TypeOf( A );
		if ( Operation == EMaterialBinary::Dot || Operation == EMaterialBinary::Distance )
		{
			Type = EMaterialValueType::Float;
		}
		return Emit( EMaterialOp::Binary, Type, Node, A, B, -1, static_cast<uint8>( Operation ) );
	}

	int32 FCompiler::EmitInput( EMaterialInput Input, EMaterialValueType Type, int32 Node )
	{
		return Emit( EMaterialOp::Input, Type, Node, -1, -1, -1, static_cast<uint8>( Input ) );
	}

	FResult FCompiler::CompileNode( int32 NodeId )
	{
		FResult* Memo = Results.Find( NodeId );
		if ( Memo != nullptr )
		{
			if ( Memo->bVisiting )
			{
				Report( EMaterialSeverity::Error, "the graph has a loop (a node depends on its own result)", NodeId );
				FResult Failed;
				Failed.Value = Fallback( NodeId );
				return Failed;
			}
			if ( Memo->bDone )
			{
				return *Memo;
			}
		}
		const FMaterialNode* NodePointer = Graph.FindNode( NodeId );
		if ( NodePointer == nullptr )
		{
			Report( EMaterialSeverity::Error, FString::Printf( "a link points to the node %d, which does not exist", NodeId ), NodeId );
			FResult Failed;
			Failed.Value = Fallback( NodeId );
			return Failed;
		}
		FResult Visiting;
		Visiting.bVisiting = true;
		Results.Add( NodeId, Visiting );

		const FMaterialNode& Node = *NodePointer;
		FResult Result;
		if ( !NMaterialNodes::IsAvailable( Node.Kind, Definition.Domain ) )
		{
			Report( EMaterialSeverity::Error, FString::Printf( "%s is not available in the %s domain", NMaterialNodes::GetInfo( Node.Kind ).Name, NMaterial::ToString( Definition.Domain ) ), NodeId );
			Result.Value = Fallback( NodeId );
			Result.Type = EMaterialValueType::Float;
		}
		else
		{
			Result = CompileKind( Node );
		}
		Result.bDone = true;
		Result.bVisiting = false;
		if ( Result.Value >= 0 )
		{
			Program.NodeValues.Add( NodeId, Result.Value );
		}
		Results.Add( NodeId, Result );
		return Result;
	}

	int32 FCompiler::EmitLerp( int32 A, int32 B, int32 Alpha, int32 Node )
	{
		const int32 Components = NumOf( A );
		return Emit( EMaterialOp::Ternary, TypeOf( A ), Node, A, B, Components > 1 ? Splat( Alpha, Components, Node ) : Alpha, static_cast<uint8>( EMaterialTernary::Lerp ) );
	}

	int32 FCompiler::MultiplyOptional( int32 Value, int32 Factor, int32 Node )
	{
		return Value < 0 ? Factor : EmitBinary( EMaterialBinary::Multiply, Value, Factor, Node );
	}

	void FCompiler::CompileOutput( const FMaterialOutputPin& Pin )
	{
		const int32 Link = Graph.GetOutput( Pin.Name );
		if ( Pin.Type == EMaterialValueType::Slab )
		{
			CompileSlabOutput( Link );
			return;
		}
		const int32 Components = NMaterial::NumComponents( Pin.Type );
		int32 Value = -1;
		if ( Link >= 0 )
		{
			const FResult Result = CompileNode( Link );
			if ( Result.bSlab )
			{
				Report( EMaterialSeverity::Error, FString::Printf( "the output '%s' cannot take a Substrate slab", Pin.Name ), Link );
				Value = Fallback( Link, Pin.Type );
			}
			else
			{
				Value = Coerce( Result.Value, Components, Link, Pin.Name );
			}
		}
		else if ( Pin.bDefaultIsSceneColor )
		{
			// つながないポストプロセスは、シーンの色をそのまま出す
			const int32 Sample = Emit( EMaterialOp::Sample, EMaterialValueType::Float4, -1, UvValue( -1 ) );
			Program.Code[Sample].Slot = 0;
			Value = MaskTo( Sample, Components, -1 );
		}
		else
		{
			Value = Splat( EmitScalar( Pin.Default, -1 ), Components, -1 );
			if ( Components == 1 )
			{
				Value = EmitScalar( Pin.Default, -1 );
			}
		}
		FMaterialOutputValue Output;
		Output.Pin = Pin.Name;
		Output.Type = Pin.Type;
		Output.Value = Value;
		Program.Outputs.Add( Output );
	}
}
