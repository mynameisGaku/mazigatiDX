// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Math/Math.h"
#include "Source/Platformer3D/Platformer3DActor.h"

struct FPlatformer3DState;

//=============================================================================
// CPlatformer3DEnemy
//   3D の敵の基底。地面の上を歩く（壁と崖で向きを変える）、主人公の近くにいるときだけ動く、
//   やられたとき（踏まれた / 弾や甲羅やスターで吹き飛ばされた）の動きを持つ。
//   種類ごとの違い（動き方・見た目・踏まれたときの動き）は、子のクラスが決める。
//   主人公・弾・甲羅は、この基底のメソッドだけを通して、敵に触れる（敵の種類を知らなくてよい）。
//   2D 版（CPlatformer2DEnemy）と、同じ役割・同じメソッド名。
//=============================================================================
class CPlatformer3DEnemy : public CPlatformer3DActor
{
public:
	CPlatformer3DEnemy( const TSharedPtr<FPlatformer3DState>& InState, const FVector3& InFeetPosition, float32 InHalfWidth, float32 InHeight );

	void Tick( float32 DeltaTime ) final;

	// 箱（足元の中心・半分の幅・高さ）と重なっているか
	bool Overlaps( const FVector3& OtherFeet, float32 OtherHalfWidth, float32 OtherHeight ) const;

	FORCEINLINE const FVector3& GetPosition() const { return Position; }
	FORCEINLINE float32 GetTopY() const { return Position.y + Height; }
	FORCEINLINE bool IsAlive() const { return !bKnockedOff && !bSquashed; }

	// ふれると、主人公がダメージを受けるか
	virtual bool IsDeadly() const { return true; }

	// 動かない甲羅か（横からふれると、蹴れる）
	virtual bool IsStill() const { return false; }

	// 動いていて、ほかの敵に当たると、倒せるか（すべる甲羅）
	virtual bool IsDangerousToEnemies() const { return false; }

	// 上から踏まれた。PlayerPosition は主人公の足元
	virtual void OnStomped( const FVector3& PlayerPosition ) = 0;

	// 横から蹴られた（動かない甲羅だけ）
	virtual void OnKicked( const FVector3& PlayerPosition ) {}

	// 吹き飛ばされて、やられる。Direction は、飛んでいく向き（水平）
	void KnockOff( const FVector3& InDirection );

protected:
	// 生きていて、主人公の近くにいるときの、毎フレームの動き（既定は、向きに歩くだけ）
	virtual void OnTickActive( float32 DeltaTime );

	// 向き Direction に、速さ Speed で歩く（重力つき。壁にぶつかったら、向きを反対にする。bAvoidEdges なら、崖の前で向きを反対にする）
	void Walk( float32 DeltaTime, float32 Speed, bool bAvoidEdges );

	// つぶされた姿を残す（Seconds 秒あと、消える）
	void Squash( float32 Seconds );

	TSharedPtr<FPlatformer3DState> State;
	FVector3 Position;
	FVector3 Velocity;
	float32 HalfWidth;
	float32 Height;

	// 水平の向き（長さ 1）
	FVector3 Direction = FVector3( 0.0f, 0.0f, -1.0f );
	float32 SquashTime = 0.0f;
	bool bSquashed = false;
	bool bKnockedOff = false;
};
