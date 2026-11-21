// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// IEEE 754 半精度（binary16）と単精度の変換。GPUのRGBA16F画像との転送に使う。
namespace NDX12HalfFloat
{
	// 単精度を半精度へ変換する（round-to-nearest-even。大きすぎる値は無限大、小さすぎる値は 0、非正規化数も扱う）。
	uint16 FromFloat32( float32 Value );
	// 半精度を単精度へ変換する（非正規化数・無限大・NaN も扱う）。
	float32 ToFloat32( uint16 Half );
}
