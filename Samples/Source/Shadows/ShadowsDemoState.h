// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/ShadowMap.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Render/Camera.h"
#include "Library/Math/Math.h"

//=============================================================================
// FShadowsDemoState
//   サンプル Shadows の、オブジェクトどうしで共有する状態（カメラ・太陽・数字キーで切り替える設定・シャドウマップ・舞台で使う画像）。
//   シーンが 1 つ作って、各オブジェクトに TSharedPtr で渡す。
//=============================================================================
struct FShadowsDemoState
{
	// 3D を見せる範囲（画面の座標。ピクセル）。上の 80 は、見出しの分
	static constexpr int32 ViewLeft = 0;
	static constexpr int32 ViewTop = 80;
	static constexpr int32 ViewWidth = 1280;
	static constexpr int32 ViewHeight = 640;

	// シャドウマップの大きさの段階（2 キー）。大きいほど、影の輪郭が細かい
	static constexpr int32 NumSizeLevels = 4;
	static constexpr int32 MapSizes[NumSizeLevels] = { 256, 512, 1024, 2048 };

	// 影を計算する範囲の段階（3 キー）: 中庭だけ・広い範囲・カメラの見える範囲
	static constexpr int32 NumAreaModes = 3;

	// 影の判定に足す深さの補正の段階（5 キー）: 既定・0（床が自分の影で縞になる）・大きい（影が物から離れる）
	static constexpr int32 NumDepthModes = 3;

	// 影の絵を確かめる表示の大きさ（6 キー。右下）
	static constexpr int32 PreviewSize = 240;

	// 切り替えられる設定
	bool bShadows = true;
	int32 SizeLevel = 2;
	int32 AreaMode = 0;
	bool bSunMoving = true;
	int32 DepthMode = 0;
	bool bPreview = false;
	bool bLantern = true;

	// メインのカメラ。画面全体（1280 x 720）を映すものとして計算し、光軸が通る点を、3D を見せる範囲の中心へずらしてある
	FCamera Camera;

	// サンプルを始めてからの時間（秒）
	float32 Time = 0.0f;

	// 太陽の横の向き（ラジアン。動く間は増える）
	float32 SunAzimuth = 0.6f;

	// 影の絵。大きさを替えたときに作り直す（作ったときの段階が ShadowMapSizeLevel）。作り直しは、メインスレッドだけ
	TSharedPtr<CShadowMap> ShadowMap;
	int32 ShadowMapSizeLevel = -1;

	// 舞台で使う画像。Draw で使うので、Tick の前に作っておく
	TSharedPtr<CTexture> FloorTexture;
	TSharedPtr<CTexture> FenceTexture;

	// カメラが映す画面（描画先）の大きさ
	FCameraViewport GetScreenViewport() const { return FCameraViewport { 1280, 720 }; }
};
