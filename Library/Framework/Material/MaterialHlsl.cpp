// SPDX-License-Identifier: MIT
#include "MazigatiEngineShaderSources.generated.h"
#include "MaterialHlsl.h"

#include "Library/Framework/Material/MaterialSubstrate.h"

namespace
{
	const char* TypeName( EMaterialValueType Type )
	{
		switch ( Type )
		{
		case EMaterialValueType::Float2:
			return "float2";
		case EMaterialValueType::Float3:
			return "float3";
		case EMaterialValueType::Float4:
			return "float4";
		default:
			return "float";
		}
	}

	// 浮動小数点の定数（HLSL では、小数点がないと整数になるので、必ず小数点を付ける）
	FString Literal( float32 Value )
	{
		FString Text = FString::Printf( "%.9g", static_cast<double>( Value ) );
		if ( !Text.Contains( "." ) && !Text.Contains( "e" ) && !Text.Contains( "E" ) && !Text.Contains( "n" ) )
		{
			Text += ".0";
		}
		return Text;
	}

	FString ConstantExpression( const FMaterialInstruction& Instruction )
	{
		const int32 N = NMaterial::NumComponents( Instruction.Type );
		const float32 Components[4] = { Instruction.Constant.x, Instruction.Constant.y, Instruction.Constant.z, Instruction.Constant.w };
		if ( N <= 1 )
		{
			return Literal( Components[0] );
		}
		FString Text = FString( TypeName( Instruction.Type ) ) + "( ";
		for ( int32 Index = 0; Index < N; ++Index )
		{
			Text += Literal( Components[Index] );
			Text += Index + 1 < N ? ", " : " )";
		}
		return Text;
	}

	const char* SwizzleLetters = "xyzw";

	FString ParamExpression( const FMaterialParamSlot& Slot )
	{
		const int32 N = NMaterial::NumComponents( Slot.Type );
		FString Text = FString::Printf( "g_Params[%d].", Slot.Index );
		for ( int32 Index = 0; Index < N; ++Index )
		{
			Text += SwizzleLetters[Index];
		}
		return Text;
	}

	FString InputExpression( EMaterialInput Input, EMaterialDomain Domain )
	{
		switch ( Input )
		{
		case EMaterialInput::Uv:
			return "i.Uv";
		case EMaterialInput::Time:
			return "g_Time.x";
		case EMaterialInput::Size:
			return "g_Rect.xy";
		case EMaterialInput::Hover:
			return "g_State.x";
		case EMaterialInput::Press:
			return "g_State.y";
		case EMaterialInput::Value:
			return "g_State.z";
		case EMaterialInput::Enabled:
			return "g_State.w";
		case EMaterialInput::VertexColor:
			return "i.Color";
		case EMaterialInput::WorldPosition:
			return "i.World";
		case EMaterialInput::LocalPosition:
			return "i.LocalPosition";
		case EMaterialInput::WorldNormal:
			return "normalize( i.Normal )";
		case EMaterialInput::ParticleLife:
			return "i.Particle.x";
		case EMaterialInput::ParticleAge:
			return "i.Particle.y";
		case EMaterialInput::ParticleRandom:
			return "i.Particle.z";
		case EMaterialInput::ParticleSpeed:
			return "i.Particle.w";
		case EMaterialInput::WorldTangent:
			return "SubstrateTangent( normalize( i.Normal ), i.World, i.Uv )";
		case EMaterialInput::WorldBitangent:
			return "SubstrateBitangent( normalize( i.Normal ), i.World, i.Uv )";
		default:
			( void ) Domain;
			return "normalize( g_CameraPosition.xyz - i.World )";
		}
	}

