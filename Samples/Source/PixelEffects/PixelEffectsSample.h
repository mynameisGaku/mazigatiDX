// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Catalog/SampleScene.h"

class CPixelEffectTile;

//=============================================================================
// CPixelEffectsSample
//   ピクセルアートのエフェクトの見本。エフェクト 1 種につき 1 枚の見本（CPixelEffectTile）を格子に並べ、それぞれ繰り返し再生する。
//   1 ページに 10 枚。← → キーでページを替える（見えないページの見本は止める）。F キーで左右反転、R キーで種を変えて最初から。
//=============================================================================
class CPixelEffectsSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleTick( float32 DeltaTime ) override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;

private:
	// Page のページの見本だけを見せて動かす
	void ShowPage( int32 InPage );

	TArray<TSharedPtr<CPixelEffectTile>> Tiles;
	int32 Page = 0;
};
