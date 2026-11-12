// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// 頂点バッファ内の1要素のデータ型。API固有のDXGI形式は公開しない。
enum class EGraphicsVertexAttributeFormat : uint8
{
	Float1, // 32bit浮動小数点×1（4バイト）。
	Float2, // 32bit浮動小数点×2（8バイト）。
	Float3, // 32bit浮動小数点×3（12バイト）。
	Float4, // 32bit浮動小数点×4（16バイト）。
	UByte4Norm, // 符号なし8bit整数×4の正規化値（4バイト、FColor32相当）。
};
