// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// FHitStop: ヒットストップ（攻撃が当たった瞬間などに、ゲームの時間を一瞬だけ止める）
//   止めている間は、ゲームの経過時間が 0 になる。止める長さは、実時間（秒）。有無で、手応えが大きく変わる。
//   ふつうは、シーンが持っているもの（CScene::StartHitStop）を使う。シーンのオブジェクト・コンポーネント・システムの時間が、まとめて止まる。
//   このクラスは、時間の計算だけ（シーンを使わない所でも、使える）。
//
//   FHitStop HitStop;
//   HitStop.Start( FHitStop::SecondsFromPower( 12.0f ) );      // 攻撃力から、長さを決める
//   const float32 GameDelta = HitStop.Advance( DeltaTime );   // 毎フレーム。止まっている間は 0
//   const float32 Offset = HitStop.GetShiver() * 0.1f;        // 止まっている間の、見た目の震え（位置に足す）
//
//   - 止めている間に、また Start を呼ぶと、残りと、新しい長さの、長いほうになる（短い止めで、長い止めが切れない）。
//   - 震え（GetShiver）は、見た目だけに使う（当たり判定の位置は、動かさない）。受けた側を大きく、与えた側を小さく震わせ、地上では横だけに使うとよい。
//=============================================================================
class FHitStop
{
public:
	// 1 回の止めの上限（秒）。これより長い指定は、ここまでに収める
	static constexpr float32 MaxSeconds = 1.0f;

	// Seconds 秒（実時間）止める。0 以下・有限でない数は、何もしない。止めている間なら、残りと比べて、長いほう
	void Start( float32 Seconds );
	// 止めるのを、やめる
	void Cancel();

	// 実時間を DeltaTime 秒進めて、ゲームの経過時間を返す。止まっている間は 0。このフレームの途中で解けたら、解けたあとの分。
	// 負・有限でない DeltaTime は、0 として扱う
	float32 Advance( float32 DeltaTime );

	bool IsActive() const { return Remaining > 0.0f; }
	// 残りの秒（止まっていなければ 0）
	float32 GetRemaining() const { return Remaining; }
	// いまの止めの、全体の秒（止まっていなければ 0）
	float32 GetDuration() const { return IsActive() ? Duration : 0.0f; }

	// 見た目の震え（-1〜1）。止めた直後が大きく、解けるころに 0 へ収まる。1/60 秒ごとに、向きが入れ替わる。止まっていなければ 0
	float32 GetShiver() const;

	// 攻撃力（ダメージの数など）から、止める秒を決める: ( 2 + Power x 0.35 ) フレーム（1 フレーム = 1/60 秒）x Scale。
	// 1/60 秒以上、0.5 秒以下に収める。Scale は、技ごとの係数（強い技は大きく）
	static float32 SecondsFromPower( float32 Power, float32 Scale = 1.0f );

private:
	float32 Remaining = 0.0f;
	float32 Duration = 0.0f;
};
