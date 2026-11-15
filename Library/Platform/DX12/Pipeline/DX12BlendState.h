// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/GeometryDrawState.h"
#include <d3d12.h>

// 描画先との混ぜ方（EGeometryBlend）を、D3D12 のブレンド状態へ写す。組み込みの図形と、利用者のシェーダー（カスタムパイプライン）が同じ表を使う。
namespace NDX12BlendState
{
	// 1 つの描画先のブレンド状態を決める。bBlend が false なら、上書き（混ぜない）。
	// 加算・減算・乗算は、描画先のアルファを変えない（色だけを混ぜる）。
	// 係数を D3D12 の値にする。bAlpha が true なら、アルファの係数として使える値にする（色の係数は、同じ種類のアルファへ読み替える）。
	inline D3D12_BLEND ToD3DFactor( EGeometryBlendFactor Factor, bool bAlpha )
	{
		switch ( Factor )
		{
		case EGeometryBlendFactor::Zero:
			return D3D12_BLEND_ZERO;
		case EGeometryBlendFactor::One:
			return D3D12_BLEND_ONE;
		case EGeometryBlendFactor::SrcColor:
			return bAlpha ? D3D12_BLEND_SRC_ALPHA : D3D12_BLEND_SRC_COLOR;
		case EGeometryBlendFactor::InvSrcColor:
			return bAlpha ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_INV_SRC_COLOR;
		case EGeometryBlendFactor::SrcAlpha:
			return D3D12_BLEND_SRC_ALPHA;
		case EGeometryBlendFactor::InvSrcAlpha:
			return D3D12_BLEND_INV_SRC_ALPHA;
		case EGeometryBlendFactor::DestColor:
			return bAlpha ? D3D12_BLEND_DEST_ALPHA : D3D12_BLEND_DEST_COLOR;
		case EGeometryBlendFactor::InvDestColor:
			return bAlpha ? D3D12_BLEND_INV_DEST_ALPHA : D3D12_BLEND_INV_DEST_COLOR;
		case EGeometryBlendFactor::DestAlpha:
			return D3D12_BLEND_DEST_ALPHA;
		case EGeometryBlendFactor::InvDestAlpha:
			return D3D12_BLEND_INV_DEST_ALPHA;
		default:
			return D3D12_BLEND_SRC_ALPHA_SAT;
		}
	}
	inline D3D12_BLEND_OP ToD3DOp( EGeometryBlendOp Op )
	{
		switch ( Op )
		{
		case EGeometryBlendOp::Subtract:
			return D3D12_BLEND_OP_SUBTRACT;
		case EGeometryBlendOp::ReverseSubtract:
			return D3D12_BLEND_OP_REV_SUBTRACT;
		case EGeometryBlendOp::Min:
			return D3D12_BLEND_OP_MIN;
		case EGeometryBlendOp::Max:
			return D3D12_BLEND_OP_MAX;
		default:
			return D3D12_BLEND_OP_ADD;
		}
	}

	inline void Configure( D3D12_RENDER_TARGET_BLEND_DESC& Blend, bool bBlend, EGeometryBlend Mode, const FGeometryCustomBlend& Custom = FGeometryCustomBlend() )
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
			case EGeometryBlend::Custom:
				Blend.SrcBlend = ToD3DFactor( Custom.SrcColor, false );
				Blend.DestBlend = ToD3DFactor( Custom.DestColor, false );
				Blend.BlendOp = ToD3DOp( Custom.ColorOp );
				Blend.SrcBlendAlpha = ToD3DFactor( Custom.SrcAlpha, true );
				Blend.DestBlendAlpha = ToD3DFactor( Custom.DestAlpha, true );
				Blend.BlendOpAlpha = ToD3DOp( Custom.AlphaOp );
				break;
			default:
				break;
			}
		}
		Blend.LogicOp = D3D12_LOGIC_OP_NOOP;
		Blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	}
}