	FString UnaryExpression( EMaterialUnary Operation, const FString& A )
	{
		switch ( Operation )
		{
		case EMaterialUnary::Abs:
			return "abs( " + A + " )";
		case EMaterialUnary::Saturate:
			return "saturate( " + A + " )";
		case EMaterialUnary::OneMinus:
			return "( 1.0 - " + A + " )";
		case EMaterialUnary::Negate:
			return "( -" + A + " )";
		case EMaterialUnary::Sin:
			return "sin( " + A + " )";
		case EMaterialUnary::Cos:
			return "cos( " + A + " )";
		case EMaterialUnary::Frac:
			return "frac( " + A + " )";
		case EMaterialUnary::Floor:
			return "floor( " + A + " )";
		case EMaterialUnary::Ceil:
			return "ceil( " + A + " )";
		case EMaterialUnary::Sqrt:
			return "sqrt( " + A + " )";
		case EMaterialUnary::Length:
			return "length( " + A + " )";
		default:
			return "normalize( " + A + " )";
		}
	}

	FString BinaryExpression( EMaterialBinary Operation, const FString& A, const FString& B )
	{
		switch ( Operation )
		{
		case EMaterialBinary::Add:
			return "( " + A + " + " + B + " )";
		case EMaterialBinary::Subtract:
			return "( " + A + " - " + B + " )";
		case EMaterialBinary::Multiply:
			return "( " + A + " * " + B + " )";
		case EMaterialBinary::Divide:
			return "( " + A + " / " + B + " )";
		case EMaterialBinary::Min:
			return "min( " + A + ", " + B + " )";
		case EMaterialBinary::Max:
			return "max( " + A + ", " + B + " )";
		case EMaterialBinary::Power:
			return "pow( " + A + ", " + B + " )";
		case EMaterialBinary::Mod:
			return "MatMod( " + A + ", " + B + " )";
		case EMaterialBinary::Step:
			return "step( " + A + ", " + B + " )";
		case EMaterialBinary::Dot:
			return "dot( " + A + ", " + B + " )";
		default:
			return "distance( " + A + ", " + B + " )";
		}
	}

	FString Name( int32 Index )
	{
		return FString::Printf( "v%d", Index );
	}

	// 補助の関数（ノイズなど）。CPU の MaterialEval と、同じ計算
	const char* Helpers = NEngineShaderSources::Material_Helpers;

	const char* FrameDeclarations = NEngineShaderSources::Material_FrameDeclarations;

	const char* DepthHelper = NEngineShaderSources::Material_DepthHelper;

	const char* SurfaceDeclarations = NEngineShaderSources::Material_SurfaceDeclarations;

	// 場面の光を、Substrate の関数へ渡す形にする関数（cbScene から。影のマップがあれば、影も読む）
	FString LightingFunctions( const FMaterialHlslFeatures& Features )
	{
		FString Text;
		if ( Features.bShadowMap )
		{
			// 影: 光から見た深度（影のマップ）と比べる。まわりの 4 x 4 個を、なめらかに重み付けして、境目をぼかす。マップの外は、光が届く
			Text += NEngineShaderSources::Material_ShadowFunction;
		}
		Text += NEngineShaderSources::Material_LightingStart;
		Text += Features.bShadowMap ? "\tlighting.Shadow = MatShadow( world, normal );\n" : "\tlighting.Shadow = 1.0;\n";
		Text += NEngineShaderSources::Material_LightingEnd;
		return Text;
	}

