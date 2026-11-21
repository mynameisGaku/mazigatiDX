// SPDX-License-Identifier: MIT
// クラスタ（タイル x 奥行きの層）ごとの光の一覧を読む（Library/Framework/Render/LightCulling/TileLightGrid.h の CTileLightGrid::WriteImage が詰めた、8 ビット RGBA の画像）。
// 使う側が、この部品の前に、画像の段を決める（例: #define TILE_LIGHTS_REGISTER t0）。
// 画像は、クラスタ 1 つが横に texelsPerTile 画素。幅 = タイルの列 x texelsPerTile、高さ = 層の数 x タイルの行（行 = 層 x タイルの行の数 + タイルの行）。
// 1 画素の R・G・B に、光の番号 + 1 を 1 つずつ（0 は、一覧の終わり）。クラスタが溢れていたら、先頭の画素の R が 255（その場合は、全部の光を評価する）。
// 画素の座標は、画像を Load で 1 画素ずつ読む（フィルターに依らない）。奥行きを分けないとき（層の数が 1）は、クラスタ = タイル。
Texture2D g_TileLights : register( TILE_LIGHTS_REGISTER );

// 画面の位置 pixel（描画先の画素の座標。SV_Position の xy）が属するタイル（tileSize はタイル 1 枚の大きさ）
int2 TileOfPixel( float2 pixel, int tileSize )
{
	return int2( floor( pixel ) ) / tileSize;
}

// 奥行き depth（カメラの前向きの距離）の層の番号。Near（nearDepth）から Far（farDepth）までを、奥行きの比が等しい numSlices 個の層に分けた、何番目か。
// Near より手前は 0、Far より奥は最後の層。CPU の CTileLightGrid::GetSliceOfDepth と同じ式
int SliceOfDepth( float depth, int numSlices, float nearDepth, float farDepth )
{
	float scale = (float)numSlices / log( farDepth / nearDepth );
	return clamp( (int)floor( log( max( depth, nearDepth ) / nearDepth ) * scale ), 0, numSlices - 1 );
}

// 画面の位置 pixel・奥行き depth の画素が属するクラスタ（x: タイルの列、y: 画像の行）。numTilesY はタイルの行の数
int2 ClusterOfPixel( float2 pixel, int tileSize, float depth, int numTilesY, int numSlices, float nearDepth, float farDepth )
{
	int2 tile = TileOfPixel( pixel, tileSize );
	return int2( tile.x, SliceOfDepth( depth, numSlices, nearDepth, farDepth ) * numTilesY + tile.y );
}

// クラスタが溢れたか（溢れていたら、全部の光を評価する）
bool IsClusterOverflowed( int2 cluster, int texelsPerTile )
{
	int first = (int)round( g_TileLights.Load( int3( cluster.x * texelsPerTile, cluster.y, 0 ) ).r * 255.0 );
	return first == 255;
}

// クラスタの一覧の、texel 番目の画素にある 3 つの光の番号。一覧の終わり（と、それ以降）は -1
int3 LoadClusterTexel( int2 cluster, int texelsPerTile, int texel )
{
	int3 packed = (int3)round( g_TileLights.Load( int3( cluster.x * texelsPerTile + texel, cluster.y, 0 ) ).rgb * 255.0 );
	return packed - 1;
}
