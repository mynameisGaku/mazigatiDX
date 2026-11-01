#include "PostEffectsSources.h"

#include "Library/Core/Collection/String.h"
#include "Library/Core/Log/Log.h"

DEFINE_LOG_CATEGORY_STATIC( LogPostEffectsSample, Log )

namespace
{
	// 場面のシェーダーで共通の定数と、頂点の入出力
	// 頂点シェーダーの入力は、FShaderVertex3D の並びのとおりに、先頭から飛ばさずに宣言する
	const char* SceneCommon = R"HLSL(
cbuffer cbScene : register( b4 )
{
	row_major float4x4 g_ViewProjection;
	float4 g_LightDirection;
	float4 g_CameraPosition;
	float4 g_Time;
	float4 g_OrbPositions[6];
	float4 g_OrbColors[6];
};

cbuffer cbObject : register( b5 )
{
	row_major float4x4 g_World;
	float4 g_Albedo;
	float4 g_Emission;
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
};

// 2 つの出力: 色（いまの描画先）と、深度（出力 1 の描画先）
struct SceneOutput
{
	float4 Color : SV_Target0;
	float4 Depth : SV_Target1;
};

// 夜の光: 空の明るさ + 月の光 + 光る玉（近いほど強い点の光）。光の裏側も真っ黒にしないよう、月の光は回り込ませる（ラップ）
float3 Lighting( float3 normal, float3 world )
{
	float3 light = float3( 0.10, 0.11, 0.17 );
	light += float3( 0.24, 0.27, 0.40 ) * saturate( ( dot( normal, -g_LightDirection.xyz ) + 0.5 ) / 1.5 );
	for ( int k = 0; k < 6; ++k )
	{
		float3 toOrb = g_OrbPositions[k].xyz - world;
		float distanceSquared = dot( toOrb, toOrb );
		light += g_OrbColors[k].rgb * saturate( dot( normal, toOrb * rsqrt( distanceSquared ) ) ) / ( 1.0 + distanceSquared * 0.12 );
	}
	return light;
}

// 色と深度をまとめる。深度は、カメラからの距離を Far で割ったもの（0〜1）
SceneOutput MakeOutput( float3 color, float3 world )
{
	SceneOutput o;
	o.Color = float4( color, 1 );
	float depth = distance( g_CameraPosition.xyz, world ) / g_Time.y;
	o.Depth = float4( depth, 0, 0, 1 );
	return o;
}
)HLSL";

	// 頂点シェーダー: ワールド座標に直し、クリップ座標にする
	const char* SceneVertexBody = R"HLSL(
VertexOutput main( VertexInput i )
{
	VertexOutput o;
	float4 world = mul( float4( i.Position, 1 ), g_World );
	o.Position = mul( world, g_ViewProjection );
	o.Normal = mul( i.Normal, (float3x3)g_World );
	o.World = world.xyz;
	return o;
}
)HLSL";

	// 物: 地の色 x 光 + 自分で光る色（光る玉は、地の色が黒で、光る色だけ）
	const char* ObjectPixelBody = R"HLSL(
SceneOutput main( VertexOutput i )
{
	float3 normal = normalize( i.Normal );
	float3 color = g_Albedo.rgb * Lighting( normal, i.World ) + g_Emission.rgb;
	return MakeOutput( color, i.World );
}
)HLSL";

	// 地面: 暗い石畳と、脈打って光る溝（ネオンの線。明るさは 1 を超える）
	const char* GroundPixelBody = R"HLSL(
SceneOutput main( VertexOutput i )
{
	float2 cell = i.World.xz / 4.0;
	float2 edge = abs( frac( cell ) - 0.5 );
	float groove = smoothstep( 0.47, 0.495, max( edge.x, edge.y ) );
	float pulse = 0.6 + 0.4 * sin( length( i.World.xz ) * 0.5 - g_Time.x * 2.0 );
	float3 albedo = float3( 0.30, 0.31, 0.36 ) * ( 0.85 + 0.15 * frac( sin( dot( floor( cell ), float2( 12.9898, 78.233 ) ) ) * 43758.5453 ) );
	float3 glow = float3( 0.2, 1.2, 2.4 ) * groove * pulse;
	float fade = saturate( 1.3 - length( i.World.xz ) / 36.0 );
	float3 color = albedo * Lighting( float3( 0, 1, 0 ), i.World ) + glow * fade;
	return MakeOutput( color, i.World );
}
)HLSL";

	// ポストエフェクトの入力: ShaderRect の組み込みの頂点シェーダーが渡すもの（位置・色・画像の座標）
	const char* PostCommon = R"HLSL(
cbuffer cbPost : register( b4 )
{
	float4 g_Parameters;
	float4 g_Sampling;
	float4 g_Modes;
	float4 g_FogColor;
};

Texture2D g_Texture0 : register( t0 );
Texture2D g_Texture1 : register( t1 );
Texture2D g_Texture2 : register( t2 );
SamplerState g_Sampler : register( s0 );

struct PixelInput
{
	float4 Position : SV_POSITION;
	float4 Color : COLOR0;
	float2 Uv : TEXCOORD0;
};
)HLSL";

	// 明るい所を抜き出す（半分の大きさへ。4 点の平均で縮める）。境目を超えた分だけを残し、なめらかに立ち上げる
	const char* BrightPixelBody = R"HLSL(
