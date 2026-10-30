// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Render/Camera.h"
#include "Library/Math/Math.h"

//=============================================================================
// FLightingDemoState
//   サンプル Lighting の、オブジェクトどうしで共有する状態（カメラ・数字キーで切り替えるライティング・舞台で使う画像）。
//   シーンが 1 つ作って、各オブジェクトに TSharedPtr で渡す。
//=============================================================================
struct FLightingDemoState
{
	// 3D を見せる範囲（画面の座標。ピクセル）。上の 80 は、見出しの分
	static constexpr int32 ViewLeft = 0;
	static constexpr int32 ViewTop = 80;
	static constexpr int32 ViewWidth = 1280;
	static constexpr int32 ViewHeight = 640;

	// 全体の環境光の強さの段階（4 キー）
	static constexpr int32 NumAmbientLevels = 4;

	// 足せる街灯の数の上限（8 キー）。ライトは同時に 6 つまでなので、月・電球・スポットライトと合わせると、超える
	static constexpr int32 MaxExtraLamps = 6;

	// 切り替えられるライティング
	bool bMoon = true;
	bool bLamp = true;
	bool bSpotlight = true;
	int32 AmbientLevel = 1;
	bool bPixelLighting = true;
	bool bSpecular = true;
	bool bWrapLighting = false;
	int32 ExtraLamps = 2;
	bool bLighting = true;

	// メインのカメラ。画面全体（1280 x 720）を映すものとして計算し、光軸が通る点を、3D を見せる範囲の中心へずらしてある
	FCamera Camera;

	// サンプルを始めてからの時間（秒）
	float32 Time = 0.0f;

	// 床の画像（市松模様）。Draw で使うので、Tick の前に作っておく
	TSharedPtr<CTexture> FloorTexture;

	// カメラが映す画面（描画先）の大きさ
	FCameraViewport GetScreenViewport() const { return FCameraViewport { 1280, 720 }; }
};
