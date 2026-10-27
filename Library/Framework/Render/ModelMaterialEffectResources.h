// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_RENDER_MODELMATERIALEFFECTRESOURCES_H
#define FRAMEWORK_RENDER_MODELMATERIALEFFECTRESOURCES_H

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Effect/MeshMaterialEffectDefinition.h"

class CModel;

// 一つのModel材質効果で共有する、検証済み設定・画像・backend形状を保持する。
struct FModelMaterialEffectResources
{
	FModelMaterialEffectResources() = default;
	~FModelMaterialEffectResources();
	FModelMaterialEffectResources( const FModelMaterialEffectResources& ) = delete;
	FModelMaterialEffectResources& operator=( const FModelMaterialEffectResources& ) = delete;

	// 借用画像handleの寿命を支える元Model。
	TSharedPtr<const CModel> Source;
	// 生成後に変更しない再生設定。
	FMeshMaterialEffectDefinition Definition;
	// 元Modelの借用画像を使わないときの組み込み白画像所有者。
	TSharedPtr<CTexture> OwnedTexture;
	// 明示指定された消失mask画像の所有者。
	TSharedPtr<CTexture> MaskTexture;
	// 正の強度を持つゆがみ曲線で必要な画像所有者。
	TSharedPtr<CTexture> DistortionTexture;
	// whiteまたは元Modelから固定した材質画像handle。
	int32 TextureHandle = -1;
	// ready確認後に固定した消失画像handle。-1なら組み込み模様を使う。
	int32 MaskTextureHandle = -1;
	// ready確認後に固定したゆがみ画像handle。強度0なら未使用。
	int32 DistortionTextureHandle = -1;
	// backendが生成した選択材質の不変geometry handle。
	int32 GeometryHandle = -1;
	// 対象native meshの裏面を描く指定。
	bool bTwoSided = false;
};

#endif
