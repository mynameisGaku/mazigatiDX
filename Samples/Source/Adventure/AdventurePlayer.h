// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Math/Math.h"
#include "Source/Adventure/AdventureActor.h"

struct FAdventureState;
class CStateMachineComponent;

//=============================================================================
// CAdventurePlayer
//   主人公。どのステート（待つ・走る・斬る・転がる・のけぞる・倒れる）にいるか、いつ遷移するかは、ステートマシン
//   （Samples/Assets/Adventure/Player.gxstate）が決める。このクラスは、
//     - センサー（キー入力を、ブラックボードへ書く。人の入力が、AI と同じ入口）
//     - 各ステートの動き（StepRun / BeginAttack / StepRoll …。ステートマシンの部品が呼ぶ）
//     - 当たり判定（斬る → 敵、敵 → 主人公 = ApplyDamage）、調べる（E）、回復薬（F）
//   だけを持つ。
//   操作: W A S D = 動く（カメラから見た向き）、← → = カメラを回す、Space / J = 斬る、Shift / K = 転がる（無敵）、
//         E = 調べる（宝箱・門・焚き火）、F = 回復薬
//=============================================================================
class CAdventurePlayer : public CAdventureActor
{
public:
	explicit CAdventurePlayer( const TSharedPtr<FAdventureState>& InState );

	void OnSpawn() override;
	void Tick( float32 DeltaTime ) override;
	void Record3D( FDrawContext& CommandList ) override;

	// 敵から傷を受ける。転がっている間・傷ついた直後・倒れたあとは、受けない。From は、攻撃してきた位置
	void ApplyDamage( int32 Damage, const FVector3& From );

	// 位置を決める（読み込み・最初の位置）
	void TeleportTo( const FVector3& Position );

	// 倒れていたら、立ち上がる（読み込み・やり直しのあと。ステートマシンへ、イベントを送る）
	void Revive();

	//-------------------------------------------------------------------------
	// ステートの動き（ステートマシンの部品が、ステートに入る / いる / 出るときに呼ぶ）
	//-------------------------------------------------------------------------
	void StepIdle( float32 DeltaTime );
	void StepRun( float32 DeltaTime );
	void BeginAttack();
	void StepAttack( float32 DeltaTime );
	void BeginRoll();
	void StepRoll( float32 DeltaTime );
	void EndRoll();
	void BeginHurt();
	void StepHurt( float32 DeltaTime );
	void BeginDead();

private:
	// キー入力を読んで、向き（MoveDirection）と、ブラックボードの値（MoveAmount・AttackPressed・RollPressed）を決める
	void SenseInput( float32 DeltaTime );

	// 近くの、調べられるものを探して、案内を決める。E が押されたら、調べる
	void UpdateInteraction( bool bInteractPressed );

	// 斬る: 前にいる敵に、傷を与える
	void StrikeEnemies();

	// 位置を動かして、壁・柱にめり込まないようにする
	void Move( const FVector3& Delta );

	void UpdateCameraTarget();

	TSharedPtr<FAdventureState> State;
	CStateMachineComponent* Machine = nullptr;
	FVector3 Position;

	// いま入力している、世界での向き（長さ 0〜1）
	FVector3 MoveDirection;

	// 向いている向き（ラジアン。0 で +Z）
	float32 FacingYaw = 0.0f;
	float32 AttackTime = 0.0f;
	bool bStruck = false;
	FVector3 RollDirection;
	FVector3 KnockDirection;
	float32 InvulnerableTime = 0.0f;
	float32 FlashTime = 0.0f;
	float32 DeadTime = 0.0f;
};
