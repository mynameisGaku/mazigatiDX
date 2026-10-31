// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/IndexBuffer.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Asset/VertexBuffer.h"
#include "Library/Framework/Render/Camera.h"
#include "Library/Math/Math.h"

//=============================================================================
// FShapes3DDemoState
//   サンプル Shapes3D の、オブジェクトどうしで共有する状態（カメラ・数字キーで切り替える設定・舞台で使う画像）。
//   シーンが 1 つ作って、各オブジェクトに TSharedPtr で渡す。
//=============================================================================
struct FShapes3DDemoState
{
	// 3D を見せる範囲（画面の座標。ピクセル）。上の 80 は、見出しの分
	static constexpr int32 ViewLeft = 0;
	static constexpr int32 ViewTop = 80;
	static constexpr int32 ViewWidth = 1280;
	static constexpr int32 ViewHeight = 640;

	// 頂点の並びの Culling の段階（3 キー）: なし・裏を描かない・表を描かない
	static constexpr int32 NumCullingModes = 3;

	// 森の木の数の段階（7 キー）
	static constexpr int32 NumForestLevels = 4;
	static constexpr int32 ForestSizes[NumForestLevels] = { 0, 16, 48, 128 };

	// 切り替えられる設定
	bool bWireCones = false;
	bool bFog = false;
	int32 CullingMode = 0;
	bool bStandOnGround = true;
	bool bTranslucent = false;
	bool bLighting = true;
	int32 ForestLevel = 2;

	// メインのカメラ。画面全体（1280 x 720）を映すものとして計算し、光軸が通る点を、3D を見せる範囲の中心へずらしてある
	FCamera Camera;

	// サンプルを始めてからの時間（秒）
	float32 Time = 0.0f;

	// 舞台で使う画像。Draw で使うので、Tick の前に作っておく
	TSharedPtr<CTexture> FloorTexture;
	TSharedPtr<CTexture> GrassTexture;
	TSharedPtr<CTexture> FlagTexture;
	TSharedPtr<CTexture> StarTexture;
	TSharedPtr<CTexture> HeartTexture;
	TSharedPtr<CTexture> LanternTexture;

	// 森の木 1 本の形（頂点バッファ + インデックスバッファ。一度作って、位置を変えて何度も描く）
	TSharedPtr<CVertexBuffer> TreeVertices;
	TSharedPtr<CIndexBuffer> TreeIndices;

	// 池の水面（格子。頂点バッファは、毎フレーム書き換える。インデックスバッファは、変わらない）
	TSharedPtr<CVertexBuffer> WaterVertices;
	TSharedPtr<CIndexBuffer> WaterIndices;

	// カメラが映す画面（描画先）の大きさ
	FCameraViewport GetScreenViewport() const { return FCameraViewport { 1280, 720 }; }
};
