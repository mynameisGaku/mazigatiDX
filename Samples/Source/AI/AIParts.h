// SPDX-License-Identifier: MIT
#pragma once

//=============================================================================
// AI のサンプルが、ステートマシン・ビヘイビアツリーへ登録する、ゲーム固有の部品
//   フレームワークの AI は、部品を名前で呼ぶだけ。この部品（C++）は、JSON のアセットから名前（"Move" "MoveToTarget" など）で使われる。
//
//   ステートの処理（Hero.gxstate）  Halt（止まる）/ Move（MoveAxis の向きに歩く）/ HeroStrike（攻撃の出来事で、前の見張りを殴る）
//   Task（Guard.gxbt）              PlayClip / MoveToPatrolPoint / MoveToTarget / FaceTarget / Strike
//   Service（Guard.gxbt）           SensePlayer（主人公との距離を測って、HasTarget・InAttackRange・TargetPos を書く）
//=============================================================================
namespace NAISample
{
	// 1 回だけ登録する（2 回目以降は何もしない）
	void RegisterParts();
}
