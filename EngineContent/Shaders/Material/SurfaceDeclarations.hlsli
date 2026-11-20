// SPDX-License-Identifier: MIT

cbuffer cbScene : register( b4 )
{
	row_major float4x4 g_ViewProjection;
	float4 g_LightDirection;
	float4 g_CameraPosition;
	float4 g_Time;
	float4 g_LightColor;
	float4 g_SkyColor;
	float4 g_GroundColor;
	float4 g_AmbientColor;
	float4 g_Environment;
	float4 g_PointLightPositions[4];
	float4 g_PointLightColors[4];
	row_major float4x4 g_ShadowViewProjection;
	float4 g_ShadowOptions;
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

#ifdef MATERIAL_MASKED
// 不透明度がしきい値以下の画素は色・深度を更新しない。
void MatClipOpacity( float Opacity, float Cutoff )
{
	clip( Opacity > Cutoff ? 1.0 : -1.0 );
}
float4 MatMaskedOutput( float4 Color, float Cutoff )
{
	MatClipOpacity( Color.a, Cutoff );
	return float4( Color.rgb, 1.0 );
}
#endif
