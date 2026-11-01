// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Framework/Asset/DynamicTexture.h"
#include "Library/Framework/Asset/PixelImage.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/PixelEffect/IPixelEffect.h"
#include "Library/Framework/PixelEffect/PixelEffectFrame.h"

class FDrawContext;

//=============================================================================
// CPixelEffectTile
//   エフェクト 1 種の見本。毎フレーム、時間を進め、色と光の 2 枚（FPixelEffectFrame）へ 後ろ → 立ち絵 → 前 の順に描き、
//   背景の上に合成して、画像（CDynamicTexture）の中身を替える。一度きりのエフェクトは、終わって少し待ってから、種を変えて始め直す。
//   F キーで左右反転、R キーで種を変えて最初から。Draw は、画像を拡大して描くことを記録するだけ。
//=============================================================================
class CPixelEffectTile : public CGameObject
{
public:
	// InOrigin は、見出しの左上（画面の座標）。InActor が nullptr でなければ、エフェクトの前後の間に立ち絵を描く
	CPixelEffectTile( const FVector2& InOrigin, TUniquePtr<IPixelEffect> InEffect, EPixelBackdrop InBackdrop, const FPixelActor* InActor );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	// いまの種と向きで、最初から始める
	void Restart();

	FVector2 Origin;
	TUniquePtr<IPixelEffect> Effect;
	FPixelEffectFrame Frame;
	FPixelImage Backdrop;
	TSharedPtr<CDynamicTexture> Texture;
	bool bHasActor = false;
	FPixelActor Actor;
	bool bFlip = false;
	uint32 Seed = 1;
	// 一度きりのエフェクトが終わってから、始め直すまでの残り（秒）
	float32 RestartDelay = 0.0f;
};
