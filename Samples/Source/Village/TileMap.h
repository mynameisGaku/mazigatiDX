// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/PixelImage.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"
#include "Source/Village/VillageArt.h"
#include "Source/Village/VillageState.h"

// タイルの種類（タイルの画像は、この順に横に並ぶ）
enum class EVillageTile : uint8
{
	Grass,
	Flowers,
	Path,
	WaterA,
	WaterB,
	Tree,
	Roof,
	Wall,
	Door,
	Count,
};

//=============================================================================
// CTileMap
//   村の地図。タイルの並び（草・花・道・水・木・家）を作り、見えている範囲のタイルだけを描く。
//   通れるか（IsAreaBlocked）、見回りの道すじ・金貨・街灯・村人の位置も、ここが決める。
//   世界の座標は、ピクセル。左上が (0, 0)。
//=============================================================================
class CTileMap : public CGameObject
{
public:
	// タイル 1 つの大きさ（ピクセル）と、地図の大きさ（タイルの数）
	static constexpr int32 TileSize = NVillageArt::TileSize;
	static constexpr int32 Columns = 56;
	static constexpr int32 Rows = 36;

	CTileMap( const TSharedPtr<FVillageState>& InState, const TSharedPtr<CTexture>& InTileset );

	void Draw( FDrawContext& CommandList ) override;

	// タイル (Column, Row) の種類。範囲の外は木（通れない）
	EVillageTile GetTile( int32 Column, int32 Row ) const;

	// 世界の点・矩形（中心と半分の大きさ）が、通れないタイルに触れているか
	bool IsBlockedAt( float32 X, float32 Y ) const;
	bool IsAreaBlocked( const FVector2& Center, float32 HalfWidth, float32 HalfHeight ) const;

	// 見回りの道すじ（世界の座標の点。最後の点から、最初の点へもつながる）と、金貨・街灯の位置、村人の足元、主人公の最初の位置
	const TArray<FVector2>& GetRoute() const { return Route; }
	const TArray<FVector2>& GetCoinPositions() const { return CoinPositions; }
	const TArray<FVector2>& GetLampPositions() const { return LampPositions; }
	FVector2 GetVillagerPosition() const { return VillagerPosition; }
	FVector2 GetStartPosition() const { return Route.Num() > 0 ? Route[0] : FVector2(); }

	// 地図の全体を、1 タイル 1 画素にした画像（ミニマップ用）
	FPixelImage MakeOverview() const;

private:
	void Generate();

	// 点 (FromColumn, FromRow) から点 (ToColumn, ToRow)（どちらもタイルの角。同じ列か同じ行）の間を、2 タイル幅の道にする
	void CarveBand( int32 FromColumn, int32 FromRow, int32 ToColumn, int32 ToRow );

	// 家（左上のタイルと、4 x 3 の大きさ）を置く
	void PlaceHouse( int32 Column, int32 Row );

	// 周りの (Radius) タイルの中に、道・水・家があるか
	bool HasNearby( int32 Column, int32 Row, int32 Radius ) const;

	void SetTile( int32 Column, int32 Row, EVillageTile Tile );
	static bool IsBlocking( EVillageTile Tile );

	TSharedPtr<FVillageState> State;
	TSharedPtr<CTexture> Tileset;

	TArray<EVillageTile> Tiles;
	TArray<FVector2> Route;
	TArray<FVector2> CoinPositions;
	TArray<FVector2> LampPositions;
	FVector2 VillagerPosition;
};
