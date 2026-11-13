// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Texture/RenderTarget.h"
#include "Library/Framework/Material/Material.h"
#include "Library/Framework/Material/PostProcessStack.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Math/Math.h"

//=============================================================================
// FMaterialsDemoState
//   サンプル Materials の、オブジェクトどうしで共有する状態（カメラ・マテリアル・ポストプロセスのチェーン・UI の矩形の状態）。
//   シーンが 1 つ作って、各オブジェクトに TSharedPtr で渡す。
//=============================================================================
struct FMaterialsDemoState
{
	// 3D を見せる範囲（画面の座標。ピクセル）。上の 80 は、見出しの分。場面は、この大きさの描画先に描いて、ポストプロセスで貼る
	static constexpr int32 ViewLeft = 0;
	static constexpr int32 ViewTop = 80;
	static constexpr int32 ViewWidth = 1280;
	static constexpr int32 ViewHeight = 640;

	// UI のマテリアルを描く矩形（画面の座標）。ボタンは、ホバーと押下で光り方が変わる。帯は、時間で光が流れる
	static constexpr int32 ButtonLeft = 24;
	static constexpr int32 ButtonTop = 640;
	static constexpr int32 ButtonWidth = 240;
	static constexpr int32 ButtonHeight = 56;
	static constexpr int32 BandLeft = 288;
	static constexpr int32 BandTop = 640;
	static constexpr int32 BandWidth = 240;
	static constexpr int32 BandHeight = 56;

	// ポストプロセスのチェーン（1〜3 キーで、1 つずつ入れる・外す）
	static constexpr int32 NumPostPasses = 3;

	// 時間（4 キーで止まる）と、ボタンの状態（マウスがのっているか・押しているか）
	float32 Time = 0.0f;
	bool bTimeRunning = true;
	bool bButtonHovered = false;
	bool bButtonPressed = false;

	FCamera Camera;

	// Surface のマテリアル（地面と柱は市松模様。球は、大理石・車の塗装・縁の光）
	TSharedPtr<CMaterial> Checker;
	TSharedPtr<CMaterial> Marble;
	TSharedPtr<CMaterial> CarPaint;
	TSharedPtr<CMaterial> RimGlow;
	// Substrate 風（フルの評価）の、手前の小さな球 6 つ: 磨いた金属（異方性）・真珠（薄膜）・ろう（サブサーフェス）・さびた金属（層を混ぜる）・
	// キャンディ塗装（層を重ねる）・槌目の金属（法線）
	static constexpr int32 NumSubstrateSpheres = 6;
	TSharedPtr<CMaterial> SubstrateSpheres[NumSubstrateSpheres];
	// 場面の光: まわりの景色の映り込み（環境マップ）と、太陽の影（影のマップ）
	TSharedPtr<CMaterialEnvironmentMap> Environment;
	TSharedPtr<CMaterialShadowMap> Shadow;
	// UI のマテリアル
	TSharedPtr<CMaterial> GlowButton;
	TSharedPtr<CMaterial> Shimmer;

	// ポストプロセス: 白黒・周りを暗く・モザイク
	CPostProcessStack PostStack;

	// 場面を描く描画先（ポストプロセスは、これを画像として読む）
	TSharedPtr<CRenderTarget> SceneTarget;

	FCameraViewport GetSceneViewport() const { return FCameraViewport { ViewWidth, ViewHeight }; }
};
