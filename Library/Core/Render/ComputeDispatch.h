// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Render/BufferResourceHandle.h"
#include "Library/Core/Render/TextureResourceHandle.h"
#include "Library/Core/Render/ComputePipelineHandle.h"

// 一回のcompute命令。配列はSubmitComputeの呼出中だけ借り、実行先が必要な内容と寿命を保持する。
struct FComputeDispatch
{
	FComputePipelineHandle Pipeline; // 実行する作成済みプログラム。
	TArrayView<const FBufferResourceHandle> Buffers; // プログラムのslot順のバッファ。一命令内で同じ資源を重複させない。
	uint32 GroupsX = 0; // X方向のグループ数。0は無効。
	uint32 GroupsY = 1; // Y方向のグループ数。
	uint32 GroupsZ = 1; // Z方向のグループ数。
	TArrayView<const FTextureResourceHandle> Textures; // 画像slot順の番号。入力配列は呼出中だけ借りる。
};
