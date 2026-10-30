// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/RenderState/RenderStateDemoState.h"

//=============================================================================
// CStageView
//   メインの 3D の画面。見せる範囲にクリップして、空を塗り、メインのカメラ（FCameraScope）で、舞台（NStageScenery）を描く。
//   描画状態（深度・カリング・画像の貼り方・フォグ・異方性）は、舞台を記録するなかで切り替える。この画面を描き終えると、すべて元に戻る。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CStageView : public CGameObject
{
public:
	explicit CStageView( const TSharedPtr<FRenderStateDemoState>& InState );

	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FRenderStateDemoState> State;
};
