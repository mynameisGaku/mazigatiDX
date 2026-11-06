// SPDX-License-Identifier: MIT
#pragma once

#include "Source/AI/AIActor.h"

class CStateMachineComponent;

//=============================================================================
// CAIHero
//   キーボードで動かす主人公。動きは、ステートマシン（Samples/Assets/AI/Hero.gxstate）が決める。
//   このクラスがすることは、キー入力をブラックボードへ書くこと（入力センサー）だけ。
//     A / D か 左右キー: 歩く（MoveAxis）   Space か J: 攻撃（AttackPressed。押した瞬間だけ真になる Trigger）
//   Idle / Run / Attack / Hurt のどれにいるか、いつ遷移するかは、Hero.gxstate のデータで決まる（コードに enum と switch はない）。
//=============================================================================
class CAIHero : public CAIActor
{
public:
	CAIHero( const FVector2& InPosition );

	void OnSpawn() override;
	void Tick( float32 DeltaTime ) override;

	CStateMachineComponent* GetMachine() const { return Machine; }

protected:
	void OnDamaged() override;

private:
	CStateMachineComponent* Machine = nullptr;
};
