// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/RenderBufferShaderAccess.h"
#include "Library/Core/Render/ComputeBufferLayout.h"

// computeプログラムへ渡すバッファの場所。配列の順がdispatchのバッファ列と対応する。
struct FComputeBufferSlot
{
	uint32 Binding = 0; // シェーダー内の場所の番号。初期DX12のspace 0で、Constantはb、その他のReadはt、ReadWriteはu。
	ERenderBufferShaderAccess Access = ERenderBufferShaderAccess::Read; // 読み取りか、読み書きか。Noneは無効。ConstantはReadだけ。
	EComputeBufferLayout Layout = EComputeBufferLayout::Raw; // 従来の二項目指定はRAWとして維持する。
	uint32 ElementStride = 0; // Structuredの一要素のバイト幅。4〜2048の4の倍数。それ以外は0。
};
