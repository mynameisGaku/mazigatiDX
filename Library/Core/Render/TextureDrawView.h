// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// 描画実装だけが解釈する借用画像。登録番号をGPUのbindingへ解決した値。
struct FTextureDrawView
{
	uint64 Binding = 0; // 対応する描画実装内だけで有効な識別子。
	int32 Width = 0; // 画素幅。
	int32 Height = 0; // 画素高さ。
};
