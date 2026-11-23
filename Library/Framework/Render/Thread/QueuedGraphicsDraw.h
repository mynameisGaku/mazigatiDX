// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IRenderGeometry3D.h"
#include "Library/Core/Render/GraphicsPipelineDraw.h"

struct FQueuedDrawStorage;

// 結果を待たずに積む 3D 描画（カスタムパイプライン）の、所有コピー。
// 呼び出しの間だけ借りる配列・頂点・定数を、全部複写して持つので、描画スレッドが後から実行しても、呼び出し元の変数に触れない。
// 複写は、1 つの入れ物（FQueuedDrawStorage）の 1 つの領域へまとめて置き、使い終わった入れ物は、次の描画のために取っておく
// （描くたびに、確保と解放を繰り返さない。確保した側と解放する側が別のスレッドなので、特に重い）。
// 持つのは、入れ物へのポインターだけなので、積む仕事の入れ物（TCallable）の中に、そのまま置ける。
class FQueuedGraphicsDraw
{
  public:
	// 何も持たない。実行は、いつも拒否される。
	FQueuedGraphicsDraw() = default;
	// 中身も複写する（積む仕事の入れ物が、コピーできる型を求めるために必要。ふだんは使わない）。
	FQueuedGraphicsDraw( const FQueuedGraphicsDraw &Other );
	// 中身を移し、元は空になる。
	FQueuedGraphicsDraw( FQueuedGraphicsDraw &&Other ) noexcept;
	// 今の中身を手放して、Other の中身を複写する。
	FQueuedGraphicsDraw &operator=( const FQueuedGraphicsDraw &Other );
	// 今の中身を手放して、Other の中身を移す。Other は空になる。
	FQueuedGraphicsDraw &operator=( FQueuedGraphicsDraw &&Other ) noexcept;
	// 入れ物を、次の描画のために返す。
	~FQueuedGraphicsDraw();

	// 共有バッファの描画を複写する（バッファ・画像は番号だけ。定数の中身は複写する）。
	static FQueuedGraphicsDraw Copy( const FGraphicsPipelineDraw &Draw );
	// 一時データの描画を複写する（頂点・添字・定数の中身を複写する）。VertexStride が 0 のもの、頂点が無いものは、複写できない（空を返す）。
	static FQueuedGraphicsDraw Copy( const FGraphicsPipelineTransientDraw &Draw );
	// 描画スレッドで実行する。実行先が受け付けたら true。空なら false。
	bool Execute( IRenderGeometry3D &Geometry ) const;
	// 一時データの描画か。
	bool IsTransient() const;
	// 描画するパイプライン。
	FGraphicsPipelineHandle GetPipeline() const;

  private:
	// 入れ物を持つ。
	explicit FQueuedGraphicsDraw( FQueuedDrawStorage *InStorage ) noexcept;
	// 持っている入れ物を返して、空にする。
	void Release();

	FQueuedDrawStorage *Storage = nullptr; // 複写を入れた入れ物（空なら nullptr）。
};
