// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Math/Math.h"
#include "Source/Adventure/AdventureActor.h"

struct FAdventureState;
class CStateMachineComponent;
class CBehaviorTreeComponent;
class CBlackboard;

// 敵の種類ごとの、能力（変わらないデータ）
struct FEnemyKind
{
	const char* Name;
	int32 Hp;
	float32 Speed;
	float32 SightRange;
	float32 AttackRange;
	int32 Damage;
	float32 Radius;
};

//=============================================================================
// CAdventureEnemy
//   敵（スライム・スケルトン）。タグ "Enemy"（プロパティ "Kind"）の置き物に結び付ける。
//     - 頭脳: ビヘイビアツリー（Samples/Assets/Adventure/Enemy.gxbt）。殴れる距離なら殴る / 見つけたら追いかける / 巡回する。
//     - 体の状態: ステートマシン（Enemy.gxstate）。生きている / 傷ついて のけぞる / 倒れる / 消える。
//     - センサー（このクラスの Tick）: 主人公が見えるか・殴れる距離か・動けるか、をブラックボードへ書く。
//   頭脳の Task と、体の状態の動き（StepChase / StepPatrol / BeginStrike …）は、AdventureParts.cpp の部品が呼ぶ。
//   倒すと、回復薬を落とすことがある。倒したことは、セーブに残る（読み込むと、もう居ない）。
//=============================================================================
class CAdventureEnemy : public CAdventureActor
{
public:
	CAdventureEnemy( const TSharedPtr<FAdventureState>& InState, const FString& InKindName, int32 InPlaceholderId );

	void OnSpawn() override;
	void Tick( float32 DeltaTime ) override;
	void Record3D( FDrawContext& CommandList ) override;

	bool IsAlive() const { return !bDead; }
	// 倒された（倒れる動きの途中も含む）
	bool IsDefeated() const { return bDead; }
	FVector3 GetPosition() const { return Position; }
	float32 GetRadius() const { return Kind->Radius; }

	// 傷を受ける。Hp が 0 になれば、倒れる。From は、攻撃してきた位置（のけぞる向きを決める）
	void ApplyDamage( int32 Damage, const FVector3& From );

	//-------------------------------------------------------------------------
	// 頭脳の Task と、体のステートの動き（部品が呼ぶ）
	//-------------------------------------------------------------------------
	// 主人公へ向かって歩く（殴れる距離の手前で止まる）
	void StepChase( float32 DeltaTime );

	// 巡回: 目標の点（家の近く）へ歩く。着いたら true。目標は、OnEnterPatrol で決める
	void BeginPatrol();
	bool StepPatrol( float32 DeltaTime );

	// 殴る: 構え（赤く光る）→ 当たり → 戻る。終わったら true
	void BeginStrike();
	bool StepStrike( float32 DeltaTime );

	void BeginHurt();
	void StepHurt( float32 DeltaTime );
	void BeginDead();
	void Remove();

private:
	// 家（置き物の位置）を、最初に決める
	void EnsureHome();

	// 位置を動かして、壁・柱にめり込まないようにする
	void MoveBy( const FVector3& Delta );

	float32 GetDistanceToPlayer() const;

	TSharedPtr<FAdventureState> State;
	CStateMachineComponent* Machine = nullptr;
	CBehaviorTreeComponent* Brain = nullptr;
	const FEnemyKind* Kind = nullptr;
	int32 PlaceholderId;
	FVector3 Position;
	FVector3 Home;
	bool bHasHome = false;
	FVector3 PatrolTarget;
	FVector3 KnockDirection;
	float32 FacingYaw = 0.0f;
	int32 Hp = 1;
	bool bDead = false;
	float32 StrikeTime = 0.0f;
	bool bStruckPlayer = false;
	float32 FlashTime = 0.0f;
	// ヒットストップの間の、見た目の震えの大きさ（斬られたときに入る）
	float32 ShiverAmount = 0.0f;
	float32 DeadTime = 0.0f;
	uint32 RandomState = 12345u;
};
