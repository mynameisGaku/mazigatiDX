// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_CORE_INTERFACES_RENDER_DRAWMODELMATERIAL_H
#define FRAMEWORK_CORE_INTERFACES_RENDER_DRAWMODELMATERIAL_H

#include <cmath>

#include "Library/Core/Interfaces/Render/DrawMaterialMesh.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/TypeTraits.h"

// MV1モデルの一材質を効果付きで描き、残りを通常描画する固定命令。
struct FDrawModelMaterialData
{
	// 通常描画でも使う個体別モデルhandle。
	int32 Handle = -1;
	// 個体のワールド変換行列。
	float32 M[12] = {};
	// 選択材質の不変mesh形状を指すbackend handle。
	int32 Geometry = -1;
	// 選択材質へ適用するAlphaまたはAddのblend値。
	int32 BlendMode = static_cast<int32>( EDrawBlend::Alpha );
	// 元の頂点alphaへ掛けるモデル効果opacity。
	float32 Opacity = 1.0f;
	// 選択面へ適用する共通効果設定。頂点数とindex数は0を保つ。
	FDrawMaterialMeshData Material;
};

static_assert( sizeof( FDrawModelMaterialData ) == 160 && bIsTriviallyCopyable<FDrawModelMaterialData>, "model material command data must occupy forty words" );

namespace NDrawModelMaterial
{
	// 固定payloadを検証し、無効handle・行列・blend・予約数ならfalseを返す。
	inline bool IsValid( const FDrawModelMaterialData& Data )
	{
		if ( Data.Handle < 0 || Data.Geometry < 0 || ( Data.BlendMode != static_cast<int32>( EDrawBlend::Alpha ) && Data.BlendMode != static_cast<int32>( EDrawBlend::Add ) ) || !NMath::IsFinite( Data.Opacity ) || Data.Opacity < 0.0f || Data.Opacity > 1.0f ) return false;
		for ( int32 Index = 0; Index < 12; ++Index )
		{
			// ワールド行列の有限性を調べる要素位置。
			if ( !NMath::IsFinite( Data.M[Index] ) ) return false;
		}
		if ( Data.Material.NumVertices != 0 || Data.Material.NumIndices != 0 ) return false;
		return NDrawMaterialMesh::IsValidSettings( Data.Material );
	}
}

#endif
