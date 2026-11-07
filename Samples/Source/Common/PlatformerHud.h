// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Common/PlatformerStatus.h"

class FDrawContext;

//=============================================================================
// NPlatformerHud
//   横スクロール・3D のアクションで共通の、画面の上の表示: 点数・コイン・残機・いまの姿・スターの残り時間。
//   ゴールしたとき・ゲームオーバーのときの、大きな知らせ。
//=============================================================================
namespace NPlatformerHud
{
	void Draw( FDrawContext& CommandList, const FPlatformerStatus& Status, bool bGoalReached, bool bGameOver );
}
