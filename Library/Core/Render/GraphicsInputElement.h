// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/GraphicsVertexAttributeFormat.h"

// 頂点シェーダー入力へ割り当てる頂点バッファ上の1要素。
struct FGraphicsInputElement
{
	// HLSLのセマンティック名（ASCII識別子、1〜31文字。SV_で始まるシステム値は不可）。
	const char *SemanticName = nullptr;
	// セマンティック番号（POSITION0なら0、TEXCOORD1なら1）。
	uint32 SemanticIndex = 0;
	// 頂点バッファ上のデータ型。
	EGraphicsVertexAttributeFormat Format = EGraphicsVertexAttributeFormat::Float3;
	// 頂点構造体先頭からのバイトオフセット（4の倍数）。
	uint32 AlignedByteOffset = 0;
};