	// フルの評価: 層ごとに、SubstrateShadeSlab（MaterialSubstrate.cpp の HLSL）で陰影を計算して、重みで足す（DefaultLit は、層 1 つ）
	FString SubstrateFullShading( const FMaterialProgram& Program )
	{
		FString Text;
		Text += "\tSubstrateGeometry geometry;\n";
		Text += "\tgeometry.Normal = normalize( i.Normal );\n";
		Text += "\tgeometry.Tangent = SubstrateTangent( geometry.Normal, i.World, i.Uv );\n";
		Text += "\tgeometry.ToLight = -g_LightDirection.xyz;\n";
		Text += "\tgeometry.ToCamera = g_CameraPosition.xyz - i.World;\n";
		Text += "\tgeometry.Position = i.World;\n";
		Text += "\tSubstrateLighting lighting = MatLighting( i.World, geometry.Normal );\n";
		Text += "\tSubstrateSlab slab;\n";
		Text += "\tfloat3 color = ( float3 )0.0;\n";
		for ( int32 Index = 0; Index < Program.SubstrateSlabs.Num(); ++Index )
		{
			const FMaterialSubstrateSlab& Slab = Program.SubstrateSlabs[Index];
			auto Set = [&]( const char* Member, int32 FieldIndex )
			{
				Text += FString( "\tslab." ) + Member + " = " + Name( Slab.Fields[FieldIndex] ) + ";\n";
			};
			Set( "BaseColor", NMaterialSlabField::BaseColor );
			Set( "F0", NMaterialSlabField::F0 );
			Set( "Roughness", NMaterialSlabField::Roughness );
			Set( "Metallic", NMaterialSlabField::Metallic );
			Set( "Emissive", NMaterialSlabField::Emissive );
			Set( "ClearCoat", NMaterialSlabField::ClearCoat );
			Set( "ClearCoatRoughness", NMaterialSlabField::ClearCoatRoughness );
			Set( "Subsurface", NMaterialSlabField::Subsurface );
			Set( "Anisotropy", NMaterialSlabField::Anisotropy );
			Set( "ThinFilm", NMaterialSlabField::ThinFilm );
			Set( "Normal", NMaterialSlabField::Normal );
			Text += "\tslab.Throughput = " + ( Slab.Throughput >= 0 ? Name( Slab.Throughput ) : FString( "( float3 )1.0" ) ) + ";\n";
			Text += "\tslab.DiffuseScale = " + ( Slab.DiffuseScale >= 0 ? Name( Slab.DiffuseScale ) : FString( "( float3 )1.0" ) ) + ";\n";
			Text += "\tcolor += " + Name( Slab.Weight ) + " * SubstrateShadeSlab( slab, geometry, lighting );\n";
		}
		const FMaterialOutputValue* Opacity = Program.FindOutput( "Opacity" );
		Text += "\treturn float4( color, " + ( Opacity != nullptr ? Name( Opacity->Value ) : FString( "1.0" ) ) + " );\n";
		return Text;
	}

	// Surface の陰影の本文（フルの評価か、簡略版: 平行光 1 つの、拡散・鏡面・クリアコート・発光）
	FString SurfaceShading( const FMaterialProgram& Program )
	{
		if ( NMaterialHlsl::UsesSceneLighting( Program ) )
		{
			return SubstrateFullShading( Program );
		}
		auto Out = [&]( const char* Pin ) -> FString
		{
			const FMaterialOutputValue* Output = Program.FindOutput( Pin );
			return Output != nullptr ? Name( Output->Value ) : FString( "0.0" );
		};
		FString Text;
		if ( Program.Shading == EMaterialShading::Unlit )
		{
			Text += "\treturn float4( " + Out( "Emissive" ) + ", " + Out( "Opacity" ) + " );\n";
			return Text;
		}
		const bool bSubstrate = Program.Shading == EMaterialShading::Substrate;
		Text += "\tfloat3 baseColor = " + Out( "BaseColor" ) + ";\n";
		Text += "\tfloat3 emissive = " + Out( "Emissive" ) + ";\n";
		Text += "\tfloat opacity = " + Out( "Opacity" ) + ";\n";
		Text += "\tfloat roughness = saturate( " + Out( "Roughness" ) + " );\n";
		Text += "\tfloat metallic = saturate( " + Out( "Metallic" ) + " );\n";
		if ( bSubstrate )
		{
			Text += "\tfloat3 f0Color = " + Out( "F0" ) + ";\n";
			Text += "\tfloat clearCoat = saturate( " + Out( "ClearCoat" ) + " );\n";
			Text += "\tfloat clearCoatRoughness = saturate( " + Out( "ClearCoatRoughness" ) + " );\n";
		}
		else
		{
			Text += "\tfloat3 f0Color = ( float3 )( 0.08 * " + Out( "Specular" ) + " );\n";
		}
		Text += "\tfloat3 ownNormal = " + Out( "Normal" ) + ";\n";
		Text += "\tfloat3 N = normalize( dot( ownNormal, ownNormal ) > 1.0e-12 ? ownNormal : i.Normal );\n";
		Text += "\tfloat3 L = -normalize( g_LightDirection.xyz );\n";
		Text += "\tfloat3 V = normalize( g_CameraPosition.xyz - i.World );\n";
		Text += "\tfloat3 H = normalize( L + V );\n";
		Text += "\tfloat ndl = saturate( dot( N, L ) );\n";
		Text += "\tfloat shininess = exp2( 10.0 * ( 1.0 - roughness ) + 1.0 );\n";
		Text += "\tfloat3 diffuseColor = baseColor * ( 1.0 - metallic );\n";
		Text += "\tfloat3 f0 = lerp( f0Color, baseColor, metallic );\n";
		Text += "\tfloat specularTerm = pow( saturate( dot( N, H ) ), shininess ) * ndl * ( 1.0 - roughness * 0.5 );\n";
		Text += "\tfloat3 color = diffuseColor * ( ndl * 0.85 + 0.15 ) + f0 * specularTerm + emissive;\n";
		if ( bSubstrate )
		{
			// クリアコート: 下の層を少し暗くして、上に、白い鏡面の層を足す
			Text += "\tfloat coatShininess = exp2( 10.0 * ( 1.0 - clearCoatRoughness ) + 1.0 );\n";
			Text += "\tfloat coatSpecular = pow( saturate( dot( N, H ) ), coatShininess ) * ndl;\n";
			Text += "\tcolor = color * ( 1.0 - 0.04 * clearCoat ) + ( float3 )( coatSpecular * clearCoat * 0.6 );\n";
		}
		Text += "\treturn float4( color, opacity );\n";
		return Text;
	}
}

