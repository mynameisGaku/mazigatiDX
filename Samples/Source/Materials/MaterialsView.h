// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Font/Font.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/Materials/MaterialsDemoState.h"

//=============================================================================
// CMaterialsView
//   メインの画面。3 段で描く:
//   ① 場面（地面・球 3 つ・柱）を、Surface のマテリアル（市松模様・大理石・車の塗装・縁の光）で、描画先（State->SceneTarget）へ描く。
//   ② 描画先の絵を、ポストプロセスのチェーン（白黒・周りを暗く・モザイク）に通して、見せる範囲に貼る。
//   ③ UI のマテリアル（光るボタン・光の帯）を、矩形に描く。ボタンは、マウスののり方と押し方で光り方が変わる。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CMaterialsView : public CGameObject
{
public:
	CMaterialsView( const TSharedPtr<FMaterialsDemoState>& InState, const TSharedPtr<CFont>& InFont );

	void Draw( FDrawContext& CommandList ) override;

private:
	// 場面（地面と物）を記録する
	void RecordScene( FDrawContext& CommandList ) const;
	// UI のマテリアルを記録する
	void RecordUi( FDrawContext& CommandList ) const;
	// 操作の説明を記録する
	void RecordLegend( FDrawContext& CommandList ) const;

	TSharedPtr<FMaterialsDemoState> State;
	TSharedPtr<CFont> Font;
};
