// SPDX-License-Identifier: MIT
#include "MaterialGraphCompiler.h"

#include "Library/Core/Types/Numeric.h"

namespace NMaterialGraphCompiler
{
	FResult FCompiler::CompileKind( const FMaterialNode& Node )
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
		case K::WorldTangent:
			return ScalarResult( EmitInput( EMaterialInput::WorldTangent, EMaterialValueType::Float3, Id ) );
		case K::WorldBitangent:
			return ScalarResult( EmitInput( EMaterialInput::WorldBitangent, EMaterialValueType::Float3, Id ) );
		case K::ParticleLife:
			return ScalarResult( EmitInput( EMaterialInput::ParticleLife, EMaterialValueType::Float, Id ) );
		case K::ParticleAge:
			return ScalarResult( EmitInput( EMaterialInput::ParticleAge, EMaterialValueType::Float, Id ) );
		case K::ParticleRandom:
			return ScalarResult( EmitInput( EMaterialInput::ParticleRandom, EMaterialValueType::Float, Id ) );
		case K::ParticleSpeed:
			return ScalarResult( EmitInput( EMaterialInput::ParticleSpeed, EMaterialValueType::Float, Id ) );
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

		case K::NormalMap:
		{
			// 色（0〜1）を -1〜1 に直して、X は U の向き、Y は V の向き（DirectX の形式の法線マップ）、Z は面の法線の向きに積む。
			// 平らな色（0.5, 0.5, 1）は、面の法線そのまま。Z は、0 より少し上に収める（つないでいない・こわれた色でも、長さ 0 にならない）
			const int32 Color = PinValue( Node, 0 );
			const int32 Strength = PinValue( Node, 1 );
			const int32 Signed = EmitBinary( EMaterialBinary::Subtract, EmitBinary( EMaterialBinary::Multiply, Color, Splat( EmitScalar( 2.0f, Id ), 3, Id ), Id ), Splat( EmitScalar( 1.0f, Id ), 3, Id ), Id );
			auto Component = [&]( int32 Index )
			{
				const int32 Mask = Emit( EMaterialOp::Mask, EMaterialValueType::Float, Id, Signed );
				Program.Code[Mask].NumSwizzle = 1;
				Program.Code[Mask].Swizzle[0] = static_cast<uint8>( Index );
				return Mask;
			};
			const int32 X = EmitBinary( EMaterialBinary::Multiply, Component( 0 ), Strength, Id );
			const int32 Y = EmitBinary( EMaterialBinary::Multiply, Component( 1 ), Strength, Id );
			const int32 Z = EmitBinary( EMaterialBinary::Max, Component( 2 ), EmitScalar( 0.05f, Id ), Id );
			const int32 Tangent = EmitInput( EMaterialInput::WorldTangent, EMaterialValueType::Float3, Id );
			const int32 Bitangent = EmitInput( EMaterialInput::WorldBitangent, EMaterialValueType::Float3, Id );
			const int32 Normal = EmitInput( EMaterialInput::WorldNormal, EMaterialValueType::Float3, Id );
			int32 Sum = EmitBinary( EMaterialBinary::Multiply, Tangent, Splat( X, 3, Id ), Id );
			Sum = EmitBinary( EMaterialBinary::Add, Sum, EmitBinary( EMaterialBinary::Multiply, Bitangent, Splat( Y, 3, Id ), Id ), Id );
			Sum = EmitBinary( EMaterialBinary::Add, Sum, EmitBinary( EMaterialBinary::Multiply, Normal, Splat( Z, 3, Id ), Id ), Id );
			return ScalarResult( EmitUnary( EMaterialUnary::Normalize, Sum, Id ) );
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
			Result.SetSingleLeaf( EmitScalar( 1.0f, Id ) );
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
			if ( A.NumLeaves + B.NumLeaves > MaxSlabLeaves || A.NumLeaves <= 0 || B.NumLeaves <= 0 )
			{
				// 層が多すぎる: A の下・B の下を、それぞれ、値を混ぜた 1 つの層にまとめてから、並べる
				A.SetSingleLeaf( EmitScalar( 1.0f, Id ) );
				B.SetSingleLeaf( EmitScalar( 1.0f, Id ) );
				if ( Capabilities.bFullSubstrate )
				{
					Report( EMaterialSeverity::Info, FString::Printf( "more than %d slabs are mixed: the slabs under each input of this node are blended by their values and shaded as one", MaxSlabLeaves ), Id );
				}
			}
			// フルの評価: 値は混ぜず、両方の層を並べて、重みを掛ける（A は 1 - Mix、B は Mix）
			const int32 WeightB = EmitUnary( EMaterialUnary::Saturate, Mix, Id );
			const int32 WeightA = EmitUnary( EMaterialUnary::OneMinus, WeightB, Id );
			for ( int32 Side = 0; Side < 2; ++Side )
			{
				const FResult& Source = Side == 0 ? A : B;
				for ( int32 Leaf = 0; Leaf < Source.NumLeaves; ++Leaf )
				{
					const int32 Target = Result.NumLeaves++;
					Result.LeafWeights[Target] = EmitBinary( EMaterialBinary::Multiply, Source.LeafWeights[Leaf], Side == 0 ? WeightA : WeightB, Id );
					Result.LeafThroughputs[Target] = Source.LeafThroughputs[Leaf];
					Result.LeafDiffuseScales[Target] = Source.LeafDiffuseScales[Leaf];
					for ( int32 Field = 0; Field < NumSlabFields; ++Field )
					{
						Result.LeafFields[Target][Field] = Source.LeafFields[Leaf][Field];
					}
				}
			}
			return Result;
		}
		case K::SubstrateLayer:
		{
			FResult Top = SlabInput( Node, 0 );
			FResult Bottom = SlabInput( Node, 1 );
			const int32 Transmittance = EmitUnary( EMaterialUnary::Saturate, PinValue( Node, 2 ), Id );
			const int32 Coverage = EmitUnary( EMaterialUnary::Saturate, PinValue( Node, 3 ), Id );
			FResult Result;
			Result.bSlab = true;
			Result.Type = EMaterialValueType::Slab;
			if ( !Capabilities.bFullSubstrate )
			{
				Report( EMaterialSeverity::Info, "this backend uses a simplified Substrate: the layers of SubstrateLayer are blended into one (a clear top becomes a clear coat)", Id );
			}

			// 値を混ぜた 1 つの層（簡略版と、出力の値）: 上の層が、下を隠す分（Opaque）だけ、上の値へ寄せる。透き通る分（Clear）は、クリアコートにする
			const int32 Third = EmitConst( FVector4( 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f, 0.0f ), 3, Id );
			const int32 AverageTransmittance = EmitBinary( EMaterialBinary::Dot, Transmittance, Third, Id );
			const int32 Opaque = EmitBinary( EMaterialBinary::Multiply, Coverage, EmitUnary( EMaterialUnary::OneMinus, AverageTransmittance, Id ), Id );
			const int32 Clear = EmitBinary( EMaterialBinary::Multiply, Coverage, AverageTransmittance, Id );
			for ( int32 Field = 0; Field < NumSlabFields; ++Field )
			{
				Result.Fields[Field] = EmitLerp( Bottom.Fields[Field], Top.Fields[Field], Opaque, Id );
			}
			Result.Fields[NMaterialSlabField::ClearCoat] = EmitUnary( EMaterialUnary::Saturate, EmitBinary( EMaterialBinary::Add, Result.Fields[NMaterialSlabField::ClearCoat], Clear, Id ), Id );
			Result.Fields[NMaterialSlabField::ClearCoatRoughness] = EmitLerp( Result.Fields[NMaterialSlabField::ClearCoatRoughness], Top.Fields[NMaterialSlabField::Roughness], Clear, Id );

			if ( Top.NumLeaves + Bottom.NumLeaves > MaxSlabLeaves || Top.NumLeaves <= 0 || Bottom.NumLeaves <= 0 )
			{
				// 層が多すぎる: 上・下を、それぞれ、値を混ぜた 1 つの層にまとめてから、重ねる
				Top.SetSingleLeaf( EmitScalar( 1.0f, Id ) );
				Bottom.SetSingleLeaf( EmitScalar( 1.0f, Id ) );
				if ( Capabilities.bFullSubstrate )
				{
					Report( EMaterialSeverity::Info, FString::Printf( "more than %d slabs are layered: the slabs under each input of this node are blended by their values and shaded as one", MaxSlabLeaves ), Id );
				}
			}
			// フルの評価: 上の層は、覆う割合だけ見える。下の層は、上の層を通り抜けた分だけ見える（覆われていない所は、そのまま）。
			// 上の層の中で散らばる光は、下へ抜ける分（Transmittance）だけ減る
			const int32 One = Splat( EmitScalar( 1.0f, Id ), 3, Id );
			const int32 Through = EmitBinary( EMaterialBinary::Multiply, EnsurePass( Top, Id ), Transmittance, Id );
			const int32 Cover = EmitLerp( One, Through, Coverage, Id );
			const int32 Scattered = EmitUnary( EMaterialUnary::OneMinus, Transmittance, Id );
			for ( int32 Side = 0; Side < 2; ++Side )
			{
				const FResult& Source = Side == 0 ? Top : Bottom;
				for ( int32 Leaf = 0; Leaf < Source.NumLeaves; ++Leaf )
				{
					const int32 Target = Result.NumLeaves++;
					Result.LeafWeights[Target] = Side == 0 ? EmitBinary( EMaterialBinary::Multiply, Source.LeafWeights[Leaf], Coverage, Id ) : Source.LeafWeights[Leaf];
					Result.LeafThroughputs[Target] = Side == 0 ? Source.LeafThroughputs[Leaf] : MultiplyOptional( Source.LeafThroughputs[Leaf], Cover, Id );
					Result.LeafDiffuseScales[Target] = Side == 0 ? MultiplyOptional( Source.LeafDiffuseScales[Leaf], Scattered, Id ) : Source.LeafDiffuseScales[Leaf];
					for ( int32 Field = 0; Field < NumSlabFields; ++Field )
					{
						Result.LeafFields[Target][Field] = Source.LeafFields[Leaf][Field];
					}
				}
			}
			Result.Pass = EmitBinary( EMaterialBinary::Multiply, Cover, EnsurePass( Bottom, Id ), Id );
			return Result;
		}
		default:
			break;
		}
		Report( EMaterialSeverity::Error, "this node kind is not supported", Id );
		return ScalarResult( Fallback( Id ) );
	}
}
