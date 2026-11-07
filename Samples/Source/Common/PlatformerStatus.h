// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// 横スクロール・3D のアクションの、共通のルール（描かない・動かさない。数字だけを持つ）
//   Platformer2D と Platformer3D が、同じルールを使う。大きさの段階（ちいさい / おおきい / ファイア）、拾うと得られる能力、
//   当たったときの結果、コイン・点数・残機を、ここにまとめる。見た目や動きは、それぞれのサンプルのオブジェクトが決める。
//=============================================================================

// 主人公の大きさの段階。段階の遷移（キノコ・花・ダメージ）は、ステートマシン（Samples/Assets/Platformer/Player.gxstate）が決める。
// ここの Form は、ステートマシンが、いまの段階を書き込む（表示や、ブロックを壊せるか、火の玉を撃てるかを、これで見る）
enum class EPlatformerForm : uint8
{
	Small,
	Big,
	Fire,
};

// 拾えるもの
enum class EPlatformerItem : uint8
{
	Coin,
	Mushroom,
	Flower,
	Star,
};

struct FPlatformerStatus
{
	// 無敵（スター）の長さと、小さくなったあとの、ひと息の長さ（秒）
	static constexpr float32 StarSeconds = 10.0f;
	static constexpr float32 HurtSeconds = 1.5f;

	EPlatformerForm Form = EPlatformerForm::Small;
	float32 StarTime = 0.0f;
	float32 HurtTime = 0.0f;
	int32 Coins = 0;
	int32 Score = 0;
	int32 Lives = 3;

	bool IsBig() const { return Form != EPlatformerForm::Small; }
	bool CanShoot() const { return Form == EPlatformerForm::Fire; }
	bool HasStar() const { return StarTime > 0.0f; }
	bool IsInvincible() const { return StarTime > 0.0f || HurtTime > 0.0f; }

	// 時間を進める（スターと、ひと息の残りを減らす）
	void Tick( float32 DeltaTime );

	// 拾った分の、点数・コイン・スターの無敵を足す（コイン 100 枚で残機 +1）。姿が変わる（キノコ・花）のは、ステートマシンの仕事
	void Collect( EPlatformerItem Item );

	void AddScore( int32 Points ) { Score += Points; }

	// 小さくなったあとの、ひと息（無敵の時間）を始める
	void GrantBreather() { HurtTime = HurtSeconds; }

	// やられた。残機を 1 つ減らす。0 より下には減らない
	void Die();

	// やられたあと、続きから始める: 無敵なし（点数・コイン・残機はそのまま。姿はステートマシンが小さくする）
	void Respawn();

	// 全部を最初に戻す（ゲームオーバーのあと）
	void Reset();
};
