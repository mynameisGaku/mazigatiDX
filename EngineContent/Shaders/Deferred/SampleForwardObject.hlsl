// SPDX-License-Identifier: MIT
// サンプル Deferred の Forward 経路: 物（SampleCommon.hlsli と SampleGeometry.hlsli のあとに置く）。地の色と自分で光る色は、物の定数。
float4 main( VertexOutput i ) : SV_Target0
{
	float3 normal = normalize( i.Normal );
	return float4( ShadeView( g_Albedo.rgb, normal, g_Emission.rgb, i.World, i.ViewDepth / g_CameraPosition.w ), 1 );
}
