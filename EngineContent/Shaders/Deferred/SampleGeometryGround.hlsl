// SPDX-License-Identifier: MIT
// サンプル Deferred のジオメトリのパス: 地面。GBufferWrite.hlsli・SampleCommon.hlsli・SampleGeometry.hlsli のあとに置く。
GBufferOutput main( VertexOutput i )
{
	float3 albedo;
	float3 emission;
	GroundSurface( i.World, albedo, emission );
	return PackGBuffer( albedo, float3( 0, 1, 0 ), emission, i.ViewDepth / g_CameraPosition.w );
}
