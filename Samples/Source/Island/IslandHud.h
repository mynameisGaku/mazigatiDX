// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Font.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/Island/IslandState.h"

//=============================================================================
// CIslandHud
//   3D の上に重ねる、画面の座標の表示（2D）:
//     左上   拾った結晶の数・時刻・設定（キーと、いまの値）
//     右下   地図（上から見た島の画像に、探検家・結晶・灯台・カメラの向きを重ねる）
//     3D の上  近くの結晶の名札（WorldToScreen で、結晶の上に置く）。全部拾ったら、知らせる
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CIslandHud : public CGameObject
{
public:
	// InFont は、文字のフォント
	CIslandHud( const TSharedPtr<FIslandState>& InState, const TSharedPtr<CFont>& InFont );

	void Draw( FDrawContext& CommandList ) override;

private:
	// 左上の一覧
	void DrawPanel( FDrawContext& CommandList ) const;

	// 右下の地図
	void DrawMap( FDrawContext& CommandList ) const;

	// 結晶の名札と、全部拾ったときの知らせ
	void DrawLabels( FDrawContext& CommandList ) const;

	TSharedPtr<FIslandState> State;
	TSharedPtr<CFont> Font;
};
