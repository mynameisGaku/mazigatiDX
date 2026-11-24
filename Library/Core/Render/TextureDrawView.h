// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// 描画実装だけが解釈する借用画像。登録番号をGPUのbindingへ解決した値。
struct FTextureDrawView
{
	uint64 Binding = 0; // 対応する描画実装内だけで有効な識別子。
	int32 Width = 0; // 画素幅。
	int32 Height = 0; // 画素高さ。
	// 派生画像（元の画像の矩形を共有する画像）のとき、元の画像の中の矩形（左上と右下の UV）。ふつうの画像は 0, 0, 1, 1。
	// 2Dと組み込み材質効果は、この範囲へUVを写す。利用者のシェーダーで使う場合は呼び出し側がUVを決める。
	float32 UvRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
};
