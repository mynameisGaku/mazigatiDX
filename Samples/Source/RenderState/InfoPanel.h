// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Font.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/RenderState/RenderStateDemoState.h"
#include "Source/RenderState/StageScenery.h"

//=============================================================================
// CInfoPanel
//   3D の上に重ねる、画面の座標の表示（2D）。
//   - 舞台の見どころの名札: 世界の点が、画面のどこに見えるか（FCamera::WorldToScreen）から、位置を決める。
//   - 切り替えられる描画状態の一覧（数字キーと、いまの値）。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CInfoPanel : public CGameObject
{
public:
	// InFont は、文字のフォント
	CInfoPanel( const TSharedPtr<FRenderStateDemoState>& InState, const TSharedPtr<CFont>& InFont );

	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FRenderStateDemoState> State;
	TSharedPtr<CFont> Font;
	TArray<NStageScenery::FStageLabel> Labels;
};
