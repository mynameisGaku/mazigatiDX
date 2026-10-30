// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/Lighting/LightingDemoState.h"

//=============================================================================
// CLightingView
//   メインの 3D の画面。見せる範囲にクリップして、夜空を塗り、メインのカメラ（FCameraScope）で、中庭（NLightingScenery）を描く。
//   ライティング（光の計算・環境光・材質・ライト）は、舞台を記録するなかで決める。この画面を描き終えると、すべて元に戻る。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CLightingView : public CGameObject
{
public:
	explicit CLightingView( const TSharedPtr<FLightingDemoState>& InState );

	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FLightingDemoState> State;
};
