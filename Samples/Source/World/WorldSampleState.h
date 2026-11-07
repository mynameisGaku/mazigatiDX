// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Framework/World/Runtime/WorldSpawner.h"
#include "Library/Math/Math.h"

//=============================================================================
// FWorldSampleState
//   World サンプルのオブジェクトが分け合う状態。置いたワールド（FWorldInstance）、見た目を持つ置き物の一覧、
//   主人公の位置とカメラ、集めたコインの数。シーンが作って、各オブジェクトに渡す。
//=============================================================================
struct FWorldSampleState
{
	static constexpr int32 ScreenWidth = 1280;
	static constexpr int32 ScreenHeight = 720;

	// 置いた結果（タグで探す窓口）。読み直すとき、Despawn する
	FWorldInstance Stage;

	// 見た目を持つ置き物（タグ "Scenery"）。メッシュとエフェクトは、CWorldSampleView が、カメラの中で描く
	TArray<TSharedPtr<CGameObject>> Scenery;

	// 柱の位置（主人公が、めり込まないように、よける）
	TArray<FVector3> PillarPositions;

	FVector3 PlayerPosition;
	FCamera Camera;
	float32 CameraYaw = 0.0f;
	float32 Time = 0.0f;

	int32 CoinsCollected = 0;
	int32 CoinsTotal = 0;
	int32 ScoreCollected = 0;

	// 読み直した回数（確認用の表示）
	int32 NumLoads = 0;

	FCameraViewport GetViewport() const { return FCameraViewport { ScreenWidth, ScreenHeight }; }
};
