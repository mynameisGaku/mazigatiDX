// SPDX-License-Identifier: MIT
// G-buffer を読む（ライティングのパス。Library/Framework/Render/Deferred/GBuffer.h の UseForLighting が、画像の段 0〜3 に置く）。
// 画像は最近傍で読む（CGBuffer::UseForLighting が設定する）。四角形は、G-buffer と同じ大きさの描画先の全体へ描くこと。
Texture2D g_GBufferAlbedo : register( t0 );
Texture2D g_GBufferNormal : register( t1 );
Texture2D g_GBufferDepth : register( t2 );
Texture2D g_GBufferEmission : register( t3 );
SamplerState g_GBufferSampler : register( s0 );

// FGBufferView（Library/Framework/Render/Deferred/GBufferView.h）。スロットは CGBuffer::ViewConstantSlot と同じ 6
cbuffer cbGBufferView : register( b6 )
{
	float4 g_GBufferCameraPosition; // xyz: カメラの位置、w: Far
	float4 g_GBufferRayOrigin;      // xyz: 画面の左上（uv = 0, 0）の、カメラから Far の面の点までのベクトル
	float4 g_GBufferRayU;           // xyz: uv.x が 0 から 1 へ変わるときの、ベクトルの増分
	float4 g_GBufferRayV;           // xyz: uv.y が 0 から 1 へ変わるときの、ベクトルの増分
};

// 1 画素ぶんの面の情報
struct GBufferSample
{
	bool bCovered;    // 何かが描かれた画素か（深度が 1 より小さい）。false なら、ほかの値は使わない
	float3 Albedo;
	float3 Normal;    // 世界の法線（長さ 1）
	float3 Emission;
	float3 World;     // 世界の位置（深度から求めたもの）
};

// 画面の位置 uv（左上が 0, 0）を通る、カメラから Far の面の点までのベクトル
float3 GBufferRayToFarPlane( float2 uv )
{
	return g_GBufferRayOrigin.xyz + g_GBufferRayU.xyz * uv.x + g_GBufferRayV.xyz * uv.y;
}

// 画面の位置 uv に見える、深度 normalizedDepth（カメラの前向きの奥行き / Far）の点の、世界の位置
float3 ReconstructWorldPosition( float2 uv, float normalizedDepth )
{
	return g_GBufferCameraPosition.xyz + GBufferRayToFarPlane( uv ) * normalizedDepth;
}

GBufferSample LoadGBuffer( float2 uv )
{
	GBufferSample gbuffer;
	float normalizedDepth = g_GBufferDepth.Sample( g_GBufferSampler, uv ).r;
	gbuffer.bCovered = normalizedDepth < 1.0;
	gbuffer.Albedo = g_GBufferAlbedo.Sample( g_GBufferSampler, uv ).rgb;
	gbuffer.Normal = normalize( g_GBufferNormal.Sample( g_GBufferSampler, uv ).xyz );
	gbuffer.Emission = g_GBufferEmission.Sample( g_GBufferSampler, uv ).rgb;
	gbuffer.World = ReconstructWorldPosition( uv, normalizedDepth );
	return gbuffer;
}
