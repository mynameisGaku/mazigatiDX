// SPDX-License-Identifier: MIT
#pragma once

//=============================================================================
// Adventure が、ステートマシン・ビヘイビアツリーへ登録する、ゲーム固有の部品
//   フレームワークの AI は、部品を名前で呼ぶだけ。この部品は、JSON（.gxstate / .gxbt）から名前で呼ばれて、
//   持ち主（CAdventurePlayer / CAdventureEnemy）の動きの関数を呼ぶだけ。
//
//   主人公のステートの動き（Player.gxstate）  Adv.Player.Idle / Run / Attack / Roll / Hurt / Dead
//   敵の体のステートの動き（Enemy.gxstate）   Adv.Enemy.Hurt / Dead / Remove
//   敵の頭脳の Task（Enemy.gxbt）             Adv.Enemy.Strike / Chase / Patrol
//=============================================================================
namespace NAdventureParts
{
	// 1 回だけ登録する（2 回目以降は何もしない）
	void Register();
}
