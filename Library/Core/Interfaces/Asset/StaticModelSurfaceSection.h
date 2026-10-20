// SPDX-License-Identifier: NOASSERTION
#ifndef LIBRARY_CORE_INTERFACES_ASSET_STATICMODELSURFACESECTION_H
#define LIBRARY_CORE_INTERFACES_ASSET_STATICMODELSURFACESECTION_H

#include "Library/Core/Types/TypeDefinition.h"

// 静的モデル面データで、同じ材質を使う連続した三角形の範囲を表す。
struct FStaticModelSurfaceSection
{
	int32 FirstIndex = 0;          // この範囲の先頭index
	int32 NumIndices = 0;          // この範囲に含むindex数
	int32 MaterialIndex = -1;     // 対応する材質番号。未対応なら -1
	int32 SourceFrameIndex = -1;  // 元モデルのフレーム番号。未対応なら -1
	int32 SourceMeshIndex = -1;   // 元モデルのメッシュ番号。未対応なら -1
};

#endif
