// SPDX-License-Identifier: MIT
#pragma once

//=============================================================================
// NLightCullingShaders
//   ライトカリングの HLSL の正本（EngineContent/Shaders/LightCulling）を、文字列として渡す。HLSL は、C++ に埋め込まず、EngineContent/Shaders のファイルとして書く。
//   戻り値は、プログラムの終わりまで有効な、NUL で終わる文字列。
//=============================================================================
namespace NLightCullingShaders
{
	// タイルごとの光の一覧を読む（g_TileLights・TileOfPixel・IsTileOverflowed・LoadTileTexel）。
	// 使う側が、これの前に、画像の段を決める（#define TILE_LIGHTS_REGISTER t0 など）。CTileLightGrid::WriteImage が詰めた画像を読む
	const char* GetTileLightListSource();
}
