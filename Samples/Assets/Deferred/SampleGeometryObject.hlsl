// SPDX-License-Identifier: MIT
// サンプル Deferred のジオメトリのパス: 物。GBufferWrite.hlsli・SampleCommon.hlsli・SampleGeometry.hlsli のあとに置く。
// 物の面の情報を、G-buffer の 4 つの出力へ書く（光は、あとのライティングのパスが当てる）。
GBufferOutput main( VertexOutput i )
{
	return PackGBuffer( g_Albedo.rgb, i.Normal, g_Emission.rgb, i.ViewDepth / g_CameraPosition.w );
}
