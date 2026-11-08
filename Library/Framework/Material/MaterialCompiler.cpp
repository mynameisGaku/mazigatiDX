// SPDX-License-Identifier: MIT
#include "MaterialCompiler.h"

#include "Library/Core/Types/Numeric.h"

namespace
{
	constexpr int32 NumSlabFields = 11;

	// Slab（層）のフィールドの名前と型。SubstrateSlab のピンの並びと同じ
	const char* SlabFieldNames[NumSlabFields] = { "BaseColor", "F0", "Roughness", "Metallic", "Emissive", "Opacity", "ClearCoat", "ClearCoatRoughness", "Subsurface", "Anisotropy", "ThinFilm" };

	EMaterialValueType SlabFieldType( int32 Field )
	{
		return ( Field == 0 || Field == 1 || Field == 4 || Field == 8 ) ? EMaterialValueType::Float3 : EMaterialValueType::Float;
	}

	// 1 つのノードの、コンパイルの結果
	struct FResult
	{
		int32 Value = -1;                      // 命令の番号（Slab のときは -1）
		EMaterialValueType Type = EMaterialValueType::Float;
		bool bSlab = false;
		int32 Fields[NumSlabFields];
		bool bDone = false;
		bool bVisiting = false;

		FResult()
		{
			for ( int32 Index = 0; Index < NumSlabFields; ++Index )
			{
				Fields[Index] = -1;
			}
		}
	};

	class FCompiler
	{
	public:
		FCompiler( const FMaterialDefinition& InDefinition, const FMaterialCapabilities& InCapabilities )
			: Definition( InDefinition )
			, Graph( InDefinition.Graph )
			, Capabilities( InCapabilities )
		{
			Program.Domain = InDefinition.Domain;
			Program.Shading = InDefinition.Shading;
		}

		FMaterialProgram Run()
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
			Finish();
			return Program;
		}

	private:
		// ---- 診断 ----
		void Report( EMaterialSeverity Severity, const FString& Message, int32 NodeId = -1 )
		{
			FMaterialDiagnostic Diagnostic;
			Diagnostic.Severity = Severity;
			Diagnostic.Message = Message;
			Diagnostic.NodeId = NodeId;
			Program.Diagnostics.Add( Diagnostic );
		}

		void Finish()
		{
			Program.bValid = !NMaterial::HasError( Program.Diagnostics );
		}

		FMaterialProgram RunCustom()
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

		// ---- 命令を足す ----
		int32 Emit( EMaterialOp Op, EMaterialValueType Type, int32 Node, int32 A = -1, int32 B = -1, int32 C = -1, uint8 Sub = 0 )
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

		EMaterialValueType TypeOf( int32 Value ) const
		{
			return Program.Code[Value].Type;
		}

		int32 NumOf( int32 Value ) const
		{
			return NMaterial::NumComponents( TypeOf( Value ) );
		}

		int32 EmitConst( const FVector4& Value, int32 NumComponents, int32 Node )
		{
			const int32 Index = Emit( EMaterialOp::Const, NMaterial::TypeFromComponents( NumComponents ), Node );
			Program.Code[Index].Constant = Value;
			return Index;
		}

		int32 EmitScalar( float32 Value, int32 Node )
		{
			return EmitConst( FVector4( Value, 0.0f, 0.0f, 0.0f ), 1, Node );
		}

		// 失敗したときの代わりの値（0）。コンパイルは、続ける
		int32 Fallback( int32 Node, EMaterialValueType Type = EMaterialValueType::Float )
		{
			const int32 Components = NMath::Max( 1, NMaterial::NumComponents( Type ) );
			return EmitConst( FVector4( 0.0f, 0.0f, 0.0f, 0.0f ), Components, Node );
		}

		int32 Splat( int32 Value, int32 Components, int32 Node )
		{
			return Emit( EMaterialOp::Splat, NMaterial::TypeFromComponents( Components ), Node, Value );
		}

