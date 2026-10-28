// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

//=============================================================================
// CShapeGallery
//   コマンドリスト（FDrawContext）に記録できる 2D の図形（四角・円・線・文字）を、ラベルつきで並べる。動かない。
//   オブジェクトの Draw( FDrawContext& CommandList ) は、DxLib も実行先も知らずに、何を描くかを記録するだけの見本。
//=============================================================================
class CShapeGallery : public CGameObject
{
public:
	// InOrigin は、並べる図形の左上（画面の座標。ピクセル）
	explicit CShapeGallery( const FVector2& InOrigin );

	void Draw( FDrawContext& CommandList ) override;

private:
	// 並べる図形の左上（画面の座標。ピクセル）
	FVector2 Origin;
};
