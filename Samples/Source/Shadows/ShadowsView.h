// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/Shadows/ShadowsDemoState.h"

//=============================================================================
// CShadowsView
//   メインの 3D の画面。見せる範囲にクリップして、空を塗り、メインのカメラ（FCameraScope）で、中庭（NShadowsScenery）を描く。
//   影の絵を描くこと（BeginShadowMap〜EndShadowMap）と、影を受けることは、舞台を記録するなかで決める。この画面を描き終えると、すべて元に戻る。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CShadowsView : public CGameObject
{
public:
	explicit CShadowsView( const TSharedPtr<FShadowsDemoState>& InState );

	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FShadowsDemoState> State;
};