		int32 MaskTo( int32 Value, int32 Components, int32 Node )
		{
			const int32 Index = Emit( EMaterialOp::Mask, NMaterial::TypeFromComponents( Components ), Node, Value );
			Program.Code[Index].NumSwizzle = Components;
			for ( int32 Component = 0; Component < 4; ++Component )
			{
				Program.Code[Index].Swizzle[Component] = static_cast<uint8>( Component );
			}
			return Index;
		}

		// 値を、Components 成分に合わせる: 同じならそのまま、Float なら広げる、大きければ切り詰める。小さいベクトルは、エラー
		int32 Coerce( int32 Value, int32 Components, int32 Node, const char* What )
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

		// 2 つの値の型を揃える（Float は、広がる）。揃えられなければ、エラー
		bool Unify( int32& A, int32& B, int32 Node, const char* What )
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

		// ---- パラメーター・画像の置き場 ----
		int32 FindOrAddParam( const FString& Name, const FString& ParameterCollection, EMaterialValueType Type, const FVector4& Default, bool bColor, int32 Node )
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

		int32 AddTextureSlot( const FString& Name, const FString& Path, EMaterialTextureSource Source )
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

		int32 TextureSlotFor( const FString& Name, const FString& Path, int32 Node )
		{
			const int32 Before = Program.Textures.Num();
			const int32 Slot = AddTextureSlot( Name, Path, EMaterialTextureSource::Parameter );
			if ( Program.Textures.Num() > Before && Program.Textures.Num() > Capabilities.MaxTextures )
			{
				Report( EMaterialSeverity::Error, FString::Printf( "there are more than %d textures", Capabilities.MaxTextures ), Node );
			}
			return Slot;
		}

		// ---- ピンの値 ----
		float32 PinDefault( const FMaterialNode& Node, int32 Pin ) const
		{
			if ( Pin < Node.PinDefaults.Num() )
			{
				return Node.PinDefaults[Pin];
			}
			return NMaterialNodes::GetInfo( Node.Kind ).Pins[Pin].Default;
		}

		int32 UvValue( int32 Node )
		{
			if ( CachedUv < 0 )
			{
				CachedUv = Emit( EMaterialOp::Input, EMaterialValueType::Float2, Node, -1, -1, -1, static_cast<uint8>( EMaterialInput::Uv ) );
			}
			return CachedUv;
		}

		// ノード Node のピン Pin の値。つながれていれば、そのノードの値。なければ、既定の数（UV の指定のピンは、UV）。求められる型に合わせる
		int32 PinValue( const FMaterialNode& Node, int32 Pin, bool* bOutConnected = nullptr )
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

		// ---- ノード ----
		FResult ScalarResult( int32 Value ) const
		{
			FResult Result;
			Result.Value = Value;
			Result.Type = TypeOf( Value );
			return Result;
		}

		int32 EmitUnary( EMaterialUnary Operation, int32 Input, int32 Node )
		{
			EMaterialValueType Type = TypeOf( Input );
			if ( Operation == EMaterialUnary::Length )
			{
				Type = EMaterialValueType::Float;
			}
			return Emit( EMaterialOp::Unary, Type, Node, Input, -1, -1, static_cast<uint8>( Operation ) );
		}

		int32 EmitBinary( EMaterialBinary Operation, int32 A, int32 B, int32 Node )
		{
			EMaterialValueType Type = TypeOf( A );
			if ( Operation == EMaterialBinary::Dot || Operation == EMaterialBinary::Distance )
			{
				Type = EMaterialValueType::Float;
			}
			return Emit( EMaterialOp::Binary, Type, Node, A, B, -1, static_cast<uint8>( Operation ) );
		}

		int32 EmitInput( EMaterialInput Input, EMaterialValueType Type, int32 Node )
		{
			return Emit( EMaterialOp::Input, Type, Node, -1, -1, -1, static_cast<uint8>( Input ) );
		}

		FResult CompileNode( int32 NodeId )
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

