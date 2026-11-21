// SPDX-License-Identifier: MIT
// サンプル Deferred のライティングのパス。GBufferRead.hlsli と SampleCommon.hlsli のあとに置く。
// ShaderRect の組み込みの頂点シェーダーが渡すもの（位置・色・画像の座標）を受けて、G-buffer の画素ごとに、Forward と同じ式で色を決める。
struct PixelInput
{
	float4 Position : SV_POSITION;
	float4 Color : COLOR0;
	float2 Uv : TEXCOORD0;
};

float4 main( PixelInput i ) : SV_Target
{
	GBufferSample gbuffer = LoadGBuffer( i.Uv );
	if ( !gbuffer.bCovered )
	{
		return float4( SkyColor, 1 );
	}
	float normalizedDepth = g_GBufferDepth.Sample( g_GBufferSampler, i.Uv ).r;
	return float4( ShadeView( gbuffer.Albedo, gbuffer.Normal, gbuffer.Emission, gbuffer.World, normalizedDepth ), 1 );
}
