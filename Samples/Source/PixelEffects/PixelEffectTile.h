// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Framework/Asset/Texture/DynamicTexture.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/PixelEffect/Playback/PixelEffectView.h"

class FDrawContext;

//=============================================================================
// CPixelEffectTile
//   エフェクト 1 種の見本。ゲームで使うときと同じ形で描く: 背景の画像 → エフェクトの後ろの層 → キャラクターの画像 → 前の層。
//   エフェクトの再生と画像は CPixelEffectView に任せる（キャラクターと背景は、エフェクトとは別の、ふつうの画像）。
//   一度きりのエフェクトは、終わって少し待ってから、種を変えて始め直す。F キーで左右反転、R キーで種を変えて最初から。
//=============================================================================
class CPixelEffectTile : public CGameObject
{
public:
	// InOrigin は、見出しの左上（画面の座標）。InActor が nullptr でなければ、エフェクトの前後の間に、その立ち絵を描く
	CPixelEffectTile( const FVector2& InOrigin, TUniquePtr<IPixelEffect> InEffect, EPixelBackdrop InBackdrop, const FPixelActor* InActor );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	// いまの種と向きで、最初から始める
	void Restart();

	FVector2 Origin;
	TUniquePtr<CPixelEffectView> Effect;
	TSharedPtr<CDynamicTexture> Backdrop;
	// 立ち絵（右向きと、左右反転の 2 枚）
	TSharedPtr<CDynamicTexture> Actor;
	TSharedPtr<CDynamicTexture> FlippedActor;
	bool bFlip = false;
	uint32 Seed = 1;
	// 一度きりのエフェクトが終わってから、始め直すまでの残り（秒）
	float32 RestartDelay = 0.0f;
};
