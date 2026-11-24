// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Render/BufferResourceHandle.h"
#include "Library/Core/Render/GeometryIndexFormat.h"
#include "Library/Core/Render/GraphicsPipelineHandle.h"
#include "Library/Core/Render/TextureResourceHandle.h"

// 定数バッファ 1 つの中身（呼び出しの間だけ借りる）。
struct FGraphicsConstantBytes
{
	const uint8* Data = nullptr;
	uint32 Size = 0;
};

// 1 回の描画の、頂点・添字の数の上限。実行先の各層（命令を積む門・実行側・Forward の描画部品）が、ここの値で検査する（層ごとに値を持たない）。
namespace NGraphicsPipelineLimits
{
	// 登録しない描画（頂点・添字を、呼び出しごとに、フレームの転送領域へ複写する）。複写する量を抑えるため、小さい。
	constexpr uint32 MaxTransientVertices = 65536;
	constexpr uint32 MaxTransientIndices = 196608;
	// 登録したバッファの描画（頂点・添字は、GPU のバッファのまま使い回す）。バッファ 1 つの上限（64 MiB）に収まる大きさ: 頂点 48 バイトで 1M 個（48 MiB）、添字 4 バイトで 6M 個（24 MiB）。
	constexpr uint32 MaxRegisteredVertices = 1u << 20;
	constexpr uint32 MaxRegisteredIndices = 6u << 20;
	// 1 フレームに記録できる、カスタムグラフィックスパイプラインの描画の回数（メッシュ・材質の範囲ごとに 1 回）。以前は 1024 で、これを超えるシーン（Faputa なら 146 体）は、GPU の経路から、遅い CPU の経路へ落ちた。
	// 描画 1 回は、画像の常駐の記述子と、GPU の仮想アドレスだけを使い、描画ごとの領域は取らない。実際の制約は、1 フレームの転送領域の予算（門は 40 MiB・DX12 は 64 MiB）と、保持する資源の数・大きさ。
	constexpr int32 MaxDrawsPerFrame = 16384;
}

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
	// 描画またはindex参照がアクセス可能な頂点数（1〜NGraphicsPipelineLimits::MaxRegisteredVertices）。
	uint32 VertexCount = 0;
	// 任意。有効な場合はindex付き描画（DrawIndexedInstanced）を行う。
	FBufferResourceHandle IndexBuffer;
	// IndexBuffer先頭からのバイトオフセット（形式サイズの倍数）。
	uint64 IndexOffset = 0;
	// IndexBuffer有効時に描画するindex数（1〜NGraphicsPipelineLimits::MaxRegisteredIndices）。
	uint32 IndexCount = 0;
	// index値のビット幅。
	EGeometryIndexFormat IndexFormat = EGeometryIndexFormat::UInt16;
	// パイプラインのBufferSlots順に並べるバッファ。先頭のConstants.Num()個の枠は、下のConstantsが受け持つので、ここには、その続きの枠だけを並べる。
	TArrayView<const FBufferResourceHandle> Buffers;
	// パイプラインのTextureSlots順に並べる画像（パス書き込み中のターゲットは禁止）。
	TArrayView<const FTextureResourceHandle> Textures;
	// 任意。BufferSlotsの先頭から順に、定数バッファの中身を、登録せずに渡す（描くたびに変わる値。頂点・添字は、登録したバッファのまま使える）。
	// 受け持つ枠は、すべてConstantで、大きさはシェーダーの宣言以上（256の倍数へ切り上げて転送する）。
	TArrayView<const FGraphicsConstantBytes> Constants;
	// 任意。この描画が参照する共有バッファ（頂点・添字・Buffers）の大きさの合計（バイト）。
	// 結果を待たずに描画を積む実行先（QueueGraphicsPipeline）が、保持できる量の上限を超えないかを見積もるために使う。0 のときは、待たずには積まず、同期で実行する。
	uint64 PinnedBytes = 0;
};

// カスタムグラフィックスパイプラインを使った、1 回限りの描画。頂点・添字・定数を、GPU のバッファとして登録せず、フレームごとの転送領域へ載せて渡す
// （毎フレーム、何度も描く用途向け。バッファの登録・完了待ちをしない）。配列は、呼び出しの間だけ借りる。
struct FGraphicsPipelineTransientDraw
{
	// 実行する登録済みグラフィックスパイプライン。
	FGraphicsPipelineHandle Pipeline;
	// 頂点（パイプラインの VertexStride バイトずつ）。
	const void* Vertices = nullptr;
	// 頂点の数（1〜NGraphicsPipelineLimits::MaxTransientVertices = 65536）。
	uint32 VertexCount = 0;
	// 任意。頂点 1 つのバイト数（パイプラインの VertexStride と同じ）。結果を待たずに描画を積む実行先（QueueGraphicsPipelineTransient）が、頂点を複写するために使う。0 のときは、待たずには積まず、同期で実行する。
	uint32 VertexStride = 0;
	// 添字（32 ビット）。nullptr なら、頂点を順に使う。
	const uint32* Indices = nullptr;
	// 添字の数（Indices があるとき、1〜NGraphicsPipelineLimits::MaxTransientIndices = 196608）。
	uint32 IndexCount = 0;
	// パイプラインの BufferSlots の順の、定数バッファの中身（すべて Constant。大きさは、シェーダーの宣言以上。256 の倍数へ切り上げて渡す）。
	TArrayView<const FGraphicsConstantBytes> Constants;
	// パイプラインの TextureSlots の順に並べる画像（パス書き込み中のターゲットは禁止）。
	TArrayView<const FTextureResourceHandle> Textures;
};
