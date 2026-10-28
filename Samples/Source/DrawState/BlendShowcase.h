// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

class FDrawContext;

//=============================================================================
// CBlendShowcase
//   描画の状態のうち、色にかかわるもの（ブレンド・カスタムブレンド・加算する色・アルファテスト）を見せる。
//   上の列: 左右で色が違う下地の上に、動く円を、ブレンドを変えて重ねる（名前は、それぞれの下）。
//   下の列: アルファテスト。透明度が段々高くなる箱を並べ、基準値を動かすと、基準値より透明度が高い箱だけが描かれる。
//   状態（ブレンド・加算する色・アルファテスト）は、Draw が終わるまで残る（終わると、元に戻る）ので、使い終わったら、その場で戻している。
//=============================================================================
class CBlendShowcase : public CGameObject
{
public:
	// InOrigin は、左端の絵の中心（画面の座標。ピクセル）。絵は、そこから右へ並ぶ
	explicit CBlendShowcase( const FVector2& InOrigin );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	// 下地（左右で色が違う帯）
	void DrawBackdrop( FDrawContext& CommandList, const FVector2& Center ) const;

	// 上の列の 1 つぶん（Index 番目の状態で、円を重ねる）。名前も描く
	void DrawCell( FDrawContext& CommandList, int32 Index, const FVector2& Center ) const;

	// 下の列（アルファテスト）
	void DrawAlphaTest( FDrawContext& CommandList ) const;

	// 左端の絵の中心（画面の座標。ピクセル）
	FVector2 Origin;

	// 動きの時間（秒）。Tick の DeltaTime を足していく
	float32 Time = 0.0f;
};
