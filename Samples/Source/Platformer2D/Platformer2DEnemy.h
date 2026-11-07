// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

struct FPlatformer2DState;

//=============================================================================
// CPlatformer2DEnemy
//   敵の基底。タイルの上を歩く（壁で向きを変える。崖は、そのまま落ちる）、画面の近くにいるときだけ動く、
//   やられたとき（踏まれた / 弾や甲羅やスターで吹き飛ばされた）の動きを持つ。
//   敵の種類ごとの違い（見た目・踏まれたときの動き）は、子のクラス（歩く敵 CPlatformer2DWalker、甲羅の敵 CPlatformer2DShelled）が決める。
//   主人公・弾・甲羅は、この基底のメソッドだけを通して、敵に触れる（敵の種類を知らなくてよい）。
//=============================================================================
class CPlatformer2DEnemy : public CGameObject
{
public:
	CPlatformer2DEnemy( const TSharedPtr<FPlatformer2DState>& InState, const FVector2& InFeetPosition, float32 InHalfWidth, float32 InHeight );

	void Tick( float32 DeltaTime ) final;

	// 箱（足元の中心・半分の幅・高さ）と重なっているか
	bool Overlaps( const FVector2& OtherFeet, float32 OtherHalfWidth, float32 OtherHeight ) const;

	FORCEINLINE const FVector2& GetPosition() const { return Position; }
	FORCEINLINE float32 GetTopY() const { return Position.y - Height; }

	// 生きているか（吹き飛ばされた・つぶされたあとは false）
	FORCEINLINE bool IsAlive() const { return !bKnockedOff && SquashTime <= 0.0f && !bSquashed; }

	// ふれると、主人公がダメージを受けるか
	virtual bool IsDeadly() const { return true; }

	// 動かない甲羅か（横からふれると、蹴れる）
	virtual bool IsStill() const { return false; }

	// 動いていて、ほかの敵に当たると、倒せるか（すべる甲羅）
	virtual bool IsDangerousToEnemies() const { return false; }

	// 上から踏まれた。PlayerX は主人公の中心の位置
	virtual void OnStomped( float32 PlayerX ) = 0;

	// 横から蹴られた（動かない甲羅だけ）
	virtual void OnKicked( float32 PlayerX ) {}

	// 吹き飛ばされて、やられる。Direction は、飛んでいく向き（-1 / 1）
	void KnockOff( float32 Direction );

protected:
	// 生きていて、画面の近くにいるときの、毎フレームの動き（既定は、歩くだけ）
	virtual void OnTickActive( float32 DeltaTime );

	// 向き Direction に、速さ Speed で歩く（重力つき。壁にぶつかったら、向きを反対にする）
	void Walk( float32 DeltaTime, float32 Speed );

	// つぶされた姿を残す（Seconds 秒あと、消える）
	void Squash( float32 Seconds );

	TSharedPtr<FPlatformer2DState> State;
	FVector2 Position;
	FVector2 Velocity;
	float32 HalfWidth;
	float32 Height;
	float32 Direction = -1.0f;
	float32 SquashTime = 0.0f;
	bool bSquashed = false;
	bool bKnockedOff = false;
};
