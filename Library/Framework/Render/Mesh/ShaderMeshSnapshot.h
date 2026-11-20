// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Memory/SharedPtr.h"

struct FDrawCommandView;
struct FDrawSpan;
struct FShaderVertex3D;

// 頂点と添字を一度だけ命令形式に固定し、複数の描画項目で所有を共有する値。
class FShaderMeshSnapshot
{
public:
	// 16bitの添字付き三角形を複写する。不正な値や容量超過では無効値を返す。
	static FShaderMeshSnapshot Create( const FShaderVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices );
	// 32bit添字を受け取り、三角形の順序を保つ複数の描画へ分ける。
	static FShaderMeshSnapshot Create( TArrayView<const FShaderVertex3D> Vertices, TArrayView<const uint32> Indices );
	// 形状が準備済みかを返す。
	bool IsValid() const { return Parts.IsValid(); }
	// 再生に必要な描画数を返す。未作成なら0。
	int32 GetNumParts() const { return Parts ? Parts->Num() : 0; }
	// 指定の部分を既存の頂点付き描画へ解決する。不正な参照や範囲ではInOutを変更しない。
	static bool Resolve( const FDrawSpan& Span, FDrawCommandView& InOut, int32 PartIndex = 0 );

private:
	TSharedPtr<const TArray<TArray<uint32>>> Parts; // 順番に再生する検査済み命令の共有所有。
};
