// SPDX-License-Identifier: MIT
// サンプル Deferred の頂点シェーダー（SampleCommon.hlsli と SampleGeometry.hlsli のあとに置く）。
// ワールド座標に直し、クリップ座標にする。クリップ座標の w は、カメラの前向きの奥行き（深度に使う）。
VertexOutput main( VertexInput i )
{
	VertexOutput o;
	float4 world = mul( float4( i.Position, 1 ), g_World );
	o.Position = mul( world, g_ViewProjection );
	o.Normal = mul( i.Normal, (float3x3)g_World );
	o.World = world.xyz;
	o.ViewDepth = o.Position.w;
	return o;
}
