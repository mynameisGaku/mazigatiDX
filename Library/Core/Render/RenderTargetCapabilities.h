// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// 描画先画像と読み取れる深度を利用できるかを表す値。GPUや描画APIの型を公開しない。
struct FRenderTargetCapabilities
{
	// 2DのRGBA8を描画先として描き、描画やcomputeの入力として読めるか。
	bool bRGBA8RenderTarget = false;
	// 2Dの32bit浮動小数点深度へ描き、computeからR32Floatとして読めるか。
	bool bReadableDepthD32 = false;
	// 1パスで同時に束縛できる色描画先の上限数（未対応は0、DX12は最大8）。
	uint32 MaxColorTargets = 0;
	// 2DのRGBA16F（HDR）を描画先として描き、描画やcomputeの入力として読めるか。
	bool bRGBA16FRenderTarget = false;
	// 2DのR32Float単一成分を描画先として描き、描画やcomputeの入力として読めるか。
	bool bR32FloatRenderTarget = false;
	// 2DのR32Float描画先でアルファブレンド（OMブレンド）が有効か。
	bool bR32FloatBlendable = false;

	// 両方の能力が揃っているか。
	constexpr bool IsSupported() const noexcept
	{
		return bRGBA8RenderTarget && bReadableDepthD32;
	}
	// 指定枚数の複数描画先（MRT）と読み取れる深度を同時に使えるか。
	constexpr bool SupportsMultipleRenderTargets( uint32 Count = 2 ) const noexcept
	{
		return IsSupported() && Count >= 1 && Count <= MaxColorTargets;
	}
	// 指定枚数のRGBA16F描画先（混在MRT含む）と読み取れる深度を同時に使えるか。
	constexpr bool SupportsRGBA16FMultipleRenderTargets( uint32 Count = 2 ) const noexcept
	{
		return bRGBA16FRenderTarget && IsSupported() && Count >= 1 && Count <= MaxColorTargets;
	}
};
