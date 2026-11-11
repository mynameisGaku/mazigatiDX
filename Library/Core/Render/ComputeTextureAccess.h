// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// computeが画像へ行う操作。描画用の読み取りはどちらも利用できる。
enum class EComputeTextureAccess : uint8
{
	Read, // Texture2Dからの読み取りだけ。
	ReadWrite, // RWTexture2Dからの読み取りと書き込み。
};
