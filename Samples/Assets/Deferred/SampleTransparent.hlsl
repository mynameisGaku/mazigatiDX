// SPDX-License-Identifier: MIT
// サンプル Deferred の Deferred 経路: 半透明の物を、ライティングのあとの場面の色へ重ねるピクセルシェーダー。
// GBufferRead.hlsli・SampleCommon.hlsli・SampleGeometry.hlsli のあとに置く。
// 半透明の物は、光を当てたあとの場面の色へ、Forward で（描きながら光を計算して）アルファ合成で重ねる。
// 奥行きバッファは、不透明の物を描いた G-buffer の描き先が持っていて、場面の色の描き先とは別なので、
// G-buffer の深度を読んで、不透明の物より奥の画素を捨てる（Forward 経路のハードウェアの奥行きの比べと同じ結果になる）。
// 画面の位置は SV_Position から求める（場面の描画先の大きさは g_Parameters.zw）。不透明度は、物の定数の g_Albedo.w。
float4 main( VertexOutput i ) : SV_Target0
{
	float2 uv = i.Position.xy / g_Parameters.zw;
	float opaqueDepth = g_GBufferDepth.Sample( g_GBufferSampler, uv ).r;
	float depth = i.ViewDepth / g_CameraPosition.w;
	clip( opaqueDepth - depth );
	float3 normal = normalize( i.Normal );
	return float4( ShadeView( g_Albedo.rgb, normal, g_Emission.rgb, i.World, depth, i.Position.xy ), g_Albedo.w );
}
