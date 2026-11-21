// SPDX-License-Identifier: MIT
#pragma once

//=============================================================================
// NLightCullingShaders
//   ライトカリングの HLSL の正本（EngineContent/Shaders/LightCulling）を、文字列として渡す。HLSL は、C++ に埋め込まず、EngineContent/Shaders のファイルとして書く。
//   戻り値は、プログラムの終わりまで有効な、NUL で終わる文字列。
//=============================================================================
namespace NLightCullingShaders
{
	// クラスタ（タイル x 奥行きの層）ごとの光の一覧を読む（g_TileLights・TileOfPixel・SliceOfDepth・ClusterOfPixel・IsClusterOverflowed・LoadClusterTexel）。
	// 使う側が、これの前に、画像の段を決める（#define TILE_LIGHTS_REGISTER t0 など）。CTileLightGrid::WriteImage が詰めた画像を読む
	const char* GetTileLightListSource();

	// クラスタごとの光の一覧を、GPU で作る（BuildTileLightTexel。TILE_LIGHT_BUILD_VIEW_REGISTER・TILE_LIGHT_BUILD_POSITION_REGISTER・TILE_LIGHT_BUILD_AXIS_REGISTER を、先に決める。
	// ピクセルシェーダーにするときは、TILE_LIGHT_BUILD_PIXEL も）。CTileLightBuilder が使う。CTileLightGrid::Build と同じ判定
	const char* GetTileLightBuildSource();
}
