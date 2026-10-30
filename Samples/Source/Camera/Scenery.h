// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/Render/Camera.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Source/Camera/CameraDemoState.h"

//=============================================================================
// NScenery
//   サンプル Camera の景色（地面の格子と、色つきの球・立方体・カプセル）。物の置き場所を作る関数と、描く（記録する）関数。
//   メインの画面と、上から見る図で、同じ景色を、違うカメラで描く。
//=============================================================================
namespace NScenery
{
	// 景色の物を作る（輪に並べた 10 個と、中央の柱、遠くの 3 個）
	TArray<FDemoObject> MakeObjects();

	// 地面の格子と軸を、記録する（カメラは、呼ぶ側が、先に PushCamera しておく）
	void RecordGround( FDrawContext& CommandList );

	// 物を記録する。Cull が null でなければ、視錐台に入らない物は描かない（描かない分だけ、描画が減る）。Highlighted は、枠を付ける物の番号（無ければ -1）。描いた数を返す
	int32 RecordObjects( FDrawContext& CommandList, const TArray<FDemoObject>& Objects, const FFrustum* Cull, int32 Highlighted );
}
