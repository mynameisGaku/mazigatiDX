// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/RenderBufferShaderAccess.h"

// computeプログラムへ渡すバッファの場所。配列の順がdispatchのバッファ列と対応する。
struct FComputeBufferSlot
{
	uint32 Binding = 0; // シェーダー内の場所の番号。初期DX12ではReadはt、ReadWriteはuのspace 0へ対応する。
	ERenderBufferShaderAccess Access = ERenderBufferShaderAccess::Read; // 読み取りか、読み書きか。Noneは無効。
};
