// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Texture/RenderTarget.h"
#include "Library/Framework/Asset/Gpu/Shader.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Math/Math.h"

//=============================================================================
// FPostEffectsDemoState
//   サンプル PostEffects の、オブジェクトどうしで共有する状態（カメラ・キーで切り替える設定・シェーダー・描画先）。
//   シーンが 1 つ作って、各オブジェクトに TSharedPtr で渡す。
//=============================================================================
struct FPostEffectsDemoState
{
	// 3D を見せる範囲（画面の座標。ピクセル）。上の 80 は、見出しの分。場面は、この大きさの描画先に描いて、最後の合成で画面に貼る
	static constexpr int32 ViewLeft = 0;
	static constexpr int32 ViewTop = 80;
	static constexpr int32 ViewWidth = 1280;
	static constexpr int32 ViewHeight = 640;

	// ブルーム（光のにじみ）を作る描画先の大きさ（場面の半分。ぼかしは、小さい絵でするほうが軽く、広くにじむ）
	static constexpr int32 BloomWidth = ViewWidth / 2;
	static constexpr int32 BloomHeight = ViewHeight / 2;

	// 光る玉の数（場面の定数の配列の大きさと同じ）
	static constexpr int32 NumOrbs = 6;

	// 見せるもの（1 キー）: 仕上がり・8 ビット相当（1 で切れる）・ブルームだけ・深度（MRT で書いた 2 つ目の出力）
	static constexpr int32 NumViewModes = 4;

	// トーンマップ（2 キー）: ACES・Reinhard・なし（1 で切る）
	static constexpr int32 NumToneMaps = 3;

	// 露出（Q / E キー）の範囲と、1 回押したときの倍率
	static constexpr float32 MinExposure = 0.25f;
	static constexpr float32 MaxExposure = 4.0f;
	static constexpr float32 ExposureStep = 1.25f;

	// 切り替えられる設定
	int32 ViewMode = 0;
	int32 ToneMap = 0;
	bool bBloom = true;
	bool bFog = true;
	bool bTimeRunning = true;
	float32 Exposure = 1.0f;

	// メインのカメラ（場面の描画先の大きさ 1280 x 640 を映すものとして計算する）
	FCamera Camera;

	// サンプルを始めてからの時間（秒。5 キーで止まる）
	float32 Time = 0.0f;

	// シェーダー。場面は、頂点シェーダー 1 つと、ピクセルシェーダー 2 つ（物・地面）。どちらのピクセルシェーダーも、色と深度の 2 つを出力する
	TSharedPtr<CShader> SceneVertex;
	TSharedPtr<CShader> ObjectPixel;
	TSharedPtr<CShader> GroundPixel;
	// ポストエフェクト（ShaderRect で描く。頂点シェーダーは組み込みのもの）: 明るい所を抜き出す・ぼかす・合成する
	TSharedPtr<CShader> BrightPixel;
	TSharedPtr<CShader> BlurPixel;
	TSharedPtr<CShader> CompositePixel;

	// 描画先
	//   SceneColor: 場面の色（16 ビットの浮動小数点。1 を超える明るさが残る。3D を描くので、奥行きバッファを持つ）
	//   SceneDepth: 場面の深度（32 ビットの浮動小数点 x 1 チャンネル。カメラからの距離 / Far。場面を描くとき、2 つ目の出力 SV_Target1 で同時に書く）
	//   BloomA / BloomB: ブルーム（16 ビットの浮動小数点・半分の大きさ）。明るい所を A に抜き出し、A → B → A とぼかす
	TSharedPtr<CRenderTarget> SceneColor;
	TSharedPtr<CRenderTarget> SceneDepth;
	TSharedPtr<CRenderTarget> BloomA;
	TSharedPtr<CRenderTarget> BloomB;

	// カメラが映す画面（場面の描画先）の大きさ
	FCameraViewport GetSceneViewport() const { return FCameraViewport { ViewWidth, ViewHeight }; }
};
