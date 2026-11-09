// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/Asset/Texture/DynamicTexture.h"

// 図形・クリップ・半透明・画素から作る画像を共通窓口だけで描く。
class CCanvas2DGallery : public CGameObject
{
public:
	// 描画の記録前に画像を用意する。
	void OnSpawn() override;
	// 準備済みの画像と固定の図形だけ記録する。
	void Draw( FDrawContext& Draw ) override;
private:
	TSharedPtr<CDynamicTexture> Texture; // 見本の市松模様。
};
