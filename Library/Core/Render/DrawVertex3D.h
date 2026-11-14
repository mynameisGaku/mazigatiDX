// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// ローカル座標・法線・RGBA・UVを持つ共通頂点。可変長の描画命令にもそのまま複写する。
struct FDrawVertex3D
{
	float32 Position[3] = {}; // ローカル座標。
	float32 Normal[3] = { 0.0f, 1.0f, 0.0f }; // 面の法線。
	uint8 Diffuse[4] = { 255, 255, 255, 255 }; // 拡散色のRGBA。
	uint8 Specular[4] = { 0, 0, 0, 0 }; // 鏡面色のRGBA。
	float32 U = 0.0f, V = 0.0f; // 主画像の座標。
	float32 SubU = 0.0f, SubV = 0.0f; // 追加画像の座標。現段階では描画に使わない。
};
static_assert( sizeof( FDrawVertex3D ) == 48, "FDrawVertex3D layout must remain stable" );
