// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Font.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/Lighting/LightingDemoState.h"

//=============================================================================
// CLightingPanel
//   3D の上に重ねる、画面の座標の表示（2D）。
//   - 見どころの名札: 世界の点が、画面のどこに見えるか（FCamera::WorldToScreen）から、位置を決める。動くライトの名札は、ライトについて動く。
//   - 切り替えられるライティングの一覧（数字キーと、いまの値）と、ライトの数（足したい数と、効く数）。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CLightingPanel : public CGameObject
{
public:
	// InFont は、文字のフォント
	CLightingPanel( const TSharedPtr<FLightingDemoState>& InState, const TSharedPtr<CFont>& InFont );

	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FLightingDemoState> State;
	TSharedPtr<CFont> Font;
};
