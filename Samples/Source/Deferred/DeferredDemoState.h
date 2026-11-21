// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Gpu/Shader.h"
#include "Library/Framework/Asset/Texture/RenderTarget.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Framework/Render/Deferred/GBuffer.h"
#include "Library/Math/Math.h"

//=============================================================================
// FDeferredDemoState
//   サンプル Deferred の、オブジェクトどうしで共有する状態（カメラ・キーで切り替える設定・シェーダー・描画先）。
//   シーンが 1 つ作って、各オブジェクトに TSharedPtr で渡す。
//=============================================================================
struct FDeferredDemoState
{
	// 3D を見せる範囲（画面の座標。ピクセル）。上の 80 は、見出しの分。場面は、この大きさの描画先に描いて、最後の合成で画面に貼る
	static constexpr int32 ViewLeft = 0;
	static constexpr int32 ViewTop = 80;
	static constexpr int32 ViewWidth = 1280;
	static constexpr int32 ViewHeight = 640;

	// 動く光の数（場面の定数の配列の大きさと同じ）
	static constexpr int32 NumLights = 24;

	// 見せるもの（1 キー）: 仕上がり・地の色・法線・位置（深度から求めた位置を、模様にしたもの）・深度・自分で光る色
	static constexpr int32 NumViewModes = 6;

	// 露出（Q / E キー）の範囲と、1 回押したときの倍率
	static constexpr float32 MinExposure = 0.25f;
	static constexpr float32 MaxExposure = 4.0f;
	static constexpr float32 ExposureStep = 1.25f;

	// 切り替えられる設定
	int32 ViewMode = 0;
	bool bDeferred = true;
	bool bTimeRunning = true;
	float32 Exposure = 1.0f;

	// メインのカメラ（場面の描画先の大きさ 1280 x 640 を映すものとして計算する）
	FCamera Camera;

	// サンプルを始めてからの時間（秒。3 キーで止まる）
	float32 Time = 0.0f;

	// 場面のシェーダー: 頂点シェーダー 1 つと、Forward で描くピクセルシェーダー 2 つ（物・地面）、G-buffer へ書くピクセルシェーダー 2 つ（物・地面）
	TSharedPtr<CShader> SceneVertex;
	TSharedPtr<CShader> ForwardObjectPixel;
	TSharedPtr<CShader> ForwardGroundPixel;
	TSharedPtr<CShader> GeometryObjectPixel;
	TSharedPtr<CShader> GeometryGroundPixel;
	// Deferred のライティング（ShaderRect で描く）と、画面への合成（トーンマップ）
	TSharedPtr<CShader> LightingPixel;
	// Deferred 経路の半透明（ライティングのあとの場面の色へ、G-buffer の深度で奥の画素を捨てながら重ねる）。Forward 経路は、物のピクセルシェーダーに、不透明度を出させて重ねる
	TSharedPtr<CShader> TransparentPixel;
	TSharedPtr<CShader> CompositePixel;

	// 描画先
	//   GBuffer: 地の色・法線・深度・自分で光る色（Deferred のジオメトリのパスが書く）
	//   SceneColor: 光を当てた場面の色（16 ビットの浮動小数点。1 を超える明るさが残る。Forward は、ここへ直接描くので、奥行きバッファを持つ）
	CGBuffer GBuffer;
	TSharedPtr<CRenderTarget> SceneColor;

	// カメラが映す画面（場面の描画先）の大きさ
	FCameraViewport GetSceneViewport() const { return FCameraViewport { ViewWidth, ViewHeight }; }
};
