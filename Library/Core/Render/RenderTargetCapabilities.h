// SPDX-License-Identifier: MIT
#pragma once

// 描画先画像と読み取れる深度を利用できるかを表す値。GPUや描画APIの型を公開しない。
struct FRenderTargetCapabilities
{
	// 2DのRGBA8を描画先として描き、描画やcomputeの入力として読めるか。
	bool bRGBA8RenderTarget = false;
	// 2Dの32bit浮動小数点深度へ描き、computeからR32Floatとして読めるか。
	bool bReadableDepthD32 = false;

	// 両方の能力が揃っているか。
	constexpr bool IsSupported() const noexcept
	{
		return bRGBA8RenderTarget && bReadableDepthD32;
	}
};