		FResult CompileKind( const FMaterialNode& Node )
		{
			using K = EMaterialNodeKind;
			const int32 Id = Node.Id;
			switch ( Node.Kind )
			{
			case K::Constant:
				return ScalarResult( EmitConst( Node.Value, NMath::Clamp( Node.ValueComponents, 1, 4 ), Id ) );
			case K::ScalarParameter:
			case K::VectorParameter:
			case K::ParameterCollectionScalar:
			case K::ParameterCollectionVector:
			{
				const bool bScalar = Node.Kind == K::ScalarParameter || Node.Kind == K::ParameterCollectionScalar;
				const bool bParameterCollection = Node.Kind == K::ParameterCollectionScalar || Node.Kind == K::ParameterCollectionVector;
				if ( Node.Name.IsEmpty() || ( bParameterCollection && Node.ParameterCollection.IsEmpty() ) )
				{
					Report( EMaterialSeverity::Error, bParameterCollection ? "a collection parameter needs a collection name and a parameter name" : "a parameter needs a name", Id );
					return ScalarResult( Fallback( Id ) );
				}
				const EMaterialValueType Type = bScalar ? EMaterialValueType::Float : NMaterial::TypeFromComponents( NMath::Clamp( Node.ValueComponents, 1, 4 ) );
				const int32 Slot = FindOrAddParam( Node.Name, bParameterCollection ? Node.ParameterCollection : FString(), Type, Node.Value, Node.bColor, Id );
				const int32 Index = Emit( EMaterialOp::Param, Type, Id );
				Program.Code[Index].Slot = Slot;
				return ScalarResult( Index );
			}
			case K::TextureSample:
			{
				if ( Node.Name.IsEmpty() )
				{
					Report( EMaterialSeverity::Error, "a texture sample needs a texture name", Id );
					return ScalarResult( Fallback( Id, EMaterialValueType::Float4 ) );
				}
				const int32 Uv = PinValue( Node, 0 );
				const int32 Slot = TextureSlotFor( Node.Name, Node.TexturePath, Id );
				const int32 Index = Emit( EMaterialOp::Sample, EMaterialValueType::Float4, Id, Uv );
				Program.Code[Index].Slot = Slot;
				return ScalarResult( Index );
			}
			case K::TexCoord:
				return ScalarResult( UvValue( Id ) );
			case K::Time:
				return ScalarResult( EmitInput( EMaterialInput::Time, EMaterialValueType::Float, Id ) );
			case K::Size:
				return ScalarResult( EmitInput( EMaterialInput::Size, EMaterialValueType::Float2, Id ) );
			case K::HoverAmount:
				return ScalarResult( EmitInput( EMaterialInput::Hover, EMaterialValueType::Float, Id ) );
			case K::PressAmount:
				return ScalarResult( EmitInput( EMaterialInput::Press, EMaterialValueType::Float, Id ) );
			case K::Value01:
				return ScalarResult( EmitInput( EMaterialInput::Value, EMaterialValueType::Float, Id ) );
			case K::Enabled:
				return ScalarResult( EmitInput( EMaterialInput::Enabled, EMaterialValueType::Float, Id ) );
			case K::VertexColor:
				return ScalarResult( EmitInput( EMaterialInput::VertexColor, EMaterialValueType::Float4, Id ) );
			case K::WorldPosition:
				return ScalarResult( EmitInput( EMaterialInput::WorldPosition, EMaterialValueType::Float3, Id ) );
			case K::WorldNormal:
				return ScalarResult( EmitInput( EMaterialInput::WorldNormal, EMaterialValueType::Float3, Id ) );
			case K::CameraVector:
				return ScalarResult( EmitInput( EMaterialInput::CameraVector, EMaterialValueType::Float3, Id ) );
			case K::SceneColor:
			{
				const int32 Uv = PinValue( Node, 0 );
				const int32 Index = Emit( EMaterialOp::Sample, EMaterialValueType::Float4, Id, Uv );
				Program.Code[Index].Slot = 0;   // PostProcess: シーンの色は、いつも置き場 0
				return ScalarResult( Index );
			}
			case K::SceneDepth:
			{
				const int32 Uv = PinValue( Node, 0 );
				if ( !Capabilities.bSceneDepthRead )
				{
					Report( EMaterialSeverity::Info, "this backend cannot read the scene depth: SceneDepth is replaced by 1 (far)", Id );
					return ScalarResult( EmitScalar( 1.0f, Id ) );
				}
				Program.bUsesSceneDepth = true;
				const int32 Slot = AddTextureSlot( "SceneDepth", FString(), EMaterialTextureSource::SceneDepth );
				if ( Program.Textures.Num() > Capabilities.MaxTextures )
				{
					Report( EMaterialSeverity::Error, FString::Printf( "there are more than %d textures", Capabilities.MaxTextures ), Id );
				}
				const int32 Index = Emit( EMaterialOp::SceneDepth, EMaterialValueType::Float, Id, Uv );
				Program.Code[Index].Slot = Slot;
				return ScalarResult( Index );
			}

			case K::Add:
			case K::Subtract:
			case K::Multiply:
			case K::Divide:
			case K::Min:
			case K::Max:
			case K::Power:
			case K::Mod:
			case K::Step:
			case K::Dot:
			case K::Distance:
			{
				int32 A = PinValue( Node, 0 );
				int32 B = PinValue( Node, 1 );
				if ( !Unify( A, B, Id, NMaterialNodes::GetInfo( Node.Kind ).Name ) )
				{
					return ScalarResult( Fallback( Id ) );
				}
				EMaterialBinary Operation = EMaterialBinary::Add;
				switch ( Node.Kind )
				{
				case K::Subtract:
					Operation = EMaterialBinary::Subtract;
					break;
				case K::Multiply:
					Operation = EMaterialBinary::Multiply;
					break;
				case K::Divide:
					Operation = EMaterialBinary::Divide;
					break;
				case K::Min:
					Operation = EMaterialBinary::Min;
					break;
				case K::Max:
					Operation = EMaterialBinary::Max;
					break;
				case K::Power:
					Operation = EMaterialBinary::Power;
					break;
				case K::Mod:
					Operation = EMaterialBinary::Mod;
					break;
				case K::Step:
					Operation = EMaterialBinary::Step;
					break;
				case K::Dot:
					Operation = EMaterialBinary::Dot;
					break;
				case K::Distance:
					Operation = EMaterialBinary::Distance;
					break;
				default:
					break;
				}
				return ScalarResult( EmitBinary( Operation, A, B, Id ) );
			}

			case K::Lerp:
			{
				int32 A = PinValue( Node, 0 );
				int32 B = PinValue( Node, 1 );
				int32 Alpha = PinValue( Node, 2 );
				if ( !Unify( A, B, Id, "Lerp" ) )
				{
					return ScalarResult( Fallback( Id ) );
				}
				// Alpha は、Float か、A・B と同じ型
				if ( NumOf( Alpha ) != 1 && NumOf( Alpha ) != NumOf( A ) )
				{
					Report( EMaterialSeverity::Error, "Lerp: Alpha must be a Float or have the same type as A and B", Id );
					return ScalarResult( Fallback( Id ) );
				}
				if ( NumOf( Alpha ) == 1 && NumOf( A ) != 1 )
				{
					Alpha = Splat( Alpha, NumOf( A ), Id );
				}
				return ScalarResult( Emit( EMaterialOp::Ternary, TypeOf( A ), Id, A, B, Alpha, static_cast<uint8>( EMaterialTernary::Lerp ) ) );
			}
			case K::Clamp:
			case K::SmoothStep:
			{
				const bool bClamp = Node.Kind == K::Clamp;
				// Clamp: Value, Min, Max / SmoothStep: Min, Max, Value
				int32 Value = PinValue( Node, bClamp ? 0 : 2 );
				int32 Low = PinValue( Node, bClamp ? 1 : 0 );
				int32 High = PinValue( Node, bClamp ? 2 : 1 );
				const int32 Components = NumOf( Value );
				if ( Components == 0 )
				{
					Report( EMaterialSeverity::Error, "the value needs a number or a vector", Id );
					return ScalarResult( Fallback( Id ) );
				}
				Low = Coerce( Low, Components, Id, "Min" );
				High = Coerce( High, Components, Id, "Max" );
				return ScalarResult( bClamp ? Emit( EMaterialOp::Ternary, TypeOf( Value ), Id, Value, Low, High, static_cast<uint8>( EMaterialTernary::Clamp ) )
											: Emit( EMaterialOp::Ternary, TypeOf( Value ), Id, Low, High, Value, static_cast<uint8>( EMaterialTernary::SmoothStep ) ) );
			}

			case K::Abs:
			case K::Saturate:
			case K::OneMinus:
			case K::Negate:
			case K::Sin:
			case K::Cos:
			case K::Frac:
			case K::Floor:
			case K::Ceil:
			case K::Sqrt:
			case K::Length:
			case K::Normalize:
			{
				const int32 Input = PinValue( Node, 0 );
				if ( NumOf( Input ) == 0 )
				{
					Report( EMaterialSeverity::Error, FString::Printf( "%s needs a number or a vector", NMaterialNodes::GetInfo( Node.Kind ).Name ), Id );
					return ScalarResult( Fallback( Id ) );
				}
				EMaterialUnary Operation = EMaterialUnary::Abs;
				switch ( Node.Kind )
				{
				case K::Saturate:
					Operation = EMaterialUnary::Saturate;
					break;
				case K::OneMinus:
					Operation = EMaterialUnary::OneMinus;
					break;
				case K::Negate:
					Operation = EMaterialUnary::Negate;
					break;
				case K::Sin:
					Operation = EMaterialUnary::Sin;
					break;
				case K::Cos:
					Operation = EMaterialUnary::Cos;
					break;
				case K::Frac:
					Operation = EMaterialUnary::Frac;
					break;
				case K::Floor:
					Operation = EMaterialUnary::Floor;
					break;
				case K::Ceil:
					Operation = EMaterialUnary::Ceil;
					break;
				case K::Sqrt:
					Operation = EMaterialUnary::Sqrt;
					break;
				case K::Length:
					Operation = EMaterialUnary::Length;
					break;
				case K::Normalize:
					Operation = EMaterialUnary::Normalize;
					break;
				default:
					break;
				}
				return ScalarResult( EmitUnary( Operation, Input, Id ) );
			}

			case K::Append:
			{
				int32 Total = 0;
				int32 Value = -1;
				for ( int32 Pin = 0; Pin < 4; ++Pin )
				{
					bool bConnected = false;
					const bool bOptional = NMaterialNodes::GetInfo( Node.Kind ).Pins[Pin].bOptional;
					if ( bOptional && ( Pin >= Node.Inputs.Num() || Node.Inputs[Pin] < 0 ) )
					{
						continue;
					}
					const int32 Part = PinValue( Node, Pin, &bConnected );
					const int32 Count = NumOf( Part );
					if ( Count == 0 )
					{
						Report( EMaterialSeverity::Error, "Append needs numbers or vectors", Id );
						return ScalarResult( Fallback( Id ) );
					}
					Total += Count;
					if ( Total > 4 )
					{
						Report( EMaterialSeverity::Error, "Append makes more than 4 components", Id );
						return ScalarResult( Fallback( Id, EMaterialValueType::Float4 ) );
					}
					Value = Value < 0 ? Part : Emit( EMaterialOp::Append, NMaterial::TypeFromComponents( Total ), Id, Value, Part );
				}
				return ScalarResult( Value );
			}
			case K::Mask:
			{
				const int32 Input = PinValue( Node, 0 );
				const int32 Have = NumOf( Input );
				if ( Have == 0 )
				{
					Report( EMaterialSeverity::Error, "Mask needs a number or a vector", Id );
					return ScalarResult( Fallback( Id ) );
				}
				const FString& Swizzle = Node.Name;
				if ( Swizzle.Len() < 1 || Swizzle.Len() > 4 )
				{
					Report( EMaterialSeverity::Error, FString::Printf( "Mask '%s' must have 1 to 4 letters (x y z w or r g b a)", *Swizzle ), Id );
					return ScalarResult( Fallback( Id ) );
				}
				uint8 Indices[4] = { 0, 0, 0, 0 };
				for ( int32 Index = 0; Index < Swizzle.Len(); ++Index )
				{
					const char Letter = Swizzle[Index];
					int32 Component = -1;
					if ( Letter == 'x' || Letter == 'r' )
					{
						Component = 0;
					}
					else if ( Letter == 'y' || Letter == 'g' )
					{
						Component = 1;
					}
					else if ( Letter == 'z' || Letter == 'b' )
					{
						Component = 2;
					}
					else if ( Letter == 'w' || Letter == 'a' )
					{
						Component = 3;
					}
					if ( Component < 0 || Component >= Have )
					{
						Report( EMaterialSeverity::Error, FString::Printf( "Mask '%s': the letter '%c' is not a component of a %s", *Swizzle, Letter, NMaterial::ToString( TypeOf( Input ) ) ), Id );
						return ScalarResult( Fallback( Id ) );
					}
					Indices[Index] = static_cast<uint8>( Component );
				}
				const int32 Index = Emit( EMaterialOp::Mask, NMaterial::TypeFromComponents( Swizzle.Len() ), Id, Input );
				Program.Code[Index].NumSwizzle = Swizzle.Len();
				for ( int32 Component = 0; Component < 4; ++Component )
				{
					Program.Code[Index].Swizzle[Component] = Indices[Component];
				}
				return ScalarResult( Index );
			}
			case K::Panner:
			{
				const int32 Uv = PinValue( Node, 0 );
				const int32 Speed = EmitConst( FVector4( Node.Value.x, Node.Value.y, 0.0f, 0.0f ), 2, Id );
				const int32 Time = EmitInput( EMaterialInput::Time, EMaterialValueType::Float, Id );
				const int32 Offset = EmitBinary( EMaterialBinary::Multiply, Speed, Splat( Time, 2, Id ), Id );
				return ScalarResult( EmitBinary( EMaterialBinary::Add, Uv, Offset, Id ) );
			}
			case K::Tiling:
			{
				const int32 Uv = PinValue( Node, 0 );
				const int32 Scale = EmitConst( FVector4( Node.Value.x, Node.Value.y, 0.0f, 0.0f ), 2, Id );
				const int32 Offset = EmitConst( FVector4( Node.Value.z, Node.Value.w, 0.0f, 0.0f ), 2, Id );
				return ScalarResult( EmitBinary( EMaterialBinary::Add, EmitBinary( EMaterialBinary::Multiply, Uv, Scale, Id ), Offset, Id ) );
			}
			case K::Noise:
			{
				const int32 Uv = PinValue( Node, 0 );
				const int32 Scale = PinValue( Node, 1 );
				const int32 Scaled = EmitBinary( EMaterialBinary::Multiply, Uv, Splat( Scale, 2, Id ), Id );
				return ScalarResult( Emit( EMaterialOp::Noise, EMaterialValueType::Float, Id, Scaled ) );
			}
			case K::Checker:
			{
				const int32 Uv = PinValue( Node, 0 );
				const int32 Count = PinValue( Node, 1 );
				const int32 Cells = EmitUnary( EMaterialUnary::Floor, EmitBinary( EMaterialBinary::Multiply, Uv, Splat( Count, 2, Id ), Id ), Id );
				int32 X = MaskTo( Cells, 1, Id );
				const int32 Y = Emit( EMaterialOp::Mask, EMaterialValueType::Float, Id, Cells );
				Program.Code[Y].NumSwizzle = 1;
				Program.Code[Y].Swizzle[0] = 1;
				const int32 Sum = EmitBinary( EMaterialBinary::Add, X, Y, Id );
				return ScalarResult( EmitBinary( EMaterialBinary::Mod, Sum, EmitScalar( 2.0f, Id ), Id ) );
			}
			case K::Fresnel:
			{
				const int32 Exponent = PinValue( Node, 0 );
				const int32 Normal = EmitInput( EMaterialInput::WorldNormal, EMaterialValueType::Float3, Id );
				const int32 View = EmitInput( EMaterialInput::CameraVector, EMaterialValueType::Float3, Id );
				const int32 Facing = EmitUnary( EMaterialUnary::Saturate, EmitBinary( EMaterialBinary::Dot, Normal, View, Id ), Id );
				return ScalarResult( EmitBinary( EMaterialBinary::Power, EmitUnary( EMaterialUnary::OneMinus, Facing, Id ), Exponent, Id ) );
			}

			case K::SubstrateSlab:
			{
				FResult Result;
				Result.bSlab = true;
				Result.Type = EMaterialValueType::Slab;
				for ( int32 Field = 0; Field < NumSlabFields; ++Field )
				{
					bool bConnected = false;
					Result.Fields[Field] = PinValue( Node, Field, &bConnected );
					// 簡略版が無視する性質を、つないでいたら、知らせる
					if ( bConnected && !Capabilities.bFullSubstrate && ( Field == 8 || Field == 9 || Field == 10 ) )
					{
						Report( EMaterialSeverity::Info, FString::Printf( "this backend uses a simplified Substrate: '%s' is ignored", NMaterialNodes::GetInfo( Node.Kind ).Pins[Field].Name ), Id );
					}
				}
				return Result;
			}
			case K::SubstrateMix:
			{
				FResult A = SlabInput( Node, 0 );
				FResult B = SlabInput( Node, 1 );
				const int32 Mix = PinValue( Node, 2 );
				FResult Result;
				Result.bSlab = true;
				Result.Type = EMaterialValueType::Slab;
				for ( int32 Field = 0; Field < NumSlabFields; ++Field )
				{
					Result.Fields[Field] = Emit( EMaterialOp::Ternary, SlabFieldType( Field ), Id, A.Fields[Field], B.Fields[Field], SlabFieldType( Field ) == EMaterialValueType::Float3 ? Splat( Mix, 3, Id ) : Mix, static_cast<uint8>( EMaterialTernary::Lerp ) );
				}
				return Result;
			}
			default:
				break;
			}
			Report( EMaterialSeverity::Error, "this node kind is not supported", Id );
			return ScalarResult( Fallback( Id ) );
		}

