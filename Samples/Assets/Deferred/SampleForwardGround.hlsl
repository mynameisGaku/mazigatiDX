// SPDX-License-Identifier: MIT
// サンプル Deferred の Forward 経路: 地面（SampleCommon.hlsli と SampleGeometry.hlsli のあとに置く）。
float4 main( VertexOutput i ) : SV_Target0
{
	float3 albedo;
	float3 emission;
	GroundSurface( i.World, albedo, emission );
	return float4( ShadeView( albedo, float3( 0, 1, 0 ), emission, i.World, i.ViewDepth / g_CameraPosition.w, i.Position.xy ), 1 );
}
