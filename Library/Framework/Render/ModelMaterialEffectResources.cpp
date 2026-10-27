// SPDX-License-Identifier: MIT
#include "ModelMaterialEffectResources.h"

#include "Library/Framework/Asset/Asset.h"

FModelMaterialEffectResources::~FModelMaterialEffectResources()
{
	// backend形状を返してからSourceを解放し、借用画像を最後まで有効に保つ。
	if ( GeometryHandle >= 0 ) NAssetService::ReleaseModelMaterialGeometry( GeometryHandle );
}
