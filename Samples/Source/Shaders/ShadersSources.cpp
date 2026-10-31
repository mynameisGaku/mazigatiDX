#include "ShadersSources.h"

#include "Library/Core/Collection/String.h"
#include "Library/Core/Log/Log.h"

DEFINE_LOG_CATEGORY_STATIC( LogShadersSample, Log )

namespace
{
	// 頂点シェーダーの入力: FShaderVertex3D の並びのとおりに、先頭から飛ばさずに宣言する
	// 頂点シェーダーの出力と、ピクセルシェーダーの入力は、同じ並び（物と地面で共通）
	const char* Common = R"HLSL(
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

struct VertexOutput
{
	float4 Position : SV_POSITION;
	float3 Normal : TEXCOORD0;
	float3 World : TEXCOORD1;
	float2 Uv : TEXCOORD2;
};
)HLSL";

	// 頂点シェーダー: ワールド座標に直し（うねりがあれば高さを波打たせ）、クリップ座標にする
	const char* ObjectVertexBody = R"HLSL(
VertexOutput main( VertexInput i )
{
	VertexOutput o;
	float4 world = mul( float4( i.Position, 1 ), g_World );
	world.y += sin( world.x * 1.5 + world.z * 1.1 + g_Time.x * 3.0 ) * g_Options.x;
	o.Position = mul( world, g_ViewProjection );
	o.Normal = mul( i.Normal, (float3x3)g_World );
	o.World = world.xyz;
	o.Uv = i.Uv;
	return o;
}
)HLSL";

	// トゥーン: 光の当たり方を 3 段に分けて、縁を明るくする
	const char* ToonPixelBody = R"HLSL(
float4 main( VertexOutput i ) : SV_Target
{
	float3 normal = normalize( i.Normal );
	float lighting = dot( normal, -normalize( g_LightDirection.xyz ) );
	float band = lighting > 0.55 ? 1.0 : ( lighting > 0.1 ? 0.62 : 0.32 );
	float3 toCamera = normalize( g_CameraPosition.xyz - i.World );
	float rim = pow( 1.0 - saturate( dot( normal, toCamera ) ), 3.0 );
	return float4( g_Color.rgb * band + rim * 0.4, 1 );
}
)HLSL";

	// 法線の色: 向きを色にする
	const char* NormalPixelBody = R"HLSL(
float4 main( VertexOutput i ) : SV_Target
{
	return float4( normalize( i.Normal ) * 0.5 + 0.5, 1 );
}
)HLSL";

	// 奥行き: カメラから遠いほど暗い（SV_Position.z は、頂点シェーダーが出したクリップ座標の z そのまま。0..1）
	const char* DepthPixelBody = R"HLSL(
float4 main( VertexOutput i ) : SV_Target
{
	float nearZ = g_Time.y;
	float farZ = g_Time.z;
	float distance = nearZ * farZ / ( farZ - i.Position.z * ( farZ - nearZ ) );
	float value = saturate( 1.0 - distance / 70.0 );
	return float4( value, value, value, 1 );
}
)HLSL";

	// 波紋: 中心から広がる輪。遠くは暗い
	const char* RipplePixelBody = R"HLSL(
float4 main( VertexOutput i ) : SV_Target
{
	float distance = length( i.World.xz );
	float wave = sin( distance * 2.4 - g_Time.x * 3.0 ) * 0.5 + 0.5;
	float3 color = lerp( float3( 0.08, 0.22, 0.42 ), float3( 0.28, 0.62, 0.82 ), wave );
	float fade = saturate( 1.25 - distance / 38.0 );
	return float4( color * fade, 1 );
}
)HLSL";

	// 市松模様
	const char* CheckerPixelBody = R"HLSL(
float4 main( VertexOutput i ) : SV_Target
{
	float parity = fmod( floor( i.World.x * 0.5 ) + floor( i.World.z * 0.5 ), 2.0 );
	float3 color = lerp( float3( 0.22, 0.24, 0.30 ), float3( 0.62, 0.64, 0.70 ), abs( parity ) );
	float fade = saturate( 1.25 - length( i.World.xz ) / 38.0 );
	return float4( color * fade, 1 );
}
)HLSL";

	// ポストエフェクトの入力: ShaderRect の組み込みの頂点シェーダーが渡すもの（位置・色・画像の座標）
	const char* PostCommon = R"HLSL(
cbuffer cbPost : register( b4 )
{
	float4 g_Parameters;
};

