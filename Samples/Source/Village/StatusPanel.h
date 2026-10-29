// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Font.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"
#include "Source/Village/VillageState.h"

//=============================================================================
// CStatusPanel
//   画面の端に出す情報。左上に、集めた金貨の数と、走るための体力（円グラフ）。右上に、村の全体の地図（ミニマップ）と、主人公・金貨・いま見えている範囲。
//   世界の座標ではなく、画面の座標で描く（カメラの変換は掛けない）。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CStatusPanel : public CGameObject
{
public:
	// InFont は文字のフォント、InCoin は金貨の画像、InRing は体力の輪の画像（NVillageArt::MakeRing）、InOverview は地図の全体の画像（CTileMap::MakeOverview）
	CStatusPanel( const TSharedPtr<FVillageState>& InState, const TSharedPtr<CFont>& InFont, const TSharedPtr<CTexture>& InCoin, const TSharedPtr<CTexture>& InRing, const TSharedPtr<CTexture>& InOverview );

	void Draw( FDrawContext& CommandList ) override;

private:
	// 左上の、金貨と体力
	void DrawCounters( FDrawContext& CommandList ) const;

	// 右上の、ミニマップ
	void DrawMinimap( FDrawContext& CommandList ) const;

	TSharedPtr<FVillageState> State;
	TSharedPtr<CFont> Font;
	TSharedPtr<CTexture> Coin;
	TSharedPtr<CTexture> Ring;
	TSharedPtr<CTexture> Overview;
};
