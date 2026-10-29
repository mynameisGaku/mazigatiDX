// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"
#include "Source/Village/VillageState.h"

//=============================================================================
// CCoin
//   道に落ちている金貨。くるくる回って（幅を縮めて、回っているように見せる）、金色に光る（加算ブレンド）。
//   主人公が近づくと拾われて（FVillageState::Coins が増える）、輪が広がって消える。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CCoin : public CGameObject
{
public:
	// InIndex は、FVillageState::CoinPositions での番号。InCoin は金貨の画像、InGlow は光の画像
	CCoin( const TSharedPtr<FVillageState>& InState, int32 InIndex, const FVector2& InPosition, const TSharedPtr<CTexture>& InCoin, const TSharedPtr<CTexture>& InGlow );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FVillageState> State;
	int32 Index;
	FVector2 Position;
	TSharedPtr<CTexture> Coin;
	TSharedPtr<CTexture> Glow;

	// 回る動きの時間（秒）。金貨ごとに、始まりをずらす
	float32 Phase;

	// 拾われたか。拾われてからの時間（秒）
	bool bCollected = false;
	float32 CollectedTime = 0.0f;
};
