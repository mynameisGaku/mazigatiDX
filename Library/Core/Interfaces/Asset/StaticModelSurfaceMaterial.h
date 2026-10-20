// SPDX-License-Identifier: NOASSERTION
#ifndef LIBRARY_CORE_INTERFACES_ASSET_STATICMODELSURFACEMATERIAL_H
#define LIBRARY_CORE_INTERFACES_ASSET_STATICMODELSURFACEMATERIAL_H

#include "Library/Core/Types/TypeDefinition.h"

// 静的モデル面データが写し取る、照明処理前の材質色と借用画像handle。
// 照明・alpha test・UV変換など、元モデルのnative描画を再現する値ではない。
struct FStaticModelSurfaceMaterial
{
	float32 Diffuse[4] = { 1.0f, 1.0f, 1.0f, 1.0f };  // 元材質の拡散色RGBA。頂点reference色へ重ねて掛けない
	int32 BorrowedDiffuseTexture = -1;                 // 元モデルが所有する拡散画像handle。削除しない
};

#endif