		// Slab のピン（A・B）の層
		FResult SlabInput( const FMaterialNode& Node, int32 Pin )
		{
			const int32 Source = Pin < Node.Inputs.Num() ? Node.Inputs[Pin] : -1;
			FResult Fallbacks;
			Fallbacks.bSlab = true;
			Fallbacks.Type = EMaterialValueType::Slab;
			for ( int32 Field = 0; Field < NumSlabFields; ++Field )
			{
				Fallbacks.Fields[Field] = Fallback( Node.Id, SlabFieldType( Field ) );
			}
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

		// ---- 出力 ----
		void CompileOutput( const FMaterialOutputPin& Pin )
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

		void CompileSlabOutput( int32 Link )
		{
			FResult Slab;
			if ( Link >= 0 )
			{
				Slab = CompileNode( Link );
				if ( !Slab.bSlab )
				{
					Report( EMaterialSeverity::Error, "the output 'Surface' needs a Substrate slab (SubstrateSlab or SubstrateMix)", Link );
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
			}
			for ( int32 Field = 0; Field < NumSlabFields; ++Field )
			{
				FMaterialOutputValue Output;
				Output.Pin = SlabFieldNames[Field];
				Output.Type = SlabFieldType( Field );
				Output.Value = Slab.Fields[Field];
				Program.Outputs.Add( Output );
			}
		}

		const FMaterialDefinition& Definition;
		const FMaterialGraph& Graph;
		FMaterialCapabilities Capabilities;
		FMaterialProgram Program;
		TMap<int32, FResult> Results;
		int32 CachedUv = -1;
	};
}

FMaterialProgram NMaterialCompiler::Compile( const FMaterialDefinition& Definition, const FMaterialCapabilities& Capabilities )
{
	FCompiler Compiler( Definition, Capabilities );
	return Compiler.Run();
}
