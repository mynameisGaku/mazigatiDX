// SPDX-License-Identifier: MIT
#include "MaterialHlsl.h"

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
		case EMaterialInput::WorldNormal:
			return "normalize( i.Normal )";
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
	const char* Helpers = R"HLSL(
#define MatMod( a, b ) ( ( a ) - ( b ) * floor( ( a ) / ( b ) ) )

float MatHash( float2 p )
{
	p = frac( p * float2( 123.34, 456.21 ) );
	p += dot( p, p + 45.32 );
	return frac( p.x * p.y );
}

float MatNoise( float2 p )
{
	float2 i = floor( p );
	float2 f = frac( p );
	float a = MatHash( i );
	float b = MatHash( i + float2( 1.0, 0.0 ) );
	float c = MatHash( i + float2( 0.0, 1.0 ) );
	float d = MatHash( i + float2( 1.0, 1.0 ) );
	float2 u = f * f * ( 3.0 - 2.0 * f );
	return lerp( lerp( a, b, u.x ), lerp( c, d, u.x ), u.y );
}
)HLSL";

	const char* FrameDeclarations = R"HLSL(
cbuffer cbMaterialFrame : register( b5 )
{
	float4 g_Time;
	float4 g_Rect;
	float4 g_State;
};

SamplerState g_Sampler : register( s0 );

struct PixelInput
{
	float4 Position : SV_POSITION;
	float4 Color : COLOR0;
	float2 Uv : TEXCOORD0;
};
)HLSL";

	const char* DepthHelper = R"HLSL(
float MatLinearDepth( float d )
{
	return g_Time.z * g_Time.w / ( g_Time.w - d * ( g_Time.w - g_Time.z ) );
}
)HLSL";

	const char* SurfaceDeclarations = R"HLSL(
cbuffer cbScene : register( b4 )
{
	row_major float4x4 g_ViewProjection;
	float4 g_LightDirection;
	float4 g_CameraPosition;
	float4 g_Time;
};

cbuffer cbObject : register( b5 )
{
	row_major float4x4 g_World;
	float4 g_Color;
	float4 g_Options;
};

SamplerState g_Sampler : register( s0 );

struct VertexOutput
{
	float4 Position : SV_POSITION;
	float3 Normal : TEXCOORD0;
	float3 World : TEXCOORD1;
	float2 Uv : TEXCOORD2;
	float4 Color : TEXCOORD3;
};
)HLSL";

	// Surface の陰影の本文（標準の光。Substrate は、簡略版: 拡散・鏡面・クリアコート・発光）
	FString SurfaceShading( const FMaterialProgram& Program )
	{
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
		Text += "\tfloat3 N = normalize( i.Normal );\n";
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
	return FString( SurfaceDeclarations ) + R"HLSL(
struct VertexInput
{
	float3 Position : POSITION;
	float4 SubPosition : POSITION1;
	float3 Normal : NORMAL;
	float3 Tangent : TANGENT;
	float3 Binormal : BINORMAL;
	float4 Diffuse : COLOR0;
	float4 Specular : COLOR1;
	float2 Uv : TEXCOORD0;
	float2 SubUv : TEXCOORD1;
};

VertexOutput main( VertexInput i )
{
	VertexOutput o;
	float4 world = mul( float4( i.Position, 1 ), g_World );
	o.Position = mul( world, g_ViewProjection );
	o.Normal = mul( i.Normal, ( float3x3 )g_World );
	o.World = world.xyz;
	o.Uv = i.Uv;
	o.Color = i.Diffuse * g_Color;
	return o;
}
)HLSL";
}

FString NMaterialHlsl::Generate( const FMaterialProgram& Program, const FString& CustomBody )
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
	Source += bSurface ? SurfaceDeclarations : FrameDeclarations;
	Source += Helpers;
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
	Source += bSurface ? "\nfloat4 main( VertexOutput i ) : SV_Target\n{\n" : "\nfloat4 main( PixelInput i ) : SV_Target\n{\n";
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
		}
		// Splat は、( float3 )v のように、スカラーからの型の変換。式の中の優先順位が、まぎれないように、括弧で包む
		Source += FString::Printf( "\t%s %s = %s;\n", TypeName( Instruction.Type ), *Name( Index ), *Expression );
	}

	if ( bSurface )
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
	return Source;
}
