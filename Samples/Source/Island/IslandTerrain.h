// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Asset/Texture/PixelImage.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Source/Island/IslandState.h"

//=============================================================================
// NIslandTerrain
//   サンプル Island の地形。決まったシードの値ノイズで、中央が高く、まわりが海に沈む島の高さを作る。
//   高さから、頂点バッファ・インデックスバッファ（高さで色を変えた三角形の格子）と、地図の画像（上から見た島）を作り、
//   木と結晶を置く場所を決める。歩けるかの判定・地面の高さ・画面の点から地面への当たり（右クリックの目的地）もここ。
//=============================================================================
namespace NIslandTerrain
{
	// 高さを作り、木と結晶を置き、探検家を浜辺に立たせる
	void Generate( FIslandState& State );

	// 頂点・インデックスのバッファを作る（メインスレッド）。作れなければ false
	bool MakeBuffers( FIslandState& State );

	// 上から見た島の画像（格子点 1 つ = 1 画素。上が北（+z））
	FPixelImage MakeMapImage( const FIslandState& State );

	// (x, z) の地面の高さ（格子の高さを、なめらかに補う）。島の外は、海の底の高さ
	float32 GetHeight( const FIslandState& State, float32 X, float32 Z );

	// (x, z) を歩けるか（島の中で、波打ち際より高い）
	bool IsWalkable( const FIslandState& State, float32 X, float32 Z );

	// 光線が、地面（海より上）に当たる点。当たらなければ false
	bool Raycast( const FIslandState& State, const FCameraRay& Ray, FVector3& OutPoint );

	// 高さに応じた地面の色（砂・草・岩）
	FColor32 GetGroundColor( float32 Height );
}
