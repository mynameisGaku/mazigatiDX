// SPDX-License-Identifier: NOASSERTION
#ifndef FRAMEWORK_RENDER_MESHMATERIALEFFECTRESOURCES_H
#define FRAMEWORK_RENDER_MESHMATERIALEFFECTRESOURCES_H

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Interfaces/Render/MeshGeometryChunk.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Effect/MeshMaterialEffectDefinition.h"
#include "Library/Core/Interfaces/Render/MeshTypes.h"

// 生成時に検証した一つの材質slotを、形状と画像の寿命ごと共有する。
struct FMeshMaterialEffectResources
{
	// 一命令の頂点・index上限に合わせて分割した、同じslot内の三角形群。
	// 既存effect APIとの互換名を保つ共通chunk型。
	using FChunk = FMeshGeometryChunk;

	// 生成後は変更しないslotの保存設定。
	FMeshMaterialEffectDefinition Definition;
	// 元描画順を保つ、全チャンクの固定形状。
	TArray<FChunk> Chunks;
	// 材質画像。空pathなら組み込み白画像を保持する。
	TSharedPtr<CTexture> Texture;
	// 明示指定された消失mask画像。未指定はshader内蔵模様を使う。
	TSharedPtr<CTexture> MaskTexture;
	// 正の強度を持つゆがみ曲線で必要な画像。
	TSharedPtr<CTexture> DistortionTexture;
	// ready確認後に固定した表示画像handle。
	int32 TextureHandle = -1;
	// ready確認後に固定した消失画像handle。-1なら組み込み模様。
	int32 MaskTextureHandle = -1;
	// ready確認後に固定したゆがみ画像handle。強度0なら未使用。
	int32 DistortionTextureHandle = -1;
	// 元slotと同じ裏面表示指定。
	bool bTwoSided = false;
};

#endif
