// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

//=============================================================================
// CShapeGallery
//   FImmediateDraw で描ける 2D の図形（四角・円・線・文字）を、ラベルつきで並べる。動かない。
//   オブジェクトの Draw() の中から、DxLib を直接呼ばずに描く見本。
//=============================================================================
class CShapeGallery : public CGameObject
{
public:
	// InOrigin は、並べる図形の左上（画面の座標。ピクセル）
	explicit CShapeGallery( const FVector2& InOrigin );

	void Draw() override;

private:
	// 並べる図形の左上（画面の座標。ピクセル）
	FVector2 Origin;
};
