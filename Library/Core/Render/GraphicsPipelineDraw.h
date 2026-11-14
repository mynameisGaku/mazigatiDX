// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Render/BufferResourceHandle.h"
#include "Library/Core/Render/GeometryIndexFormat.h"
#include "Library/Core/Render/GraphicsPipelineHandle.h"
#include "Library/Core/Render/TextureResourceHandle.h"

// カスタムグラフィックスパイプラインを使った1回のGPUバッファ描画命令。
// 配列はDrawGraphicsPipelineの呼び出し中だけ借り、実体の寿命は実行先がGPU完了まで保持する。
struct FGraphicsPipelineDraw
{
	// 実行する登録済みグラフィックスパイプライン。
	FGraphicsPipelineHandle Pipeline;
	// 頂点配列を含むGPUバッファ（範囲内のOffset + Count * VertexStride）。
	FBufferResourceHandle VertexBuffer;
	// VertexBuffer先頭からのバイトオフセット（VertexStrideに整合するか4の倍数）。
	uint64 VertexOffset = 0;
	// 描画またはindex参照がアクセス可能な頂点数（1以上）。
	uint32 VertexCount = 0;
	// 任意。有効な場合はindex付き描画（DrawIndexedInstanced）を行う。
	FBufferResourceHandle IndexBuffer;
	// IndexBuffer先頭からのバイトオフセット（形式サイズの倍数）。
	uint64 IndexOffset = 0;
	// IndexBuffer有効時に描画するindex数。
	uint32 IndexCount = 0;
	// index値のビット幅。
	EGeometryIndexFormat IndexFormat = EGeometryIndexFormat::UInt16;
	// パイプラインのBufferSlots順に並べるバッファ。
	TArrayView<const FBufferResourceHandle> Buffers;
	// パイプラインのTextureSlots順に並べる画像（パス書き込み中のターゲットは禁止）。
	TArrayView<const FTextureResourceHandle> Textures;
};