FString NMaterialHlsl::GetSurfaceVertexSource()
{
	return FString( SurfaceDeclarations ) + NEngineShaderSources::Material_SurfaceVertex;
}

FString NMaterialHlsl::GetSurfaceDepthPixelSource()
{
	return FString( SurfaceDeclarations ) + NEngineShaderSources::Material_SurfaceDepthPixel;
}

bool NMaterialHlsl::UsesSceneLighting( const FMaterialProgram& Program )
{
	return Program.Domain == EMaterialDomain::Surface && Program.Shading != EMaterialShading::Unlit && Program.bFullSubstrate && !Program.SubstrateSlabs.IsEmpty() && !Program.bCustomHlsl;
}

int32 NMaterialHlsl::GetEnvironmentRegister( const FMaterialProgram& Program )
{
	return Program.Textures.Num();
}

int32 NMaterialHlsl::GetShadowRegister( const FMaterialProgram& Program, const FMaterialHlslFeatures& Features )
{
	return Program.Textures.Num() + ( Features.bEnvironmentMap ? 1 : 0 );
}

// 色と影で同じグラフの命令・入力・資源の配置を生成する。
static FString GeneratePixel( const FMaterialProgram& Program, const FString& CustomBody, const FMaterialHlslFeatures& Features, bool bShadowDepth )
{
	FString Source = "// Generated by the material system. Domain: ";
	Source += NMaterial::ToString( Program.Domain );
	Source += "\n";
	const bool bSurface = Program.Domain == EMaterialDomain::Surface;

	// 宣言: 画像・パラメーター・フレームの定数・入力
	for ( int32 Index = 0; Index < Program.Textures.Num(); ++Index )
	{
		Source += FString::Printf( "Texture2D g_Tex%d : register( t%d );\n", Program.Textures[Index].Register, Program.Textures[Index].Register );
	}
	Source += FString::Printf( "cbuffer cbMaterialParams : register( b%d )\n{\n\tfloat4 g_Params[%d];\n};\n", bSurface ? 6 : 4, Program.Params.Num() > 0 ? Program.Params.Num() : 1 );
	const bool bMasked = bSurface && Program.OpacityCutoff >= 0.0f; // 切り抜き材質だけ専用の入口を生成する。
	if ( bMasked ) { Source += "#define MATERIAL_MASKED\n"; }
	if ( bShadowDepth ) { Source += "#define MATERIAL_CUTOFF " + Literal( Program.OpacityCutoff ) + "\n"; }
	Source += bSurface ? SurfaceDeclarations : FrameDeclarations;
	Source += Helpers;
	const bool bLit = !bShadowDepth && NMaterialHlsl::UsesSceneLighting( Program );
	// 接線・従接線を読むマテリアルも、Substrate の関数（画素の接線を作る関数）を使う
	bool bUsesTangent = false;
	for ( int32 Index = 0; Index < Program.Code.Num(); ++Index )
	{
		const FMaterialInstruction& Instruction = Program.Code[Index];
		bUsesTangent = bUsesTangent || ( Instruction.Op == EMaterialOp::Input && ( Instruction.Sub == static_cast<uint8>( EMaterialInput::WorldTangent ) || Instruction.Sub == static_cast<uint8>( EMaterialInput::WorldBitangent ) ) );
	}
	if ( bLit )
	{
		// 場面の画像は、マテリアルの画像のすぐあとの段に、環境マップ・影のマップの順で置く
		if ( Features.bEnvironmentMap )
		{
			Source += FString::Printf( "Texture2D g_EnvironmentMap : register( t%d );\n#define SUBSTRATE_ENVIRONMENT_MAP g_EnvironmentMap\n", NMaterialHlsl::GetEnvironmentRegister( Program ) );
		}
		if ( Features.bShadowMap )
		{
			Source += FString::Printf( "Texture2D g_ShadowMap : register( t%d );\n", NMaterialHlsl::GetShadowRegister( Program, Features ) );
		}
	}
	if ( bLit || ( bSurface && bUsesTangent && !Program.bCustomHlsl ) )
	{
		Source += NMaterialSubstrate::GetHlslFunctions();
	}
	if ( bLit )
	{
		Source += LightingFunctions( Features );
	}
	if ( Program.Domain == EMaterialDomain::PostProcess )
	{
		Source += DepthHelper;
	}

	if ( Program.bCustomHlsl )
	{
		Source += "\n// ---- the material's own HLSL ----\n";
		Source += CustomBody;
		return Source;
	}

	// 本文: 命令を、1 つずつ、変数にする
	if ( bShadowDepth ) { Source += "\nfloat EvaluateShadowOpacity( VertexOutput i )\n{\n"; }
	else if ( bMasked ) { Source += "\nfloat4 EvaluateMaskedSurface( VertexOutput i )\n{\n"; }
	else { Source += bSurface ? "\nfloat4 main( VertexOutput i ) : SV_Target\n{\n" : "\nfloat4 main( PixelInput i ) : SV_Target\n{\n"; }
	for ( int32 Index = 0; Index < Program.Code.Num(); ++Index )
	{
		const FMaterialInstruction& Instruction = Program.Code[Index];
		FString Expression;
		switch ( Instruction.Op )
		{
		case EMaterialOp::Const:
			Expression = ConstantExpression( Instruction );
			break;
		case EMaterialOp::Param:
			Expression = ParamExpression( Program.Params[Instruction.Slot] );
			break;
		case EMaterialOp::Input:
			Expression = InputExpression( static_cast<EMaterialInput>( Instruction.Sub ), Program.Domain );
			break;
		case EMaterialOp::Unary:
			Expression = UnaryExpression( static_cast<EMaterialUnary>( Instruction.Sub ), Name( Instruction.A ) );
			break;
		case EMaterialOp::Binary:
			Expression = BinaryExpression( static_cast<EMaterialBinary>( Instruction.Sub ), Name( Instruction.A ), Name( Instruction.B ) );
			break;
		case EMaterialOp::Ternary:
		{
			const FString A = Name( Instruction.A );
			const FString B = Name( Instruction.B );
			const FString C = Name( Instruction.C );
			switch ( static_cast<EMaterialTernary>( Instruction.Sub ) )
			{
			case EMaterialTernary::Lerp:
				Expression = "lerp( " + A + ", " + B + ", " + C + " )";
				break;
			case EMaterialTernary::Clamp:
				Expression = "clamp( " + A + ", " + B + ", " + C + " )";
				break;
			default:
				Expression = "smoothstep( " + A + ", " + B + ", " + C + " )";
				break;
			}
			break;
		}
		case EMaterialOp::Splat:
			Expression = FString::Printf( "( %s )%s", TypeName( Instruction.Type ), *Name( Instruction.A ) );
			break;
		case EMaterialOp::Append:
			Expression = FString( TypeName( Instruction.Type ) ) + "( " + Name( Instruction.A ) + ", " + Name( Instruction.B ) + " )";
			break;
		case EMaterialOp::Mask:
		{
			Expression = Name( Instruction.A ) + ".";
			for ( int32 Component = 0; Component < Instruction.NumSwizzle; ++Component )
			{
				Expression += SwizzleLetters[Instruction.Swizzle[Component]];
			}
			break;
		}
		case EMaterialOp::Sample:
			Expression = FString::Printf( "g_Tex%d.Sample( g_Sampler, %s )", Program.Textures[Instruction.Slot].Register, *Name( Instruction.A ) );
			break;
		case EMaterialOp::SceneDepth:
			Expression = FString::Printf( "MatLinearDepth( g_Tex%d.Sample( g_Sampler, %s ).r )", Program.Textures[Instruction.Slot].Register, *Name( Instruction.A ) );
			break;
		case EMaterialOp::Noise:
			Expression = "MatNoise( " + Name( Instruction.A ) + " )";
			break;
		case EMaterialOp::Noise3D:
			Expression = "MatNoise3DFiltered( " + Name( Instruction.A ) + " )";
			break;
		case EMaterialOp::Noise3DNormal:
			Expression = "MatNoise3DNormal( " + Name( Instruction.A ) + ", i.LocalNormal, " + Name( Instruction.B ) + ", g_World, i.Normal )";
			break;
		}
		// Splat は、( float3 )v のように、スカラーからの型の変換。式の中の優先順位が、まぎれないように、括弧で包む
		Source += FString::Printf( "\t%s %s = %s;\n", TypeName( Instruction.Type ), *Name( Index ), *Expression );
	}

	if ( bShadowDepth )
	{
		const FMaterialOutputValue* Opacity = Program.FindOutput( "Opacity" ); // 色と同じグラフ出力を読む。
		Source += "\treturn " + ( Opacity ? Name( Opacity->Value ) : FString( "1.0" ) ) + ";\n";
	}
	else if ( bSurface )
	{
		Source += SurfaceShading( Program );
	}
	else
	{
		auto Out = [&]( const char* Pin ) -> FString
		{
			const FMaterialOutputValue* Output = Program.FindOutput( Pin );
			return Output != nullptr ? Name( Output->Value ) : FString( "1.0" );
		};
		if ( Program.Domain == EMaterialDomain::PostProcess )
		{
			Source += "\treturn float4( " + Out( "Color" ) + ", 1.0 );\n";
		}
		else
		{
			// UI・Particle: 部品・粒子の色（と、不透明度）を掛ける
			Source += "\treturn float4( " + Out( "Color" ) + ", " + Out( "Opacity" ) + " ) * i.Color;\n";
		}
	}
	Source += "}\n";
	if ( bShadowDepth ) { Source += NEngineShaderSources::Material_SurfaceDepthPixel; }
	else if ( bMasked ) { Source += "\nfloat4 main( VertexOutput i ) : SV_Target { return MatMaskedOutput( EvaluateMaskedSurface( i ), " + Literal( Program.OpacityCutoff ) + " ); }\n"; }
	return Source;
}

FString NMaterialHlsl::Generate( const FMaterialProgram& Program, const FString& CustomBody, const FMaterialHlslFeatures& Features )
{
	return GeneratePixel( Program, CustomBody, Features, false );
}

FString NMaterialHlsl::GetMaskedSurfaceDepthPixelSource( const FMaterialProgram& Program )
{
	if ( !Program.bValid || Program.Domain != EMaterialDomain::Surface || Program.bCustomHlsl || Program.OpacityCutoff < 0.0f ) { return FString(); }
	return GeneratePixel( Program, FString(), FMaterialHlslFeatures(), true );
}
