// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/Asset/Texture/PixelImage.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Math/Math.h"

//=============================================================================
// FCullLight
//   カリングに使う点の光。Position を中心に、Radius の球の内側だけが、光を受ける（半径の端で、光が 0 になる光）。
//=============================================================================
struct FCullLight
{
	FVector3 Position;
	float32 Radius = 0.0f;
};

//=============================================================================
// FTileLightGridDesc
//=============================================================================
struct FTileLightGridDesc
{
	// タイル 1 枚の大きさ（ピクセル。画面を、この大きさの格子に分ける）
	int32 TileSize = 32;
	// タイル 1 枚の一覧に入る光の数の上限（3 の倍数。一覧は、画素 1 つに 3 つの光の番号を詰める）。超えたタイルは「溢れた」印を付け、シェーダーが全部の光を評価する（欠落させない）
	int32 MaxLightsPerTile = 24;
};

//=============================================================================
// CTileLightGrid
//   画面をタイルに分け、タイルごとに、光が届く点光源の一覧を作る（Forward+ / Deferred+ のライトカリング。CPU の基準実装）。
//   タイルは、カメラから Far の面まで伸びる四角錐。光の球と、四角錐の 4 つの側面・Near と Far の範囲を比べる。見落とさない（球が少しでも触れるタイルには、必ず入る）が、
//   四角錐の角の近くでは、触れていないタイルに入ることもある（保守的）。奥行きの範囲は、画面の中の物の実際の奥行きでなく、Near から Far まで全部として扱う。
//   光の一覧は、光の番号の昇順（光の足し算の順が、全部の光を評価するときと同じになり、結果が一致する）。
//
//   一覧は、8 ビット RGBA の画像へ詰めて（WriteImage）、シェーダーが読む（EngineContent/Shaders/LightCulling/TileLightList.hlsli）。
//   画像は、タイル 1 枚が横に TexelsPerTile 画素、タイルの並びの形（幅 = タイルの列 x TexelsPerTile、高さ = タイルの行）。1 画素の R・G・B に、光の番号 + 1 を 1 つずつ（0 は、一覧の終わり）。
//   溢れたタイルは、先頭の画素の R が 255（光の番号 + 1 は 254 までなので、区別できる）。光は MaxLights（254）個まで。
//
//   CTileLightGrid Grid;
//   Grid.Build( Camera, FCameraViewport { 1280, 640 }, Lights, NumLights );
//   Grid.WriteImage( Image );          // 同じ大きさなら、画素だけ書き換える
//   Texture->Update( Image );
//=============================================================================
class CTileLightGrid
{
public:
	// 扱える光の数（光の番号 + 1 を 8 ビットに詰め、255 は溢れの印に使う）
	static constexpr int32 MaxLights = 254;
	// 1 画素に詰める光の数（R・G・B）
	static constexpr int32 LightsPerTexel = 3;

	// 一覧を作る。カメラ・映す先・光・設定のどれかが使えない（透視射影でない・大きさが 0・光が多すぎる・半径や位置が有限でない・設定が範囲の外）ときは、何も持たず false。
	// 半径が 0 以下の光は、どのタイルにも入らない
	bool Build( const FCamera& Camera, const FCameraViewport& Viewport, const FCullLight* Lights, int32 NumLights, const FTileLightGridDesc& Desc = FTileLightGridDesc() );

	// 持っている一覧を捨てる
	void Reset();

	// 作れているか
	bool IsValid() const { return NumTilesX > 0; }

	int32 GetNumTilesX() const { return NumTilesX; }
	int32 GetNumTilesY() const { return NumTilesY; }
	int32 GetTileSize() const { return Desc.TileSize; }
	int32 GetMaxLightsPerTile() const { return Desc.MaxLightsPerTile; }
	// 画像の中の、タイル 1 枚の横の画素の数（MaxLightsPerTile / 3）
	int32 GetTexelsPerTile() const { return Desc.MaxLightsPerTile / LightsPerTexel; }
	int32 GetNumLights() const { return NumLights; }

	// タイル (TileX, TileY) に入った光の数（溢れたタイルは、MaxLightsPerTile）。範囲の外は 0
	int32 GetTileCount( int32 TileX, int32 TileY ) const;
	// タイルに入った光の、Index 番目（0 から）の光の番号。範囲の外は -1
	int32 GetTileLight( int32 TileX, int32 TileY, int32 Index ) const;
	// タイルが溢れたか（上限を超える光が、届いていた）。範囲の外は false
	bool IsTileOverflowed( int32 TileX, int32 TileY ) const;

	// 溢れたタイルの数と、全タイルの一覧の長さの合計（溢れたタイルは、上限まで）
	int32 GetNumOverflowedTiles() const { return NumOverflowed; }
	int32 GetTotalEntries() const { return TotalEntries; }

	// 一覧を、8 ビット RGBA の画像へ詰める。Out が、必要な大きさでなければ作り直し、同じ大きさなら、全画素を書き換える。作れていなければ、何もせず false
	bool WriteImage( FPixelImage& Out ) const;

	// 必要な画像の大きさ（作れていなければ 0）
	int32 GetImageWidth() const { return NumTilesX * GetTexelsPerTile(); }
	int32 GetImageHeight() const { return NumTilesY; }

private:
	FTileLightGridDesc Desc;
	int32 NumTilesX = 0;
	int32 NumTilesY = 0;
	int32 NumLights = 0;
	int32 NumOverflowed = 0;
	int32 TotalEntries = 0;
	// タイルごとの光の数（溢れたら上限）と、溢れの印
	TArray<uint8> Counts;
	TArray<uint8> Overflowed;
	// タイルごとの光の番号（MaxLightsPerTile 個ずつ）
	TArray<uint8> Entries;
};
