// SPDX-License-Identifier: MIT
// サンプル Deferred の、場面のシェーダーで共通の定数・光・見せ方（頂点・Forward のピクセル・ライティングのピクセルが使う）。
// cbScene の並びは、Samples/Source/Deferred/DeferredSources.h の FSceneConstants と同じ。光の数 24 は FDeferredDemoState::NumLights と同じ。
cbuffer cbScene : register( b4 )
{
	row_major float4x4 g_ViewProjection;
	float4 g_CameraPosition;        // xyz: カメラの位置、w: Far
	float4 g_Parameters;            // x: 時間、y: 見せるもの（0〜5）
	float4 g_LightPositions[24];    // xyz: 位置、w: 光の届く半径
	float4 g_LightColors[24];       // rgb: 色 x 明るさ
};

// 何も描かれていない所の色（Forward の描画先を消す色と同じ）
static const float3 SkyColor = float3( 6.0, 8.0, 18.0 ) / 255.0;

// 夜の光: 空の明るさ + 月の光（裏側も真っ黒にしないよう回り込ませる）+ 動く光 24 個（届く半径の端で 0 になる点の光）
float3 Lighting( float3 normal, float3 world )
{
	float3 light = float3( 0.10, 0.11, 0.17 );
	light += float3( 0.24, 0.27, 0.40 ) * saturate( ( dot( normal, float3( 0.4969, 0.7951, 0.3478 ) ) + 0.5 ) / 1.5 );
	for ( int k = 0; k < 24; ++k )
	{
		float3 toLight = g_LightPositions[k].xyz - world;
		float distanceSquared = max( dot( toLight, toLight ), 1e-4 );
		float radius = g_LightPositions[k].w;
		float window = saturate( 1.0 - distanceSquared / ( radius * radius ) );
		light += g_LightColors[k].rgb * saturate( dot( normal, toLight * rsqrt( distanceSquared ) ) ) * window * window / ( 1.0 + distanceSquared * 0.3 );
	}
	return light;
}

// 見せるもの（g_Parameters.y）に合わせた色。Forward でも Deferred でも、同じ式で決める
//   0: 仕上がり  1: 地の色  2: 法線  3: 位置（世界の位置を模様にしたもの。深度から求めた位置のずれが、模様のずれになる）  4: 深度（奥行き / Far）  5: 自分で光る色
float3 ShadeView( float3 albedo, float3 normal, float3 emission, float3 world, float normalizedDepth )
{
	int mode = (int)g_Parameters.y;
	if ( mode == 1 )
	{
		return albedo;
	}
	if ( mode == 2 )
	{
		return normal * 0.5 + 0.5;
	}
	if ( mode == 3 )
	{
		return 0.5 + 0.5 * sin( world * 4.0 );
	}
	if ( mode == 4 )
	{
		return float3( normalizedDepth, normalizedDepth, normalizedDepth ) * 4.0;
	}
	if ( mode == 5 )
	{
		return emission;
	}
	return albedo * Lighting( normal, world ) + emission;
}
