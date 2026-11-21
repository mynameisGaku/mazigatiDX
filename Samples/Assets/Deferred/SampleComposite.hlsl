// SPDX-License-Identifier: MIT
// サンプル Deferred の画面への合成（単独で完結する）。場面の色（t0）に、仕上がりのときだけ、露出を掛けて、トーンマップ（ACES）で 0〜1 に収める。
// ほかの見せ方は、値をそのまま見せる。cbComposite の並びは、Samples/Source/Deferred/DeferredSources.h の FCompositeConstants と同じ。
cbuffer cbComposite : register( b4 )
{
	float4 g_Parameters; // x: 露出、y: トーンマップを掛けるか（仕上がりのときだけ 1）
};

Texture2D g_Texture0 : register( t0 );
SamplerState g_Sampler : register( s0 );

struct PixelInput
{
	float4 Position : SV_POSITION;
	float4 Color : COLOR0;
	float2 Uv : TEXCOORD0;
};

float3 ToneMapAces( float3 x )
{
	return saturate( ( x * ( 2.51 * x + 0.03 ) ) / ( x * ( 2.43 * x + 0.59 ) + 0.14 ) );
}

float4 main( PixelInput i ) : SV_Target
{
	float3 color = g_Texture0.Sample( g_Sampler, i.Uv ).rgb;
	if ( g_Parameters.y > 0.5 )
	{
		color = ToneMapAces( color * g_Parameters.x );
	}
	return float4( saturate( color ), 1 );
}
