// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

class FDrawContext;

//=============================================================================
// CShapeShowcase
//   2D の図形（三角形・四角形・楕円・角の丸い四角形・円・線）を、ゆっくり動かしながら横に 6 つ並べる。
//   bSmooth が false なら整数の座標の図形（Triangle / Quad / Oval / RoundBox / Circle / Line）、true ならなめらかな図形
//   （TriangleAA / QuadAA / OvalAA / RoundBoxAA / CircleAA / LineAA。座標は float で、ピクセルの途中にも置ける）で描く。
//   同じ動きを 2 つのオブジェクトで描き比べると、縁のなめらかさと、ゆっくりした動きのガタつきの違いが分かる。
//=============================================================================
class CShapeShowcase : public CGameObject
{
public:
	// InOrigin は、左端の図形の中心（画面の座標。ピクセル）。図形は、そこから右へ並ぶ
	CShapeShowcase( const FVector2& InOrigin, bool bInSmooth );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	// 図形 1 つぶんの描き方（Center は、その図形の中心）
	void DrawTriangle( FDrawContext& CommandList, const FVector2& Center ) const;
	void DrawQuad( FDrawContext& CommandList, const FVector2& Center ) const;
	void DrawOval( FDrawContext& CommandList, const FVector2& Center ) const;
	void DrawRoundBox( FDrawContext& CommandList, const FVector2& Center ) const;
	void DrawCircle( FDrawContext& CommandList, const FVector2& Center ) const;
	void DrawLine( FDrawContext& CommandList, const FVector2& Center ) const;

	// 左端の図形の中心（画面の座標。ピクセル）
	FVector2 Origin;

	// true ならなめらかな図形、false なら整数の座標の図形
	bool bSmooth;

	// 動きの時間（秒）。Tick の DeltaTime を足していく
	float32 Time = 0.0f;
};
