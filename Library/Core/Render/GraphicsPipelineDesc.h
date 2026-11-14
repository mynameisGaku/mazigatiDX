// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Render/GeometryDrawState.h"
#include "Library/Core/Render/GraphicsBufferSlot.h"
#include "Library/Core/Render/GraphicsInputElement.h"
#include "Library/Core/Render/GraphicsTextureSlot.h"
#include "Library/Core/Render/RenderTargetFormat.h"

// グラフィックスパイプラインが結合先として要求する深度形式。
enum class EGraphicsDepthFormat : uint8
{
	None, // 深度ターゲットを使わない（bDepthTestとbDepthWriteもfalseにする）。
	D32Float, // 32bit浮動小数点深度（D32_FLOAT）を使用する。
};

// カスタムグラフィックスパイプラインの作成設定。GPUやD3D12固有型を公開しない。
struct FGraphicsPipelineDesc
{
	// 作成対象となる色描画先の最大同時数（D3D12_SIMULTANEOUS_RENDER_TARGET_COUNTに一致）。
	static constexpr uint32 MaxRenderTargets = 8;

	// DXBC形式の頂点シェーダーバイナリ（SM5.0／SM5.1）。
	TArrayView<const uint8> VertexShaderBytecode;
	// DXBC形式のピクセルシェーダーバイナリ（SM5.0／SM5.1）。
	TArrayView<const uint8> PixelShaderBytecode;
	// 頂点バッファ1頂点あたりのバイト幅（4〜2048の4の倍数）。
	uint32 VertexStride = 0;
	// 頂点入力レイアウト定義（1〜16要素。VS非システム入力と一致させる）。
	TArrayView<const FGraphicsInputElement> InputElements;
	// 読み取り専用バッファのスロット定義（bまたはt）。
	TArrayView<const FGraphicsBufferSlot> BufferSlots;
	// 読み取り専用2D画像のスロット定義（t）。
	TArrayView<const FGraphicsTextureSlot> TextureSlots;
	// 同時色描画先数（1〜MaxRenderTargets）。PSのSV_Target0..N-1と一致させる。
	uint32 NumRenderTargets = 1;
	// 各色描画先の画素形式。
	ERenderTargetColorFormat RenderTargetFormats[MaxRenderTargets] =
	{
		ERenderTargetColorFormat::RGBA8, ERenderTargetColorFormat::RGBA8, ERenderTargetColorFormat::RGBA8, ERenderTargetColorFormat::RGBA8,
		ERenderTargetColorFormat::RGBA8, ERenderTargetColorFormat::RGBA8, ERenderTargetColorFormat::RGBA8, ERenderTargetColorFormat::RGBA8
	};
	// 深度バッファの使用有無。
	EGraphicsDepthFormat DepthFormat = EGraphicsDepthFormat::D32Float;
	// 頂点の結び方（Triangles／Lines／Points）。
	EGeometryTopology Topology = EGeometryTopology::Triangles;
	// DepthFormatがD32FloatでbDepthTestがtrueのときの深度比較方法。
	EGeometryCompare DepthCompare = EGeometryCompare::LessEqual;
	// 表面・裏面のカリング設定。
	EGeometryCulling Culling = EGeometryCulling::None;
	// register(s0)サンプラの範囲外UV扱い。
	EGeometryTextureAddress SamplerAddress = EGeometryTextureAddress::Clamp;
	// 深度比較を行うか（DepthFormatがNoneならfalse必須）。
	bool bDepthTest = true;
	// 深度値を書き込むか（DepthFormatがD32FloatかつbDepthTestがtrueのときのみ許可）。
	bool bDepthWrite = true;
	// 全描画先に対して標準アルファブレンド（SrcAlpha / InvSrcAlpha）を有効にするか。
	bool bAlphaBlend = false;
	// register(s0)サンプラに線形補間を使うか（falseはPoint）。
	bool bLinearSampler = false;
};
