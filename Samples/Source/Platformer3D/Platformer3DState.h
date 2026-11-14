// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Math/Math.h"
#include "Source/Common/PlatformerStatus.h"
#include "Source/Platformer3D/Platformer3DLevel.h"

//=============================================================================
// FPlatformer3DState
//   Platformer3D のオブジェクトが、分け合う状態。面（箱）、カメラ、主人公の状態（大きさ・コイン・点数・残機）、
//   主人公の位置（敵が追いかけるために見る）、ゴール / ゲームオーバーの印。シーンが作って、各オブジェクトに渡す。
//=============================================================================
struct FPlatformer3DState
{
	static constexpr int32 ScreenWidth = 1280;
	static constexpr int32 ScreenHeight = 720;

	FPlatformerStatus Status;
	FPlatformer3DLevel Level = FPlatformer3DLevel::MakeDefault();

	// カメラ（主人公が毎フレーム決める）と、カメラの向き（ラジアン。0 で +Z を向く）
	FCamera Camera;
	float32 CameraYaw = 0.0f;

	// 主人公の足元の位置と、いまの高さ（敵が、近づいてきたかを見るため）
	FVector3 PlayerPosition;

	float32 Time = 0.0f;
	bool bGoalReached = false;
	bool bGameOver = false;
	bool bAutoPlay = false;

	FCameraViewport GetViewport() const { return FCameraViewport { ScreenWidth, ScreenHeight }; }
};
