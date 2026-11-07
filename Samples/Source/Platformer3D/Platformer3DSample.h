// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Source/Catalog/SampleScene.h"

struct FPlatformer3DState;

//=============================================================================
// CPlatformer3DSample
//   3D のアクション（Platformer2D の、3D 版）。奥（+Z）へ進んで、ゴールのポールを目指す。
//     - 主人公: カメラに合わせて走る・ジャンプ（押している間は高く）・敵を踏む・ブロックを下から叩く（CPlatformer3DPlayer）
//     - 敵: 追いかけてくる敵（踏むとつぶれる）、甲羅の敵（踏むと甲羅になり、蹴ると滑って、ほかの敵を倒す）
//     - パワーアップ: キノコで大きく、花でファイア（X / J で火の玉）、スターで 10 秒無敵。敵に当たると、一段階 小さくなる
//     - 面: 地面・穴・階段・土管・浮いた足場・ブロック（コイン / キノコ・花 / スター / レンガ）・コイン
//   ルール（大きさの段階・無敵・コイン・点数・残機）は Common/PlatformerStatus.h にあり、Platformer2D と共通。
//   操作: W A S D = 動く、← → または Q / E = カメラを回す、Shift = 走る、Space = ジャンプ、X / J = 火の玉、Enter = (ゴール / ゲームオーバーのあと) もう一度
//   --autoplay を付けると、自動で遊ぶ（画面の確認用）。--form=big / fire、--star で、最初の姿を決められる。
//=============================================================================
class CPlatformer3DSample : public CSampleScene
{
protected:
	void OnEnter() override;
	void OnSampleTick( float32 DeltaTime ) override;
	void OnSampleDraw( FImmediateDraw& Screen ) override;

private:
	TSharedPtr<FPlatformer3DState> State;
};
