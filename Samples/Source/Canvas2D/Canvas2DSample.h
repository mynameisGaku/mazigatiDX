// SPDX-License-Identifier: MIT
#pragma once
#include "Source/Catalog/SampleScene.h"

// 各描画実装で同じ2D命令を試す場面。描き方はGalleryが持つ。
class CCanvas2DSample : public CSampleScene
{
protected:
	// 図形と画像の見本を一つ配置する。
	void OnEnter() override;
};
