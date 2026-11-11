// SPDX-License-Identifier: MIT
#pragma once

// RGBA8の画像をcomputeで利用できるかを表す値。GPUや描画APIの型を公開しない。
struct FComputeTextureCapabilities
{
	// 2DのRGBA8をTexture2DのLoadから読めるか。
	bool bRGBA8Read = false;
	// 2DのRGBA8をRWTexture2Dから読み書きできるか。書き込みだけの能力とは区別する。
	bool bRGBA8ReadWrite = false;
};
