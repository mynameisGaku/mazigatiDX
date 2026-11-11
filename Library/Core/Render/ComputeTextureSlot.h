// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/ComputeTextureAccess.h"

// プログラムの画像の場所と用途。GPUのdescriptorやアドレスは含めない。
struct FComputeTextureSlot
{
	// Readはt0〜127、ReadWriteはu0〜63の番号。同じ名前空間ではbufferと重ねない。
	uint32 Binding = 0;
	// シェーダーの画像宣言と照合する用途。
	EComputeTextureAccess Access = EComputeTextureAccess::Read;
};
