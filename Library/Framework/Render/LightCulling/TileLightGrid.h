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
	// クラスタ 1 つの一覧に入る光の数の上限（3 の倍数。一覧は、画素 1 つに 3 つの光の番号を詰める）。超えたクラスタは「溢れた」印を付け、シェーダーが全部の光を評価する（欠落させない）
	int32 MaxLightsPerTile = 24;
	// 奥行きの分割数。1 は、画面だけを分けるタイル。2 以上は、Near から Far までを、奥行きの比が等しい（対数の）層に分けたクラスタ（タイル x 層）
	int32 NumDepthSlices = 1;
};

//=============================================================================
// CTileLightGrid
//   画面をタイルに分け（奥行きも分けるときは、さらに層に分け）、タイル（クラスタ）ごとに、光が届く点光源の一覧を作る（Forward+ / Deferred+ のライトカリング。CPU の基準実装）。
//   タイルは、カメラから Far の面まで伸びる四角錐。光の球と、四角錐の 4 つの側面・奥行きの範囲を比べる。見落とさない（球が少しでも触れるクラスタには、必ず入る）が、
//   四角錐の角の近くや層の境目では、触れていないクラスタに入ることもある（保守的）。奥行きを分けないときは、奥行きの範囲を Near から Far まで全部として扱う。
//   奥行きを分けると、層 k の範囲は Near x (Far / Near)^(k / 層の数) から次の層の始まりまで。画素の層は、画素の奥行き（カメラの前向きの距離）から、GetSliceOfDepth（シェーダーの SliceOfDepth と同じ式）で決まる。
//   層の境目の丸め誤差で、画素が隣の層に入っても欠落しないよう、層の範囲は、前後に少し（SliceMargin）広げる。Near より手前は最初の層、Far より奥は最後の層に入る（シェーダーと同じ）。
//   光の一覧は、光の番号の昇順（光の足し算の順が、全部の光を評価するときと同じになり、結果が一致する）。
//
//   一覧は、8 ビット RGBA の画像へ詰めて（WriteImage）、シェーダーが読む（EngineContent/Shaders/LightCulling/TileLightList.hlsli）。
//   画像は、クラスタ 1 つが横に TexelsPerTile 画素。幅 = タイルの列 x TexelsPerTile、高さ = 層の数 x タイルの行（行 = 層 x タイルの行の数 + タイルの行）。1 画素の R・G・B に、光の番号 + 1 を 1 つずつ（0 は、一覧の終わり）。
//   溢れたクラスタは、先頭の画素の R が 255（光の番号 + 1 は 254 までなので、区別できる）。光は MaxLights（254）個まで。
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
	// 奥行きの分割数の上限
	static constexpr int32 MaxDepthSlices = 64;
	// 層の範囲を前後に広げる割合（層の境目の丸め誤差に、欠落が負けないため）
	static constexpr float32 SliceMargin = 0.001f;

	// 一覧を作る。カメラ・映す先・光・設定のどれかが使えない（透視射影でない・大きさが 0・光が多すぎる・半径や位置が有限でない・設定が範囲の外。奥行きを分けるときは、0 < Near < Far でない）ときは、何も持たず false。
	// 半径が 0 以下の光は、どのクラスタにも入らない
	bool Build( const FCamera& Camera, const FCameraViewport& Viewport, const FCullLight* Lights, int32 NumLights, const FTileLightGridDesc& Desc = FTileLightGridDesc() );

	// 持っている一覧を捨てる
	void Reset();

	// 作れているか
	bool IsValid() const { return NumTilesX > 0; }

	// 奥行き Depth（カメラの前向きの距離）の層の番号。Near より手前は 0、Far より奥は最後の層。シェーダー（TileLightList.hlsli の SliceOfDepth）と同じ式。NumSlices が 1 なら、いつも 0
	static int32 GetSliceOfDepth( float32 Depth, float32 Near, float32 Far, int32 NumSlices );

	int32 GetNumTilesX() const { return NumTilesX; }
	int32 GetNumTilesY() const { return NumTilesY; }
	int32 GetNumSlices() const { return NumSlices; }
	// クラスタの数（タイルの数 x 層の数）
	int32 GetNumClusters() const { return NumTilesX * NumTilesY * NumSlices; }
	int32 GetTileSize() const { return Desc.TileSize; }
	int32 GetMaxLightsPerTile() const { return Desc.MaxLightsPerTile; }
	// 画像の中の、クラスタ 1 つの横の画素の数（MaxLightsPerTile / 3）
	int32 GetTexelsPerTile() const { return Desc.MaxLightsPerTile / LightsPerTexel; }
	int32 GetNumLights() const { return NumLights; }
	// 層の境目の奥行き。Boundary は 0（Near）〜層の数（Far）。範囲の外、または作れていなければ 0
	float32 GetSliceDepth( int32 Boundary ) const;

	// クラスタ（タイル (TileX, TileY)・層 Slice）に入った光の数（溢れたクラスタは、MaxLightsPerTile）。範囲の外は 0。奥行きを分けないときは、Slice を省ける
	int32 GetTileCount( int32 TileX, int32 TileY, int32 Slice = 0 ) const;
	// クラスタに入った光の、Index 番目（0 から）の光の番号。範囲の外は -1
	int32 GetTileLight( int32 TileX, int32 TileY, int32 Index, int32 Slice = 0 ) const;
	// クラスタが溢れたか（上限を超える光が、届いていた）。範囲の外は false
	bool IsTileOverflowed( int32 TileX, int32 TileY, int32 Slice = 0 ) const;

	// 溢れたクラスタの数と、全クラスタの一覧の長さの合計（溢れたクラスタは、上限まで）
	int32 GetNumOverflowedTiles() const { return NumOverflowed; }
	int32 GetTotalEntries() const { return TotalEntries; }

	// 一覧を、8 ビット RGBA の画像へ詰める。Out が、必要な大きさでなければ作り直し、同じ大きさなら、全画素を書き換える。作れていなければ、何もせず false
	bool WriteImage( FPixelImage& Out ) const;

	// 必要な画像の大きさ（作れていなければ 0）
	int32 GetImageWidth() const { return NumTilesX * GetTexelsPerTile(); }
	int32 GetImageHeight() const { return NumTilesY * NumSlices; }

private:
	// クラスタの番号（一覧の並びの位置）。範囲の外は -1
	int32 GetClusterIndex( int32 TileX, int32 TileY, int32 Slice ) const;

	FTileLightGridDesc Desc;
	int32 NumTilesX = 0;
	int32 NumTilesY = 0;
	int32 NumSlices = 0;
	int32 NumLights = 0;
	int32 NumOverflowed = 0;
	int32 TotalEntries = 0;
	float32 NearDepth = 0.0f;
	float32 FarDepth = 0.0f;
	// クラスタごとの光の数（溢れたら上限）と、溢れの印
	TArray<uint8> Counts;
	TArray<uint8> Overflowed;
	// クラスタごとの光の番号（MaxLightsPerTile 個ずつ）
	TArray<uint8> Entries;
};
