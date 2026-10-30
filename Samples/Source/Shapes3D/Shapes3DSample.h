// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Catalog/SampleScene.h"
#include "Source/Shapes3D/Shapes3DDemoState.h"

//=============================================================================
// CShapes3DSample
//   3D 図形の追加の見本。夜の広場に、次のものを並べる:
//     松の木（Cone3D）、星空（Pixel3D）、蛍・座標軸・らせん・波・傘・宝石（Primitive3D / PrimitiveIndexed3D の 6 通りのつなぎ方）、
//     草・旗・回る星・脈打つ心臓・揺れるランタン（BillboardPivot3D / BillboardQuad3D / Sprite3D / SpriteStretch3D / SpritePivot3D）。
//   数字キーで切り替えて、見え方の違いを確かめる。シーンは、画像を用意して、3 つのオブジェクトを置くだけ。
//=============================================================================
class CShapes3DSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;

private:
	TSharedPtr<FShapes3DDemoState> State;
};
