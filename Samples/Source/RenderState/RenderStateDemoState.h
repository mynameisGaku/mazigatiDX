// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Render/Camera.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Math/Math.h"

//=============================================================================
// FRenderStateDemoState
//   サンプル RenderState の、オブジェクトどうしで共有する状態（カメラ・数字キーで切り替える描画状態・舞台で使う画像）。
//   シーンが 1 つ作って、各オブジェクトに TSharedPtr で渡す。
//=============================================================================
struct FRenderStateDemoState
{
	// 3D を見せる範囲（画面の座標。ピクセル）。上の 80 は、見出しの分
	static constexpr int32 ViewLeft = 0;
	static constexpr int32 ViewTop = 80;
	static constexpr int32 ViewWidth = 1280;
	static constexpr int32 ViewHeight = 640;

	// 半透明のガラスの深度の使い方（1 キー）
	enum class EGlassDepth : uint8
	{
		Normal,     // 深度を書く（描く順番で、奥のガラスが手前のガラスに隠れる）
		ReadOnly,   // 深度を比べるだけで書かない（ガラスどうしが重なって見える）
	};

	// フォグ（3 キー）
	enum class EFogChoice : uint8
	{
		Off,
		Linear,
		Exponential,
		ExponentialSquared,
		Count,
	};

	// 地面の画像の拡大・縮小の色の決め方（5 キー）
	enum class EGroundFilter : uint8
	{
		Nearest,
		Bilinear,
		Anisotropic,
		Count,
	};

	// 切り替えられる描画状態
	EGlassDepth GlassDepth = EGlassDepth::ReadOnly;
	EDrawCulling CardCulling = EDrawCulling::BackFaces;
	EFogChoice Fog = EFogChoice::Linear;
	bool bHeightFog = true;
	EGroundFilter GroundFilter = EGroundFilter::Anisotropic;
	// 異方性フィルタリングの強さ（1・2・4・8・16。6 キーで上げる）
	int32 GroundAnisotropy = 16;
	bool bScrollWater = true;
	// 手前に描く光線の深度を消すか（8 キー）
	bool bBeamClearsDepth = true;

	// メインのカメラ。画面全体（1280 x 720）を映すものとして計算し、光軸が通る点を、3D を見せる範囲の中心へずらしてある
	FCamera Camera;

	// サンプルを始めてからの時間（秒）
	float32 Time = 0.0f;

	// 舞台で使う画像（地面の市松模様、壁の見本、水面）。Draw で使うので、Tick の前に作っておく
	TSharedPtr<CTexture> GroundTexture;
	TSharedPtr<CTexture> WallTexture;
	TSharedPtr<CTexture> WaterTexture;

	// カメラが映す画面（描画先）の大きさ
	FCameraViewport GetScreenViewport() const { return FCameraViewport { 1280, 720 }; }
};
