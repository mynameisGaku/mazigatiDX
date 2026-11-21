// SPDX-License-Identifier: MIT
// タイルごとの光の一覧を読む（Library/Framework/Render/LightCulling/TileLightGrid.h の CTileLightGrid::WriteImage が詰めた、8 ビット RGBA の画像）。
// 使う側が、この部品の前に、画像の段を決める（例: #define TILE_LIGHTS_REGISTER t0）。
// 画像は、タイル 1 枚が横に texelsPerTile 画素、タイルの並びの形。1 画素の R・G・B に、光の番号 + 1 を 1 つずつ（0 は、一覧の終わり）。
// タイルが溢れていたら、先頭の画素の R が 255（その場合は、全部の光を評価する）。画素の座標は、画像を Load で 1 画素ずつ読む（フィルターに依らない）。
Texture2D g_TileLights : register( TILE_LIGHTS_REGISTER );

// 画面の位置 pixel（描画先の画素の座標。SV_Position の xy）が属するタイル（tileSize はタイル 1 枚の大きさ）
int2 TileOfPixel( float2 pixel, int tileSize )
{
	return int2( floor( pixel ) ) / tileSize;
}

// タイルが溢れたか（溢れていたら、全部の光を評価する）
bool IsTileOverflowed( int2 tile, int texelsPerTile )
{
	int first = (int)round( g_TileLights.Load( int3( tile.x * texelsPerTile, tile.y, 0 ) ).r * 255.0 );
	return first == 255;
}

// タイルの一覧の、texel 番目の画素にある 3 つの光の番号。一覧の終わり（と、それ以降）は -1
int3 LoadTileTexel( int2 tile, int texelsPerTile, int texel )
{
	int3 packed = (int3)round( g_TileLights.Load( int3( tile.x * texelsPerTile + texel, tile.y, 0 ) ).rgb * 255.0 );
	return packed - 1;
}