Texture2D g_Scene : register( t0 );
SamplerState g_Sampler : register( s0 );

struct PixelInput
{
	float4 Position : SV_POSITION;
	float4 Color : COLOR0;
	float2 Uv : TEXCOORD0;
};
)HLSL";

	const char* PostBodies[] = {
		// なし
		R"HLSL(
float4 main( PixelInput i ) : SV_Target
{
	return g_Scene.Sample( g_Sampler, i.Uv );
}
)HLSL",
		// 白黒
		R"HLSL(
float4 main( PixelInput i ) : SV_Target
{
	float3 color = g_Scene.Sample( g_Sampler, i.Uv ).rgb;
	float gray = dot( color, float3( 0.299, 0.587, 0.114 ) );
	return float4( gray, gray, gray, 1 );
}
)HLSL",
		// 周りを暗く
		R"HLSL(
float4 main( PixelInput i ) : SV_Target
{
	float3 color = g_Scene.Sample( g_Sampler, i.Uv ).rgb;
	float2 center = i.Uv - 0.5;
	float darkness = saturate( 1.0 - dot( center, center ) * 2.6 );
	return float4( color * darkness, 1 );
}
)HLSL",
		// モザイク
		R"HLSL(
float4 main( PixelInput i ) : SV_Target
{
	float2 blocks = float2( 128.0, 64.0 );
	float2 uv = ( floor( i.Uv * blocks ) + 0.5 ) / blocks;
	return g_Scene.Sample( g_Sampler, uv );
}
)HLSL",
		// ゆがみ
		R"HLSL(
float4 main( PixelInput i ) : SV_Target
{
	float2 uv = i.Uv;
	uv.x += sin( uv.y * 36.0 + g_Parameters.x * 3.0 ) * 0.008;
	uv.y += sin( uv.x * 24.0 + g_Parameters.x * 2.0 ) * 0.006;
	return g_Scene.Sample( g_Sampler, uv );
}
)HLSL",
	};

	// 1 つコンパイルする。失敗したら、理由をログに出して、nullptr を返す
	TSharedPtr<CShader> CompileOne( EShaderStage Stage, const FString& Source, const char* Name )
	{
		FString Message;
		TSharedPtr<CShader> Shader = CShader::Compile( Stage, Source, "main", &Message );
		if ( !Shader.IsValid() )
		{
			LOG( LogShadersSample, Error, "Cannot compile the shader %s: %s", Name, Message.GetData() );
		}
		return Shader;
	}
}

bool NShadersSources::CompileAll( FShadersDemoState& State )
{
	State.ObjectVertex = CompileOne( EShaderStage::Vertex, FString( Common ) + ObjectVertexBody, "object vertex" );
	const FString ObjectPixelCommon = FString( Common );
	State.ObjectPixels[0] = CompileOne( EShaderStage::Pixel, ObjectPixelCommon + ToonPixelBody, "toon" );
	State.ObjectPixels[1] = CompileOne( EShaderStage::Pixel, ObjectPixelCommon + NormalPixelBody, "normal" );
	State.ObjectPixels[2] = CompileOne( EShaderStage::Pixel, ObjectPixelCommon + DepthPixelBody, "depth" );
	// うねりは、頂点シェーダーが定数（Options.x）で行う。色の付け方はトゥーンと同じ
	State.ObjectPixels[3] = State.ObjectPixels[0];
	State.GroundPixels[0] = CompileOne( EShaderStage::Pixel, ObjectPixelCommon + RipplePixelBody, "ripple" );
	State.GroundPixels[1] = CompileOne( EShaderStage::Pixel, ObjectPixelCommon + CheckerPixelBody, "checker" );
	bool bOk = State.ObjectVertex.IsValid();
	for ( int32 Mode = 0; Mode < FShadersDemoState::NumObjectModes; ++Mode )
	{
		bOk = bOk && State.ObjectPixels[Mode].IsValid();
	}
	for ( int32 Mode = 0; Mode < FShadersDemoState::NumGroundModes; ++Mode )
	{
		bOk = bOk && State.GroundPixels[Mode].IsValid();
	}
	for ( int32 Mode = 0; Mode < FShadersDemoState::NumPostModes; ++Mode )
	{
		State.PostPixels[Mode] = CompileOne( EShaderStage::Pixel, FString( PostCommon ) + PostBodies[Mode], "post effect" );
		bOk = bOk && State.PostPixels[Mode].IsValid();
	}
	return bOk;
}
