// SPDX-License-Identifier: MIT
// サンプル Deferred の、場面の物の入出力と、地面の面の色。SampleCommon.hlsli のあとに置く。
// cbObject の並びは、Samples/Source/Deferred/DeferredSources.h の FObjectConstants と同じ。
// 頂点シェーダーの入力は、FShaderVertex3D の並びのとおりに、先頭から飛ばさずに宣言する。
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
	float ViewDepth : TEXCOORD2;
};

// 地面: 暗い石畳と、脈打って光る溝（ネオンの線。明るさは 1 を超える）
void GroundSurface( float3 world, out float3 albedo, out float3 emission )
{
	float2 cell = world.xz / 4.0;
	float2 edge = abs( frac( cell ) - 0.5 );
	float groove = smoothstep( 0.47, 0.495, max( edge.x, edge.y ) );
	float pulse = 0.6 + 0.4 * sin( length( world.xz ) * 0.5 - g_Parameters.x * 2.0 );
	albedo = float3( 0.30, 0.31, 0.36 ) * ( 0.85 + 0.15 * frac( sin( dot( floor( cell ), float2( 12.9898, 78.233 ) ) ) * 43758.5453 ) );
	float fade = saturate( 1.3 - length( world.xz ) / 36.0 );
	emission = float3( 0.2, 1.2, 2.4 ) * groove * pulse * fade;
}
