// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/RenderTarget.h"
#include "Library/Framework/Asset/Shader.h"
#include "Library/Framework/Render/Camera.h"
#include "Library/Math/Math.h"

//=============================================================================
// FShadersDemoState
//   サンプル Shaders の、オブジェクトどうしで共有する状態（カメラ・数字キーで切り替える設定・シェーダー・場面を描く描画先）。
//   シーンが 1 つ作って、各オブジェクトに TSharedPtr で渡す。
//=============================================================================
struct FShadersDemoState
{
	// 3D を見せる範囲（画面の座標。ピクセル）。上の 80 は、見出しの分。場面は、この大きさの描画先に描いて、ポストエフェクトで画面に貼る
	static constexpr int32 ViewLeft = 0;
	static constexpr int32 ViewTop = 80;
	static constexpr int32 ViewWidth = 1280;
	static constexpr int32 ViewHeight = 640;

	// 物の見え方（1 キー）: トゥーン（段のある陰と縁の光）・法線の色・奥行き・うねり（頂点が波打つ）
	static constexpr int32 NumObjectModes = 4;

	// 地面（2 キー）: 波紋・市松模様
	static constexpr int32 NumGroundModes = 2;

	// ポストエフェクト（3 キー）: なし・白黒・周りを暗く・モザイク・ゆがみ
	static constexpr int32 NumPostModes = 5;

	// 切り替えられる設定
	int32 ObjectMode = 0;
	int32 GroundMode = 0;
	int32 PostMode = 0;
	bool bTimeRunning = true;

	// メインのカメラ（場面の描画先の大きさ 1280 x 640 を映すものとして計算する）
	FCamera Camera;

	// サンプルを始めてからの時間（秒。4 キーで止まる）
	float32 Time = 0.0f;

	// シェーダー。物と地面は、頂点シェーダーを共有する。ポストエフェクトは、ピクセルシェーダーだけ（ShaderRect は、組み込みの頂点シェーダーを使う）
	TSharedPtr<CShader> ObjectVertex;
	TSharedPtr<CShader> ObjectPixels[NumObjectModes];
	TSharedPtr<CShader> GroundPixels[NumGroundModes];
	TSharedPtr<CShader> PostPixels[NumPostModes];

	// 場面を描く描画先（ポストエフェクトは、これを画像として読む）
	TSharedPtr<CRenderTarget> SceneTarget;

	// カメラが映す画面（場面の描画先）の大きさ
	FCameraViewport GetSceneViewport() const { return FCameraViewport { ViewWidth, ViewHeight }; }
};
