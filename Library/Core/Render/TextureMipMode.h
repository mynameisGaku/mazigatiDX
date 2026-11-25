// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

// 画像を縮小するときの成分の扱い。既存の画像は Color を使う。
enum class ETextureMipMode : uint8
{
	Color,      // sRGB色を線形の光に変換し、透明度を考慮して平均する。
	LinearData  // 法線・粗さ・金属度などの各成分を独立して平均する。
};
