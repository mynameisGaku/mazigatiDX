// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Render/Camera.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Math/Math.h"

// 景色に置く物の形
enum class EDemoShape : uint8
{
	Sphere,
	Cube,
	Capsule,
};

// 景色に置く物 1 つ
struct FDemoObject
{
	FString Name;
	EDemoShape Shape = EDemoShape::Sphere;
	// 中心の位置。Radius は、形の大きさ（球・カプセルは半径、立方体は半分の辺の長さ）
	FVector3 Position;
	float32 Radius = 1.0f;
	FColor32 Color = FColor32::White;

	// 視錐台との交差や、マウスの指し示しに使う、物をすっぽり囲む球の半径
	float32 GetBoundingRadius() const
	{
		switch ( Shape )
		{
		case EDemoShape::Cube:
			return Radius * 1.75f;
		case EDemoShape::Capsule:
			return Radius * 2.2f;
		default:
			return Radius;
		}
	}
};

//=============================================================================
// FCameraDemoState
//   サンプル Camera の、オブジェクトどうしで共有する状態（カメラ・景色の物・見えているか・マウスが指している物）。
//   シーンが 1 つ作って、各オブジェクトに TSharedPtr で渡す。
//=============================================================================
struct FCameraDemoState
{
	// 3D を見せる範囲（画面の座標。ピクセル）。上の 80 は、見出しの分
	static constexpr int32 ViewLeft = 0;
	static constexpr int32 ViewTop = 80;
	static constexpr int32 ViewWidth = 1280;
	static constexpr int32 ViewHeight = 640;

	// 上から見る図（右下の窓）の大きさと位置（画面の座標）
	static constexpr int32 TopViewSize = 300;
	static constexpr int32 TopViewLeft = ViewLeft + ViewWidth - 16 - TopViewSize;
	static constexpr int32 TopViewTop = ViewTop + ViewHeight - 16 - TopViewSize;

	// メインのカメラ。画面全体（1280 x 720）を映すものとして計算し、光軸が通る点を、3D を見せる範囲の中心へずらしてある
	FCamera Camera;

	// 景色の物と、そのうち、メインのカメラの視錐台に入っているもの（同じ番号が対応する）
	TArray<FDemoObject> Objects;
	TArray<bool> Visible;

	// マウスが指している物の番号（無ければ -1）
	int32 HoveredIndex = -1;

	// サンプルを始めてからの時間（秒）
	float32 Time = 0.0f;

	// カメラが映す画面（描画先）の大きさ。世界の点が画面のどこに見えるかを、画面の座標のまま求められる
	FCameraViewport GetScreenViewport() const { return FCameraViewport { 1280, 720 }; }

	int32 CountVisible() const
	{
		int32 Count = 0;
		for ( const bool bVisible : Visible )
		{
			Count += bVisible ? 1 : 0;
		}
		return Count;
	}
};
