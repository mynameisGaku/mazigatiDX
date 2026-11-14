// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/DrawVertex3D.h"

// 同期の命令解釈中だけ借用する頂点。書き換え・削除・次の登録より前に消費する。
struct FGeometryVertexBufferView
{
	const FDrawVertex3D* Vertices = nullptr; // 所有側の読み取り専用メモリ。
	int32 NumVertices = 0; // 読める要素数。
};

// indexは32bitへ統一して読む。元の16bit／32bit形式を描画側へ持ち込まない。
struct FGeometryIndexBufferView
{
	const uint32* Indices = nullptr; // 所有側の読み取り専用メモリ。
	int32 NumIndices = 0; // 読める要素数。
};
