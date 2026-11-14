// SPDX-License-Identifier: MIT
#pragma once

// 画像をcomputeで利用できるかを表す値。GPUや描画APIの型を公開しない。
struct FComputeTextureCapabilities
{
	// 2DのRGBA8をTexture2DのLoadから読めるか。
	bool bRGBA8Read = false;
	// 2DのRGBA8をRWTexture2Dから読み書きできるか。書き込みだけの能力とは区別する。
	bool bRGBA8ReadWrite = false;
	// 2DのR32Float（読み取れる深度など）をTexture2D<float>のLoadから読めるか。
	bool bR32FloatRead = false;
	// 2DのRGBA16FをTexture2D<float4>のLoadから読めるか。
	bool bRGBA16FRead = false;
	// 2DのRGBA16FをRWTexture2D<float4>から読み書きできるか。
	bool bRGBA16FReadWrite = false;
};
