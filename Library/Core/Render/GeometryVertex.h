// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Math/Color.h"

// 射影後・透視除算前の頂点。Zの範囲は0〜Wで、GPUがクリップする。
struct FGeometryVertex
{
	float32 Position[4] = {}; // 同次クリップ座標。Wを除算せず渡す。
	FColor32 Color; // 照明計算を済ませたRGBA。
	float32 U = 0, V = 0; // 透視補間する画像座標。
};
