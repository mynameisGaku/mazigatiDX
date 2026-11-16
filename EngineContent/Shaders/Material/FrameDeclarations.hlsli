// SPDX-License-Identifier: MIT

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
	float4 Particle : TEXCOORD1;   // 粒子ごとの数（x = 寿命の位置、y = 生まれてからの秒、z = 粒子ごとの 0〜1 の数、w = 速さ）。矩形に描くときは 0
};
