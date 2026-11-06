// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Font/Font.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/PostEffects/PostEffectsDemoState.h"

//=============================================================================
// CPostEffectsPanel
//   3D の上に重ねる、画面の座標の表示（2D）。切り替えられる設定の一覧（キーと、いまの値）。
//   ポストエフェクトの影響を受けないよう、場面を貼ったあとに描く。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CPostEffectsPanel : public CGameObject
{
public:
	// InFont は、文字のフォント
	CPostEffectsPanel( const TSharedPtr<FPostEffectsDemoState>& InState, const TSharedPtr<CFont>& InFont );

	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FPostEffectsDemoState> State;
	TSharedPtr<CFont> Font;
};
