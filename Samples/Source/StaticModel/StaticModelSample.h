// SPDX-License-Identifier: MIT
#pragma once
#include "Source/Catalog/SampleScene.h"

// 同じモデルから作った二つの個体を描く場面。
class CStaticModelSample : public CSampleScene
{
protected:
	// モデルの読み込みと描画を持つオブジェクトを置く。
	void OnEnter() override;
};
