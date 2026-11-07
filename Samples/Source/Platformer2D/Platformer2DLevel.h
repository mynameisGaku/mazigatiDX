// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Math/Math.h"

//=============================================================================
// FPlatformer2DLevel
//   横スクロールの面。正方形のタイル（32 px）の並びで、地面・ブロック・土管・コインを持つ。
//   動くものの、タイルとの当たり（Move）と、ブロックを下から叩いたときの結果（Hit）もここにある。
//   座標は、面の左上を原点にした、ピクセル（Y は下向き）。動くものの位置は、足元の中心。
//
//   タイルの文字:  ' ' 空  '#' 地面  'B' レンガ  '?' コインのブロック  'M' パワーアップのブロック  '*' スターのブロック
//                  'U' 叩いたあとのブロック  'p' 土管  'o' コイン（取れる）  'G' ゴールのポール（通れる）
//=============================================================================

// 動いたあとの結果
struct FTile2DMoveResult
{
	bool bOnGround = false;
	bool bHitCeiling = false;
	bool bHitWall = false;
	int32 CeilingTileX = -1;
	int32 CeilingTileY = -1;
};

// 下から叩いた結果
enum class ETile2DHit : uint8
{
	None,
	Bumped,
	Broke,
	Coin,
	PowerUp,
	Star,
};

// 面を作るときに決まる、敵の置き場所（Kind: 'w' 歩く敵、's' 甲羅の敵）
struct FTile2DSpawn
{
	char Kind = 'w';
	FVector2 Position;
};

// 叩かれて、ぽんと持ち上がっているブロック（見た目だけ）
struct FTile2DBump
{
	int32 X = 0;
	int32 Y = 0;
	float32 Time = 0.0f;
};

class FPlatformer2DLevel
{
public:
	static constexpr int32 TileSize = 32;

	int32 Width = 0;
	int32 Height = 0;
	TArray<FTile2DSpawn> Spawns;
	TArray<FTile2DBump> Bumps;
	FVector2 PlayerStart;

	// ゴールのポールの、横の位置（ピクセル）
	float32 GoalX = 0.0f;

	// 作る面（固定。毎回同じ）
	static FPlatformer2DLevel MakeDefault();

	// 範囲の外は、左右が壁（地面と同じ）、上は空、下も空（穴に落ちる）
	char Get( int32 X, int32 Y ) const;
	void Set( int32 X, int32 Y, char Tile );
	bool IsSolid( int32 X, int32 Y ) const;

	// 位置 Position（足元の中心）、大きさ（半分の幅 HalfWidth、高さ Height）の箱を、速さ Velocity で DeltaTime 動かす。
	// 横 → 縦の順に、タイルにぶつかったら止める（Velocity のその軸は 0 になる）
	FTile2DMoveResult Move( FVector2& Position, float32 HalfWidth, float32 Height, FVector2& Velocity, float32 DeltaTime ) const;

	// 箱がタイルにふれているか（'o' や 'G' のような、通れるタイルを調べるのに使う）。見つけたタイルの位置を返す
	bool FindTileTouching( char Tile, const FVector2& Position, float32 HalfWidth, float32 Height, int32& OutX, int32& OutY ) const;

	// ブロックを下から叩いた。bBig なら、レンガを壊せる。叩けるものなら、タイルを変えて、結果を返す
	ETile2DHit Hit( int32 X, int32 Y, bool bBig );

	// 持ち上がりの時間を進める
	void Tick( float32 DeltaTime );

	// 画面の座標へ
	static int32 ToTile( float32 Pixel );

private:
	TArray<char> Tiles;
	void Fill( int32 X0, int32 Y0, int32 X1, int32 Y1, char Tile );
};
