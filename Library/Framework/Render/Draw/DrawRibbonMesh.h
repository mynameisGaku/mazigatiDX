// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Render/DrawVertex3D.h"
#include "Library/Framework/Render/Draw/DrawRibbon.h"
#include "Library/Math/Vector3.h"

// 帯を展開するときの、カメラの位置と向き（帯の幅を、カメラへ向けるために使う）。
struct FDrawRibbonCamera
{
	// カメラの位置（ワールド）。
	FVector3 Position = FVector3( 0.0f, 0.0f, 0.0f );
	// カメラの右向き（単位ベクトル）。
	FVector3 Right = FVector3( 1.0f, 0.0f, 0.0f );
	// カメラの上向き（単位ベクトル）。
	FVector3 Up = FVector3( 0.0f, 1.0f, 0.0f );
	// カメラの前向き（単位ベクトル）。
	FVector3 Front = FVector3( 0.0f, 0.0f, 1.0f );
};

// 帯の展開結果（頂点と、三角形のリストの 16 ビットの番号）。
struct FDrawRibbonMesh
{
	// 幅の方向へ広げた頂点列（画像の繰り返しで分けるときは、繰り返しごとに複製される）。
	TArray<FDrawVertex3D> Vertices;
	// 三角形のリストとして、頂点を結ぶ番号。
	TArray<uint16> Indices;
};

// 帯（FDrawRibbonData と経路の点）を、カメラへ面を向けた三角形へ展開する。描画 API を知らない（DxLib・DX12 が同じ展開を使う）。
namespace NDrawRibbonMesh
{
	// 帯から三角形を作る。bSplitTexture が true なら、画像の繰り返しを、整数の U の境界で分ける（連続した U のまま渡すときは false）。
	// 値が使えない（点が 2 つ未満・有限でない・U が戻る・描ける区間が無い・分割が多すぎる）ときは false（Mesh は空）。
	bool Build( const FDrawRibbonData& Data, const FDrawRibbonData::FPoint* Points, const FDrawRibbonCamera& Camera, bool bSplitTexture, FDrawRibbonMesh& Mesh );
}
