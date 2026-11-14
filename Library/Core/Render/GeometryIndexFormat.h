// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// GPUバッファに格納する頂点番号の整数幅。API固有の形式番号は使わない。
enum class EGeometryIndexFormat : uint8
{
	UInt16, // 一つの頂点番号が16bit。
	UInt32, // 一つの頂点番号が32bit。
};
