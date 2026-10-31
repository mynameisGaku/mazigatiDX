// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Font.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/Shadows/ShadowsDemoState.h"

//=============================================================================
// CShadowsPanel
//   3D の上に重ねる、画面の座標の表示（2D）。
//   - 見どころの名札: 世界の点が、画面のどこに見えるか（FCamera::WorldToScreen）から、位置を決める。動くものの名札は、ついて動く。
//   - 切り替えられる設定の一覧（数字キーと、いまの値）。
//   - 6 キーで、影の絵の中身（光から見た深度。近いほど暗い赤）を右下に出す（PreviewShadowMap）。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CShadowsPanel : public CGameObject
{
public:
	// InFont は、文字のフォント
	CShadowsPanel( const TSharedPtr<FShadowsDemoState>& InState, const TSharedPtr<CFont>& InFont );

	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FShadowsDemoState> State;
	TSharedPtr<CFont> Font;
};
