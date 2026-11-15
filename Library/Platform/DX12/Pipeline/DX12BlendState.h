// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/GeometryDrawState.h"
#include <d3d12.h>

// 描画先との混ぜ方（EGeometryBlend）を、D3D12 のブレンド状態へ写す。組み込みの図形と、利用者のシェーダー（カスタムパイプライン）が同じ表を使う。
namespace NDX12BlendState
{
	// 1 つの描画先のブレンド状態を決める。bBlend が false なら、上書き（混ぜない）。
	// 加算・減算・乗算は、描画先のアルファを変えない（色だけを混ぜる）。
	inline void Configure( D3D12_RENDER_TARGET_BLEND_DESC& Blend, bool bBlend, EGeometryBlend Mode )
	{
		// 既定は、通常のアルファ合成。
		Blend.BlendEnable = bBlend ? TRUE : FALSE;
		Blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		Blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		Blend.BlendOp = D3D12_BLEND_OP_ADD;
		Blend.SrcBlendAlpha = D3D12_BLEND_ONE;
		Blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		Blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		if ( bBlend )
		{
			switch ( Mode )
			{
			case EGeometryBlend::Add:
			case EGeometryBlend::Subtract:
				Blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
				Blend.DestBlend = D3D12_BLEND_ONE;
				Blend.BlendOp = Mode == EGeometryBlend::Add ? D3D12_BLEND_OP_ADD : D3D12_BLEND_OP_REV_SUBTRACT;
				Blend.SrcBlendAlpha = D3D12_BLEND_ZERO;
				Blend.DestBlendAlpha = D3D12_BLEND_ONE;
				break;
			case EGeometryBlend::Multiply:
				Blend.SrcBlend = D3D12_BLEND_ZERO;
				Blend.DestBlend = D3D12_BLEND_SRC_COLOR;
				Blend.SrcBlendAlpha = D3D12_BLEND_ZERO;
				Blend.DestBlendAlpha = D3D12_BLEND_ONE;
				break;
			case EGeometryBlend::PremultipliedAlpha:
				Blend.SrcBlend = D3D12_BLEND_ONE;
				Blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
				break;
			case EGeometryBlend::PremultipliedAdd:
			case EGeometryBlend::PremultipliedSubtract:
				Blend.SrcBlend = D3D12_BLEND_ONE;
				Blend.DestBlend = D3D12_BLEND_ONE;
				Blend.BlendOp = Mode == EGeometryBlend::PremultipliedAdd ? D3D12_BLEND_OP_ADD : D3D12_BLEND_OP_REV_SUBTRACT;
				Blend.SrcBlendAlpha = D3D12_BLEND_ZERO;
				Blend.DestBlendAlpha = D3D12_BLEND_ONE;
				break;
			default:
				break;
			}
		}
		Blend.LogicOp = D3D12_LOGIC_OP_NOOP;
		Blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	}
}
