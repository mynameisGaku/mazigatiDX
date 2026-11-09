// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Render/ComputeBufferSlot.h"
#include "Library/Core/Render/ComputePipelineHandle.h"

// computeプログラムの作成と所有。HLSLの生成やファイルの読み込み、命令提出は担当しない。
class IComputePipelineResources
{
public:
	// 提出済み処理の借用が終わった後で所有先を破棄する。
	virtual ~IComputePipelineResources() = default;
	// 完成したバイナリとslot定義を呼出中に消費する。不正・未対応の形式や不一致では無効値。
	// 初期DX12経路はDXBCのCS5.0/5.1、バイト列のSRV/UAV、space 0だけを受け付ける。
	virtual FComputePipelineHandle CreateComputePipeline( TArrayView<const uint8> Bytecode, TArrayView<const FComputeBufferSlot> Slots ) = 0;
	// 番号を直ちに無効にし、提出済み処理の実体は完了まで保持する。無効・別の所有先・二重解放は無視する。
	virtual void ReleaseComputePipeline( FComputePipelineHandle Pipeline ) = 0;
};
