// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/GraphicsBufferLayout.h"

// カスタムグラフィックスパイプラインへ渡す読み取り専用バッファの場所。
// 配列の順が描画命令のBuffers列と対応する。
struct FGraphicsBufferSlot
{
	// シェーダー内の番号。space 0でConstantはb0〜b15、Raw・Structuredはt0〜t127。
	uint32 Binding = 0;
	// 定数か読み取りバッファの配置。
	EGraphicsBufferLayout Layout = EGraphicsBufferLayout::Constant;
	// Structuredの一要素のバイト幅（4〜2048の4の倍数）。それ以外は0。
	uint32 ElementStride = 0;
};
