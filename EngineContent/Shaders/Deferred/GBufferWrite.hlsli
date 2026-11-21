// SPDX-License-Identifier: MIT
// G-buffer へ書く（Library/Framework/Render/Deferred/GBuffer.h の EGBufferTarget と、同じ並びの 4 つの出力）。
// ジオメトリのパスのピクセルシェーダーが、PackGBuffer の結果をそのまま返す。
//   SV_Target0: 地の色 rgb（8 ビット）
//   SV_Target1: 世界の法線 xyz（16 ビットの浮動小数点。長さ 1）
//   SV_Target2: 深度 = カメラの前向きの奥行き / Far（32 ビットの浮動小数点 1 チャンネル。何も描かれていない画素は 1）
//   SV_Target3: 自分で光る色 rgb（16 ビットの浮動小数点。1 を超えてよい）
struct GBufferOutput
{
	float4 Albedo : SV_Target0;
	float4 Normal : SV_Target1;
	float4 Depth : SV_Target2;
	float4 Emission : SV_Target3;
};

// albedo: 地の色。normal: 世界の法線（長さは問わない）。emission: 自分で光る色。
// normalizedDepth: 面のカメラの前向きの奥行きを Far で割った値。頂点シェーダーが、クリップ座標の w（= 奥行き）を渡して補間すると、画素ごとの値になる
GBufferOutput PackGBuffer( float3 albedo, float3 normal, float3 emission, float normalizedDepth )
{
	GBufferOutput output;
	output.Albedo = float4( albedo, 1.0 );
	output.Normal = float4( normalize( normal ), 1.0 );
	output.Depth = float4( normalizedDepth, 0.0, 0.0, 1.0 );
	output.Emission = float4( emission, 1.0 );
	return output;
}
