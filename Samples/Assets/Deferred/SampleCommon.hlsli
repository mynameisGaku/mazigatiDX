// SPDX-License-Identifier: MIT
// サンプル Deferred の、場面のシェーダーで共通の定数・光・見せ方（頂点・Forward のピクセル・ライティングのピクセルが使う）。
// 使う側が、これの前に、次を決める（サンプルの C++ が連ねる。Samples/Source/Deferred/DeferredSources.cpp）:
//   NUM_LIGHTS: 動く光の数（FDeferredDemoState::NumLights と同じ）、TILE_LIGHTS_REGISTER: タイルごとの光の一覧の画像の段、
//   そのあとに、製品の部品 LightCulling/TileLightList.hlsli（g_TileLights・TileOfPixel・IsTileOverflowed・LoadTileTexel）。
// cbScene の並びは、Samples/Source/Deferred/DeferredSources.h の FSceneConstants と同じ。
cbuffer cbScene : register( b4 )
{
	row_major float4x4 g_ViewProjection;
	float4 g_CameraPosition;        // xyz: カメラの位置、w: Far
	float4 g_Parameters;            // x: 時間、y: 見せるもの（0〜5）、zw: 場面の描画先の大きさ（ピクセル）
	float4 g_TileInfo;              // x: タイル 1 枚の大きさ（ピクセル）、y: タイル 1 枚の横の画素の数、z: タイルの一覧を使うか（0 なら、全部の光を評価する）
	float4 g_LightPositions[NUM_LIGHTS];    // xyz: 位置、w: 光の届く半径
	float4 g_LightColors[NUM_LIGHTS];       // rgb: 色 x 明るさ
};

// 何も描かれていない所の色（Forward の描画先を消す色と同じ）
static const float3 SkyColor = float3( 6.0, 8.0, 18.0 ) / 255.0;

// 点の光 1 つぶんの明るさ（届く半径の端で 0 になる）
float3 PointLight( int k, float3 normal, float3 world )
{
	float3 toLight = g_LightPositions[k].xyz - world;
	float distanceSquared = max( dot( toLight, toLight ), 1e-4 );
	float radius = g_LightPositions[k].w;
	float window = saturate( 1.0 - distanceSquared / ( radius * radius ) );
	return g_LightColors[k].rgb * saturate( dot( normal, toLight * rsqrt( distanceSquared ) ) ) * window * window / ( 1.0 + distanceSquared * 0.3 );
}

// 夜の光: 空の明るさ + 月の光（裏側も真っ黒にしないよう回り込ませる）+ 動く光（点の光）。
// 動く光は、タイルの一覧を使うとき（Forward+ / Deferred+）は、画素が属するタイルに届く光だけを（光の番号の昇順に）足す。使わないときは、全部の光を足す。
// 一覧に入らない光は、その画素には届かない（光が 0）ので、どちらも同じ結果になる。タイルが溢れていたら、全部の光を足す（欠落させない）。pixel は、描画先の画素の座標（SV_Position の xy）
float3 Lighting( float3 normal, float3 world, float2 pixel )
{
	float3 light = float3( 0.10, 0.11, 0.17 );
	light += float3( 0.24, 0.27, 0.40 ) * saturate( ( dot( normal, float3( 0.4969, 0.7951, 0.3478 ) ) + 0.5 ) / 1.5 );
	int texelsPerTile = (int)g_TileInfo.y;
	int2 tile = TileOfPixel( pixel, (int)g_TileInfo.x );
	if ( g_TileInfo.z > 0.5 && !IsTileOverflowed( tile, texelsPerTile ) )
	{
		for ( int texel = 0; texel < texelsPerTile; ++texel )
		{
			int3 ids = LoadTileTexel( tile, texelsPerTile, texel );
			if ( ids.x < 0 )
			{
				break;
			}
			light += PointLight( ids.x, normal, world );
			if ( ids.y < 0 )
			{
				break;
			}
			light += PointLight( ids.y, normal, world );
			if ( ids.z < 0 )
			{
				break;
			}
			light += PointLight( ids.z, normal, world );
		}
	}
	else
	{
		for ( int k = 0; k < NUM_LIGHTS; ++k )
		{
			light += PointLight( k, normal, world );
		}
	}
	return light;
}

// 見せるもの（g_Parameters.y）に合わせた色。Forward でも Deferred でも、同じ式で決める
//   0: 仕上がり  1: 地の色  2: 法線  3: 位置（世界の位置を模様にしたもの。深度から求めた位置のずれが、模様のずれになる）  4: 深度（奥行き / Far）  5: 自分で光る色
float3 ShadeView( float3 albedo, float3 normal, float3 emission, float3 world, float normalizedDepth, float2 pixel )
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
	return albedo * Lighting( normal, world, pixel ) + emission;
}
