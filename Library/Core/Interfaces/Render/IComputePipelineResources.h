// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Render/ComputeBufferSlot.h"
#include "Library/Core/Render/ComputeTextureSlot.h"
#include "Library/Core/Render/ComputePipelineHandle.h"

// computeプログラムの作成と所有。HLSLの生成やファイルの読み込み、命令提出は担当しない。
class IComputePipelineResources
{
public:
	// 提出済み処理の借用が終わった後で所有先を破棄する。
	virtual ~IComputePipelineResources() = default;
	// 完成したバイナリとslot定義を呼出中に消費する。不正・未対応の形式や不一致では無効値。
	// 初期DX12経路はDXBCのCS5.0/5.1、RAW／構造体配列のSRV/UAV、読み取り専用の定数、space 0を受け付ける。
	virtual FComputePipelineHandle CreateComputePipeline( TArrayView<const uint8> Bytecode, TArrayView<const FComputeBufferSlot> Slots ) = 0;
	// bufferと画像を別のslot順で宣言する。画像だけでもよい。未対応・不一致・場所の重複は無効値。
	virtual FComputePipelineHandle CreateComputePipelineWithTextures( TArrayView<const uint8> Bytecode, TArrayView<const FComputeBufferSlot> BufferSlots, TArrayView<const FComputeTextureSlot> TextureSlots ) { return {}; }
	// 番号を直ちに無効にし、提出済み処理の実体は完了まで保持する。無効・別の所有先・二重解放は無視する。
	virtual void ReleaseComputePipeline( FComputePipelineHandle Pipeline ) = 0;
};
