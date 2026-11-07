// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"
#include "Source/Common/PlatformerBody.h"

struct FPlatformer2DState;
class CStateMachineComponent;

//=============================================================================
// CPlatformer2DEnemy
//   敵の基底。どのステートにいるか（歩く・止まる・すべる・つぶれる・吹き飛ぶ…）と、いつ遷移するかは、ステートマシン
//   （Samples/Assets/Platformer/*.gxstate。CStateMachineComponent）が決める。このクラスは、ステートごとの「動き」
//   （IPlatformerEnemyBody: 歩く・止まる・飛ぶ…）と、外から触れる窓口（踏まれた・蹴られた・吹き飛ばされた = イベントを送る）を持つ。
//   種類ごとの違い（見た目・どのステートを持つか）は、子のクラスと、その .gxstate が決める。
//   主人公・弾・甲羅は、この基底のメソッドだけを通して、敵に触れる（敵の種類を知らなくてよい）。
//   画面の近くにいるときだけ動く（遠いと、歩きも滑りもしない）。
//=============================================================================
class CPlatformer2DEnemy : public CGameObject, public IPlatformerEnemyBody
{
public:
	// InStatePath は、ステートマシンのファイル（資産のルートからの相対）
	CPlatformer2DEnemy( const TSharedPtr<FPlatformer2DState>& InState, const FVector2& InFeetPosition, float32 InHalfWidth, float32 InHeight, const char* InStatePath );

	void OnSpawn() override;
	void Tick( float32 DeltaTime ) override;

	// 箱（足元の中心・半分の幅・高さ）と重なっているか
	bool Overlaps( const FVector2& OtherFeet, float32 OtherHalfWidth, float32 OtherHeight ) const;

	FORCEINLINE const FVector2& GetPosition() const { return Position; }
	FORCEINLINE float32 GetTopY() const { return Position.y - Height; }

	// 生きているか（吹き飛ばされた・つぶされたあとは false。イベントを送った瞬間から）
	FORCEINLINE bool IsAlive() const { return !bKnockedOff && !bSquashed; }

	// ふれると、主人公がダメージを受けるか
	virtual bool IsDeadly() const { return true; }

	// 動かない甲羅か（横からふれると、蹴れる）
	virtual bool IsStill() const { return false; }

	// 動いていて、ほかの敵に当たると、倒せるか（すべる甲羅）
	virtual bool IsDangerousToEnemies() const { return false; }

	// 上から踏まれた。PlayerX は主人公の中心の位置（イベント "Stomped" を送る）
	virtual void OnStomped( float32 PlayerX );

	// 横から蹴られた（イベント "Kicked" を送る。動かない甲羅だけが受ける）
	virtual void OnKicked( float32 PlayerX );

	// 吹き飛ばされて、やられる（イベント "KnockedOff" を送る）。Direction は、飛んでいく向き（-1 / 1）
	void KnockOff( float32 Direction );

	//-------------------------------------------------------------------------
	// IPlatformerEnemyBody（ステートの動き）
	//-------------------------------------------------------------------------
	void StepWalk( float32 DeltaTime, float32 Speed, bool bAvoidEdges ) override;
	void StepRest( float32 DeltaTime ) override;
	void BeginSquash() override;
	void BeginFly() override;
	void StepFly( float32 DeltaTime ) override;
	void Remove() override;

protected:
	// ステートマシンの、そのステートの中にいるか（"Still" など）と、そのステートに入ってからの秒数
	bool IsInMachineState( const char* Path ) const;
	float32 GetMachineStateTime() const;

	// 画面の近くにいるか（遠いと、動かない）
	bool IsActive() const;

	TSharedPtr<FPlatformer2DState> State;
	CStateMachineComponent* Machine = nullptr;
	FString StatePath;
	FVector2 Position;
	FVector2 Velocity;
	float32 HalfWidth;
	float32 Height;
	float32 Direction = -1.0f;
	float32 FlyDirection = 1.0f;

	// 最後に触れた主人公の位置（蹴られた向きを決める）
	float32 LastPlayerX = 0.0f;
	bool bSquashed = false;
	bool bKnockedOff = false;
};
