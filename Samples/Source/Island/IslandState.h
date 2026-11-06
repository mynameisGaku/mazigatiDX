// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Texture/DynamicTexture.h"
#include "Library/Framework/Asset/Gpu/IndexBuffer.h"
#include "Library/Framework/Asset/Gpu/ShadowMap.h"
#include "Library/Framework/Asset/Gpu/VertexBuffer.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Math/Math.h"

//=============================================================================
// FIslandState
//   サンプル Island の、オブジェクトどうしで共有する状態（地形・探検家・結晶・木・時刻・カメラ・画像とバッファ）。
//   シーンが 1 つ作って、各オブジェクトに TSharedPtr で渡す。地形と木は、決まった種から作るので、毎回同じ島になる。
//=============================================================================
struct FIslandState
{
	// 3D を見せる範囲（画面の座標。ピクセル）。上の 80 は、見出しの分
	static constexpr int32 ViewLeft = 0;
	static constexpr int32 ViewTop = 80;
	static constexpr int32 ViewWidth = 1280;
	static constexpr int32 ViewHeight = 640;

	// 地形: 格子のマスの数（片側）と、1 マスの大きさ（ワールドの単位）。島は -60〜60 の正方形に収まる
	static constexpr int32 TerrainCells = 80;
	static constexpr float32 CellSize = 1.5f;
	static constexpr float32 TerrainHalfSize = TerrainCells * CellSize * 0.5f;

	// 海面の高さ。これより低い所は歩けない（少し上まで、波打ち際として避ける）
	static constexpr float32 SeaLevel = 0.0f;
	static constexpr float32 ShoreMargin = 0.35f;

	// 結晶の数
	static constexpr int32 NumCrystals = 8;

	// 一日の長さ（秒）。時刻は 0〜1（0.25 が昼前、0.5 が昼、0.75 が夕方）
	static constexpr float32 DayLength = 90.0f;

	// 結晶 1 つ
	struct FCrystal
	{
		FVector3 Position;
		bool bTaken = false;
		// 拾った時刻（サンプルの時間。輪が広がる演出に使う）
		float32 TakenTime = 0.0f;
	};

	// 木 1 本（根元の位置と、大きさの倍率）
	struct FTree
	{
		FVector3 Position;
		float32 Scale = 1.0f;
	};

	// 切り替えられる設定（数字キー）
	bool bShadows = true;
	bool bFog = true;
	bool bTimeRunning = true;

	// サンプルを始めてからの時間（秒）と、一日の時刻（0〜1）
	float32 Time = 0.0f;
	float32 DayTime = 0.32f;

	// 地形の高さ（(TerrainCells + 1) x (TerrainCells + 1)。行が z、列が x）
	TArray<float32> Heights;

	// 探検家: 足元の位置・向き（ラジアン。0 が +z）・歩く速さ（今の）・ひとりで歩くか・目的地（右クリックで決める）
	FVector3 ExplorerPosition;
	float32 ExplorerYaw = 0.0f;
	float32 ExplorerSpeed = 0.0f;
	bool bAutoWalk = true;
	bool bHasTarget = false;
	FVector3 Target;

	// カメラの軌道: 横・縦の回転（ラジアン）と、探検家からの距離
	float32 CameraYaw = 0.6f;
	float32 CameraPitch = 0.55f;
	float32 CameraDistance = 18.0f;

	// メインのカメラ。画面全体（1280 x 720）を映すものとして計算し、光軸が通る点を、3D を見せる範囲の中心へずらしてある
	FCamera Camera;

	// 灯台の足元（北の浜の近く）
	FVector3 LighthousePosition;

	// 結晶・木
	TArray<FCrystal> Crystals;
	TArray<FTree> Trees;
	int32 NumTaken = 0;
	// 全部拾った時刻（全部拾うと、少しして、結晶が戻る）。拾い切っていなければ負
	float32 AllTakenTime = -1.0f;

	// 地形の頂点・添字のバッファ（作ったら変わらない）
	TSharedPtr<CVertexBuffer> TerrainVertices;
	TSharedPtr<CIndexBuffer> TerrainIndices;

	// 影の絵・光のにじみの画像・地図の画像
	TSharedPtr<CShadowMap> ShadowMap;
	TSharedPtr<CDynamicTexture> GlowTexture;
	TSharedPtr<CDynamicTexture> MapTexture;

	// カメラが映す画面（描画先）の大きさ
	FCameraViewport GetScreenViewport() const { return FCameraViewport { 1280, 720 }; }
};
