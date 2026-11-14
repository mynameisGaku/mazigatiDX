// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// 描画先の画素で表した切り取り範囲。右端と下端は含まない。
struct FRenderClipRect
{
	int32 Left = 0; // 含む左端。
	int32 Top = 0; // 含む上端。
	int32 Right = 0; // 含まない右端。
	int32 Bottom = 0; // 含まない下端。
};