float4 main( PixelInput i ) : SV_Target
{
	float2 offset = g_Sampling.xy * 0.5;
	float3 color = g_Texture0.Sample( g_Sampler, i.Uv + float2( -offset.x, -offset.y ) ).rgb;
	color += g_Texture0.Sample( g_Sampler, i.Uv + float2( offset.x, -offset.y ) ).rgb;
	color += g_Texture0.Sample( g_Sampler, i.Uv + float2( -offset.x, offset.y ) ).rgb;
	color += g_Texture0.Sample( g_Sampler, i.Uv + float2( offset.x, offset.y ) ).rgb;
	color *= 0.25 * g_Parameters.x;
	float brightness = max( color.r, max( color.g, color.b ) );
	float threshold = g_Parameters.y;
	float knee = threshold * 0.5;
	float soft = clamp( brightness - threshold + knee, 0.0, 2.0 * knee );
	soft = soft * soft / ( 4.0 * knee + 1e-4 );
	float contribution = max( soft, brightness - threshold ) / max( brightness, 1e-4 );
	return float4( color * contribution, 1 );
}
)HLSL";

	// ぼかす（9 点のガウス。向きと広がりは g_Sampling.zw）
	const char* BlurPixelBody = R"HLSL(
float4 main( PixelInput i ) : SV_Target
{
	const float weights[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };
	float2 step = g_Sampling.xy * g_Sampling.zw;
	float3 color = g_Texture0.Sample( g_Sampler, i.Uv ).rgb * weights[0];
	for ( int k = 1; k < 5; ++k )
	{
		color += g_Texture0.Sample( g_Sampler, i.Uv + step * k ).rgb * weights[k];
		color += g_Texture0.Sample( g_Sampler, i.Uv - step * k ).rgb * weights[k];
	}
	return float4( color, 1 );
}
)HLSL";

	// 合成: 場面の色（t0）+ ブルーム（t1）に、深度（t2）で霧をかけ、露出を掛けて、トーンマップで 0〜1 に収める
	const char* CompositePixelBody = R"HLSL(
float3 ToneMapAces( float3 x )
{
	return saturate( ( x * ( 2.51 * x + 0.03 ) ) / ( x * ( 2.43 * x + 0.59 ) + 0.14 ) );
}

float3 ToneMapReinhard( float3 x )
{
	return x / ( 1.0 + x );
}

float4 main( PixelInput i ) : SV_Target
{
	float3 scene = g_Texture0.Sample( g_Sampler, i.Uv ).rgb;
	float3 bloom = g_Texture1.Sample( g_Sampler, i.Uv ).rgb;
	float depth = g_Texture2.Sample( g_Sampler, i.Uv ).r;
	float exposure = g_Parameters.x;
	int mode = (int)g_Modes.x;

	// 深度: 近いほど白い
	if ( mode == 3 )
	{
		float value = 1.0 - saturate( depth * 2.5 );
		return float4( value, value, value, 1 );
	}
	// ブルームだけ
	if ( mode == 2 )
	{
		return float4( ToneMapAces( bloom * g_Parameters.z * exposure ), 1 );
	}
	// 8 ビット相当: 露出を掛けて、1 で切るだけ（ブルームも霧もトーンマップもない）
	if ( mode == 1 )
	{
		return float4( saturate( scene * exposure ), 1 );
	}

	float3 color = scene;
	if ( g_Modes.z > 0.5 )
	{
		color += bloom * g_Parameters.z;
	}
	if ( g_Modes.w > 0.5 )
	{
		float distanceToCamera = depth * g_FogColor.w;
		float fog = 1.0 - exp( -distanceToCamera * g_Parameters.w );
		color = lerp( color, g_FogColor.rgb, saturate( fog ) );
	}
	color *= exposure;
	int toneMap = (int)g_Modes.y;
	if ( toneMap == 0 )
	{
		color = ToneMapAces( color );
	}
	else if ( toneMap == 1 )
	{
		color = ToneMapReinhard( color );
	}
	return float4( saturate( color ), 1 );
}
)HLSL";

	// 1 つコンパイルする。失敗したら、理由をログに出して、nullptr を返す
	TSharedPtr<CShader> CompileOne( EShaderStage Stage, const FString& Source, const char* Name )
	{
		FString Message;
		TSharedPtr<CShader> Shader = CShader::Compile( Stage, Source, "main", &Message );
		if ( !Shader.IsValid() )
		{
			LOG( LogPostEffectsSample, Error, "Cannot compile the shader %s: %s", Name, Message.GetData() );
		}
		return Shader;
	}
}

bool NPostEffectsSources::CompileAll( FPostEffectsDemoState& State )
{
	State.SceneVertex = CompileOne( EShaderStage::Vertex, FString( SceneCommon ) + SceneVertexBody, "scene vertex" );
	State.ObjectPixel = CompileOne( EShaderStage::Pixel, FString( SceneCommon ) + ObjectPixelBody, "object" );
	State.GroundPixel = CompileOne( EShaderStage::Pixel, FString( SceneCommon ) + GroundPixelBody, "ground" );
	State.BrightPixel = CompileOne( EShaderStage::Pixel, FString( PostCommon ) + BrightPixelBody, "bright pass" );
	State.BlurPixel = CompileOne( EShaderStage::Pixel, FString( PostCommon ) + BlurPixelBody, "blur" );
	State.CompositePixel = CompileOne( EShaderStage::Pixel, FString( PostCommon ) + CompositePixelBody, "composite" );
	return State.SceneVertex.IsValid() && State.ObjectPixel.IsValid() && State.GroundPixel.IsValid() && State.BrightPixel.IsValid() && State.BlurPixel.IsValid() && State.CompositePixel.IsValid();
}
