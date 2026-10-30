// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Source/RenderState/RenderStateDemoState.h"

class CAssetManager;

//=============================================================================
// NStageScenery
//   サンプル RenderState の舞台。一つの場面に、3D の描画状態の使いどころを並べる:
//     地面の市松模様（フィルターと異方性）、遠くの柱（距離のフォグ）、足元の霧（高さのフォグ）、
//     部屋（カリング）、水面（画像の変換）、壁（画像の外側の貼り方 4 種）、ガラス（深度バッファ）、
//     腕（3D 図形の変換の積み重ね）、手前に描く光線（深度バッファの消去）。
//   画像を作る関数と、舞台を描く（記録する）関数、名札の置き場所を作る関数。
//=============================================================================
namespace NStageScenery
{
	// 名札 1 つ（世界の位置と、文字）
	struct FStageLabel
	{
		FString Text;
		FVector3 Position;
	};

	// 舞台で使う画像を作って、State に入れる。作れなければ false
	bool MakeTextures( CAssetManager& Assets, FRenderStateDemoState& State );

	// 名札を作る
	TArray<FStageLabel> MakeLabels();

	// 舞台を、State の描画状態で、記録する（カメラは、呼ぶ側が、先に PushCamera しておく）
	void RecordStage( FDrawContext& CommandList, const FRenderStateDemoState& State );
}
