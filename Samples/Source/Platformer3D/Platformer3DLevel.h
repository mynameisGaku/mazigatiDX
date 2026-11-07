// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Math/Math.h"

//=============================================================================
// FPlatformer3DLevel
//   3D アクションの面。軸に平行な箱（地面・足場・土管・ブロック）の集まりで、コインとゴールの位置も持つ。
//   動くものの、箱との当たり（Move）と、ブロックを下から叩いたときの結果（Hit）もここにある。
//   座標はメートル。Y が上、Z が奥（面は +Z の向きに進む）。動くものの位置は、足元の中心。
//=============================================================================

enum class EBlock3DKind : uint8
{
	Ground,     // 地面・足場
	Wall,       // 階段・足場（動かない箱）
	Pipe,       // 土管（見た目が違うだけの、動かない箱）
	Brick,      // レンガ（大きいと壊せる）
	Question,   // コインのブロック
	PowerUp,    // パワーアップのブロック
	StarBlock,  // スターのブロック
	Used,       // 叩いたあと
};

struct FBlock3D
{
	FVector3 Min;
	FVector3 Max;
	EBlock3DKind Kind = EBlock3DKind::Ground;

	// 叩かれて持ち上がっている時間（見た目だけ。0.2 秒で戻る）
	float32 BumpTime = 0.0f;

	bool IsActive = true;
};

// 動いたあとの結果
struct FBlock3DMoveResult
{
	bool bOnGround = false;
	bool bHitCeiling = false;
	bool bHitWall = false;
	int32 CeilingBlock = -1;
};

// 下から叩いた結果
enum class EBlock3DHit : uint8
{
	None,
	Bumped,
	Broke,
	Coin,
	PowerUp,
	Star,
};

// 面を作るときに決まる、敵の置き場所（Kind: 'w' 追いかける敵、's' 甲羅の敵）
struct FBlock3DSpawn
{
	char Kind = 'w';
	FVector3 Position;
};

class FPlatformer3DLevel
{
public:
	TArray<FBlock3D> Blocks;
	TArray<FBlock3DSpawn> Spawns;

	// まだ取っていないコイン（中心の位置）
	TArray<FVector3> Coins;

	FVector3 PlayerStart;
	FVector3 GoalPosition;

	// 落ちたら、やられる高さ
	static constexpr float32 KillHeight = -12.0f;

	static FPlatformer3DLevel MakeDefault();

	// 箱を足す
	void Add( const FVector3& Min, const FVector3& Max, EBlock3DKind Kind );

	// 位置 Position（足元の中心）、大きさ（半分の幅 HalfWidth、高さ Height）の箱を、速さ Velocity で DeltaTime 動かす。
	// X → Z → Y の順に、箱にぶつかったら止める（Velocity のその軸は 0 になる）
	FBlock3DMoveResult Move( FVector3& Position, float32 HalfWidth, float32 Height, FVector3& Velocity, float32 DeltaTime ) const;

	// 点 (X, Z) の下にある、いちばん高い足場の上面（From の高さ + Tolerance 以下のもの）。無ければ KillHeight より低い値
	float32 GetGroundHeight( float32 X, float32 Z, float32 From ) const;

	// その高さ（From）から見て、すぐ下（Depth まで）に足場があるか。歩く敵が、崖で止まるために使う
	bool HasGroundBelow( float32 X, float32 Z, float32 From, float32 Depth ) const;

	// 箱（足元の中心・半分の幅・高さ）が、どれかの箱に重なっているか
	bool Overlaps( const FVector3& Position, float32 HalfWidth, float32 Height ) const;

	// ブロックを下から叩いた。bBig なら、レンガを壊せる。叩けるものなら、種類を変えて、結果を返す
	EBlock3DHit Hit( int32 Index, bool bBig );

	// 持ち上がりの時間を進める
	void Tick( float32 DeltaTime );
};
