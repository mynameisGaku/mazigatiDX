// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Physics/PhysicsCharacterTypes.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Vector3.h"

class FPhysicsCharacter;

//=============================================================================
// ICharacterControllers
//   操作するキャラクター（歩く・跳ぶ・段差を登る・動く床に乗る・剛体を押す）を作る・壊す窓口。
//   実装: CPhysicsSystem / CPhysicsSubsystem（Jolt）。ゲームの側はこのインターフェースと
//   Library/Physics/PhysicsCharacter.h（作ったキャラクターを操作する FPhysicsCharacter。どちらも Jolt を含まない）だけを知ればよい。
//
//   ICharacterControllers* Characters = GetScene()->GetSubsystem<ICharacterControllers>();   // 物理を使うシーンにだけある（無ければ nullptr）
//   FPhysicsCharacter* Player = Characters->CreateCharacter( SpawnPosition, FCharacterSettings() );
//   Player->SetMoveVelocity( Input * 4.0f );   // 毎フレーム、歩きたい向きと速さ（m/s）を渡す
//   if ( Jump ) Player->Jump( 5.0f );
//
//   - 物理の Update が、固定ステップごとに、キャラクターを動かす（重力・ジャンプ・段差・坂・動く床・剛体との押し合いを含む）
//   - 位置 Position は足元の中心。設定が不正（NaN・無限大・半径や高さの矛盾・範囲外のレイヤー）のときは、ログを出して nullptr を返す
//   - 作ったキャラクターは CPhysicsSystem が持つ。DestroyCharacter か Shutdown まで、返したポインターは有効
//   - bCreateBody の剛体（FPhysicsCharacter::GetBody）は、キャラクターが持つ。DestroyBody では壊せない（警告して何もしない）。
//     DestroyAllBodies では、キャラクターは外れる（FPhysicsCharacter::IsAttached が false）。入れ物は DestroyCharacter まで残る
//   - 物理の Update と同時には呼ばない
//=============================================================================
class ICharacterControllers
{
public:
	virtual ~ICharacterControllers() = default;

	// キャラクターを作る。Position は足元の中心（ワールド）。失敗は nullptr
	virtual FPhysicsCharacter* CreateCharacter( const FVector3& Position, const FCharacterSettings& Settings ) = 0;

	// キャラクターを壊す（剛体も一緒に壊れる）。このワールドで作ったものでなければ何もしない。nullptr も無視する
	virtual void DestroyCharacter( FPhysicsCharacter* Character ) = 0;
	virtual void DestroyAllCharacters() = 0;

	// 持っているキャラクターの数（外れて入れ物だけ残っているものも数える）
	virtual int32 GetCharacterCount() const = 0;
};
