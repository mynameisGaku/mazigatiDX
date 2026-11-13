// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// computeプログラムで扱う画像の画素形式。
enum class EComputeTextureFormat : uint8
{
	RGBA8, // 4成分のRGBA8 (float4 / unorm float4)。従来の二項目指定の既定値。
	R32Float, // 1成分の32bit浮動小数点 (float)。読み取れる深度などのRead専用。
	RGBA16F, // 4成分の16bit浮動小数点 (float4)。HDR描画・compute読み書き用。
};
