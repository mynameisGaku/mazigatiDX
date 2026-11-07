// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Math/Math.h"
#include "Source/Common/PlatformerStatus.h"
#include "Source/Platformer2D/Platformer2DLevel.h"

//=============================================================================
// FPlatformer2DState
//   Platformer2D のオブジェクトが、分け合う状態。面（タイル）、カメラの位置、主人公の状態（大きさ・コイン・点数・残機）、
//   ゴール / ゲームオーバーの印。シーンが作って、各オブジェクトに渡す（オブジェクトどうしは、これを通して知り合う）。
//=============================================================================
struct FPlatformer2DState
{
	static constexpr int32 ScreenWidth = 1280;
	static constexpr int32 ScreenHeight = 720;

	FPlatformerStatus Status;
	FPlatformer2DLevel Level = FPlatformer2DLevel::MakeDefault();

	// 画面の左端の、面の上の位置（ピクセル）。進む向きにしか動かない
	float32 CameraX = 0.0f;

	// 面に入ってからの時間（秒）。コインの回転などの見た目に使う
	float32 Time = 0.0f;

	bool bGoalReached = false;
	bool bGameOver = false;

	// 自動で遊ぶ（--autoplay。画面の確認と、検査のため）
	bool bAutoPlay = false;

	int32 ToScreenX( float32 WorldX ) const { return static_cast<int32>( WorldX - CameraX ); }

	// 画面の近く（左右に Margin ピクセルまで広げた範囲）か。敵は、近くにいるときだけ動く
	bool IsNearCamera( float32 WorldX, float32 Margin ) const
	{
		return WorldX >= CameraX - Margin && WorldX <= CameraX + static_cast<float32>( ScreenWidth ) + Margin;
	}
};
