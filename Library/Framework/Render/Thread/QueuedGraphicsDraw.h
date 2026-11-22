// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Render/IRenderGeometry3D.h"
#include "Library/Core/Render/GraphicsPipelineDraw.h"

// 結果を待たずに積む 3D 描画（カスタムパイプライン）の、所有コピー。
// 呼び出しの間だけ借りる配列・頂点・定数を、全部複写して持つので、描画スレッドが後から実行しても、呼び出し元の変数に触れない。
class FQueuedGraphicsDraw
{
  public:
	// 共有バッファの描画を複写する（バッファ・画像は番号だけ。定数の中身は複写する）。
	static FQueuedGraphicsDraw Copy( const FGraphicsPipelineDraw &Draw );
	// 一時データの描画を複写する（頂点・添字・定数の中身を複写する）。VertexStride が 0 のものは複写できない。
	static FQueuedGraphicsDraw Copy( const FGraphicsPipelineTransientDraw &Draw );
	// 描画スレッドで実行する。実行先が受け付けたら true。
	bool Execute( IRenderGeometry3D &Geometry ) const;
	// 一時データの描画か。
	bool IsTransient() const { return bTransient; }
	// 描画するパイプライン。
	FGraphicsPipelineHandle GetPipeline() const { return Pipeline; }

  private:
	// 定数の複写から、借用の見え方を作る。
	void MakeConstantViews( TArray<FGraphicsConstantBytes> &OutViews ) const;

	bool bTransient = false; // 一時データの描画か。
	FGraphicsPipelineHandle Pipeline; // 描画するパイプライン。
	FBufferResourceHandle VertexBuffer; // 共有の頂点バッファ。
	FBufferResourceHandle IndexBuffer; // 共有の添字バッファ（添字なしなら無効）。
	uint64 VertexOffset = 0; // 頂点バッファ内の位置。
	uint64 IndexOffset = 0; // 添字バッファ内の位置。
	uint32 VertexCount = 0; // 頂点の数。
	uint32 IndexCount = 0; // 添字の数。
	EGeometryIndexFormat IndexFormat = EGeometryIndexFormat::UInt16; // 共有の添字の幅。
	TArray<FBufferResourceHandle> Buffers; // 共有のバッファスロット。
	TArray<FTextureResourceHandle> Textures; // 画像スロット。
	TArray<uint32> ConstantSizes; // 定数ごとの大きさ。
	TArray<uint8> ConstantBytes; // 定数の中身を、順に並べたもの。
	TArray<uint8> Vertices; // 一時データの頂点。
	TArray<uint32> Indices; // 一時データの添字。
	bool bIndexed = false; // 一時データに添字があるか。
};
