// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// バッファをシェーダーへ渡す際に許可する用途。転送とCPUへの読み戻しはどの用途でも使える。
enum class ERenderBufferShaderAccess : uint8
{
	None, // シェーダーへ渡さない既定の転送用。
	Read, // シェーダーから読むSRVとして使用する。
	ReadWrite // 読み取りに加えて、UAVからの書き込みを許可する。
};
