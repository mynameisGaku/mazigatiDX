// SPDX-License-Identifier: MIT
#pragma once

//=============================================================================
// Platformer2D / Platformer3D が、ステートマシンへ登録する、ステートの処理（部品）
//   フレームワークの AI は、部品を名前で呼ぶだけ。この部品は、JSON（.gxstate）から名前で呼ばれて、
//   持ち主の体の窓口（IPlatformerPlayerBody / IPlatformerEnemyBody）の関数を呼ぶだけ（2D と 3D で、同じ部品を使う）。
//
//   主人公（Player.gxstate）  Player.EnterAlive / SetForm / HurtBreather / Alive / Death / Goal / GameOver
//   敵（*.gxstate）           Enemy.Walk / Shell / Slide / Squash / Remove / Fly
//   敵の頭脳（Chaser3D.gxbt）  Task: Enemy.ChaseTarget / Enemy.Wander
//   自動操作（Autopilot*.gxbt） Task: Platformer.Cruise / SeekItem / Jump / Fire、Service: Platformer.Sense（ブラックボードの操作を書く）
//=============================================================================
namespace NPlatformerParts
{
	// 1 回だけ登録する（2 回目以降は何もしない）
	void Register();
}
