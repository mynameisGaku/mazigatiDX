// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Catalog/SampleScene.h"

//=============================================================================
// CRenderTargetSample
//   描画先（レンダーターゲット）の見本。描き込める画像を作って（CAssetManager::CreateRenderTarget）、そこへ描き、
//   ふつうの画像として貼り、画面の一部を写し、色を読み、PNG に保存する。シーンは、描画先と画像を用意して、描くオブジェクト（CTargetShowcase）を置くだけ。
//=============================================================================
class CRenderTargetSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;
};
