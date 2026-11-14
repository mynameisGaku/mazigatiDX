// SPDX-License-Identifier: MIT
#pragma once
#include "Source/Catalog/SampleScene.h"

// 共通命令によるカメラ・頂点色・深度の見本を配置する。
class CGeometry3DSample : public CSampleScene
{
protected:
	// 描画の責務を持つオブジェクトを一つ置く。
	void OnEnter() override;
};
