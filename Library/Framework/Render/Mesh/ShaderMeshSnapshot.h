// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"

struct FDrawCommandView;
struct FDrawSpan;
struct FShaderVertex3D;

// 頂点と添字を一度だけ命令形式に固定し、複数の描画項目で所有を共有する値。
class FShaderMeshSnapshot
{
public:
	// 添字付き三角形を複写する。不正な頂点・添字・命令の容量超過では無効値を返す。
	static FShaderMeshSnapshot Create( const FShaderVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices );
	// 形状が準備済みかを返す。
	bool IsValid() const { return Words.IsValid(); }
	// 参照命令を既存の頂点付き描画へ解決する。不正な参照ではInOutを変更しない。
	static bool Resolve( const FDrawSpan& Span, FDrawCommandView& InOut );

private:
	TSharedPtr<const TArray<uint32>> Words; // 公開後に変更しない一つの検査済み命令。
};
