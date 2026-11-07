// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Catalog/SampleScene.h"

struct FPlatformer2DState;

//=============================================================================
// CPlatformer2DSample
//   横スクロールのアクション（有名な配管工のゲームのような遊び）。右へ進んで、ゴールのポールを目指す。
//     - 主人公: 走る・ジャンプ（押している間は高く）・敵を踏む・ブロックを下から叩く（CPlatformer2DPlayer）
//     - 敵: 歩く敵（踏むとつぶれる）、甲羅の敵（踏むと甲羅になり、蹴ると滑って、ほかの敵を倒す）
//     - パワーアップ: キノコで大きく、花でファイア（X / J で火の玉）、スターで 10 秒無敵。敵に当たると、一段階 小さくなる
//     - ブロック: ?（コイン）・M（キノコ、大きいときは花）・*（スター）・レンガ（大きいと壊せる）
//   ルール（大きさの段階・無敵・コイン・点数・残機）は Common/PlatformerStatus.h にあり、Platformer3D と共通。
//   操作: A / D または左右 = 動く、Shift = 走る、Space / Z = ジャンプ、X / J = 火の玉、Enter = (ゴール / ゲームオーバーのあと) もう一度
//   --autoplay を付けると、自動で遊ぶ（画面の確認用）。
//=============================================================================
class CPlatformer2DSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleTick( float32 DeltaTime ) override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;

private:
	TSharedPtr<FPlatformer2DState> State;
};
