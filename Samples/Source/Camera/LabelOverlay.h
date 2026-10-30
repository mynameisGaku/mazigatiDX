// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Font.h"
#include "Library/Framework/Object/GameObject.h"
#include "Source/Camera/CameraDemoState.h"

//=============================================================================
// CLabelOverlay
//   3D の上に重ねる、画面の座標の表示（2D）。
//   - 物の名前と、カメラからの距離: 世界の点が、画面のどこに見えるか（FCamera::WorldToScreen）から、位置を決める。
//   - カメラの説明（射影・視野角・位置）と、視錐台カリングの結果（描いた数 / 全部の数）、マウスが指している物。
//   Draw( FDrawContext& CommandList ) は、何を描くかを記録するだけ。
//=============================================================================
class CLabelOverlay : public CGameObject
{
public:
	// InFont は、文字のフォント
	CLabelOverlay( const TSharedPtr<FCameraDemoState>& InState, const TSharedPtr<CFont>& InFont );

	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FCameraDemoState> State;
	TSharedPtr<CFont> Font;
};
