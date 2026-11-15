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

// 定数バッファ 1 つの中身（呼び出しの間だけ借りる）。
struct FGraphicsConstantBytes
{
	const uint8* Data = nullptr;
	uint32 Size = 0;
};

// カスタムグラフィックスパイプラインを使った、1 回限りの描画。頂点・添字・定数を、GPU のバッファとして登録せず、フレームごとの転送領域へ載せて渡す
// （毎フレーム、何度も描く用途向け。バッファの登録・完了待ちをしない）。配列は、呼び出しの間だけ借りる。
struct FGraphicsPipelineTransientDraw
{
	// 実行する登録済みグラフィックスパイプライン。
	FGraphicsPipelineHandle Pipeline;
	// 頂点（パイプラインの VertexStride バイトずつ）。
	const void* Vertices = nullptr;
	// 頂点の数（1〜65536）。
	uint32 VertexCount = 0;
	// 添字（32 ビット）。nullptr なら、頂点を順に使う。
	const uint32* Indices = nullptr;
	// 添字の数（Indices があるとき、1〜196608）。
	uint32 IndexCount = 0;
	// パイプラインの BufferSlots の順の、定数バッファの中身（すべて Constant。大きさは、シェーダーの宣言以上。256 の倍数へ切り上げて渡す）。
	TArrayView<const FGraphicsConstantBytes> Constants;
	// パイプラインの TextureSlots の順に並べる画像（パス書き込み中のターゲットは禁止）。
	TArrayView<const FTextureResourceHandle> Textures;
};
