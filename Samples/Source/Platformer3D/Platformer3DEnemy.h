// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Math/Math.h"
#include "Source/Common/PlatformerBody.h"
#include "Source/Platformer3D/Platformer3DActor.h"

struct FPlatformer3DState;
class CStateMachineComponent;
class CBehaviorTreeComponent;
class CBlackboard;

//=============================================================================
// CPlatformer3DEnemy
//   3D の敵の基底。どのステートにいるか（うろうろ・追いかける・止まる・すべる・つぶれる・吹き飛ぶ…）と、いつ遷移するかは、
//   ステートマシン（Samples/Assets/Platformer/*.gxstate。CStateMachineComponent）が決める。このクラスは、ステートごとの
//   「動き」（IPlatformerEnemyBody）と、センサー（主人公が近いか、をブラックボードへ書く）と、外から触れる窓口
//   （踏まれた・蹴られた・吹き飛ばされた = イベントを送る）を持つ。2D 版（CPlatformer2DEnemy）と、同じ役割・同じ窓口。
//   主人公・弾・甲羅は、この基底のメソッドだけを通して、敵に触れる（敵の種類を知らなくてよい）。
//=============================================================================
class CPlatformer3DEnemy : public CPlatformer3DActor, public IPlatformerEnemyBody
{
public:
	CPlatformer3DEnemy( const TSharedPtr<FPlatformer3DState>& InState, const FVector3& InFeetPosition, float32 InHalfWidth, float32 InHeight, const char* InStatePath );

	void OnSpawn() override;
	void Tick( float32 DeltaTime ) override;

	// 箱（足元の中心・半分の幅・高さ）と重なっているか
	bool Overlaps( const FVector3& OtherFeet, float32 OtherHalfWidth, float32 OtherHeight ) const;

	FORCEINLINE const FVector3& GetPosition() const { return Position; }
	FORCEINLINE float32 GetTopY() const { return Position.y + Height; }

	// 生きているか（吹き飛ばされた・つぶされたあとは false。イベントを送った瞬間から）
	FORCEINLINE bool IsAlive() const { return !bKnockedOff && !bSquashed; }

	// ふれると、主人公がダメージを受けるか
	virtual bool IsDeadly() const { return true; }

	// 動かない甲羅か（横からふれると、蹴れる）
	virtual bool IsStill() const { return false; }

	// 動いていて、ほかの敵に当たると、倒せるか（すべる甲羅）
	virtual bool IsDangerousToEnemies() const { return false; }

	// 上から踏まれた。PlayerPosition は主人公の足元（イベント "Stomped" を送る）
	virtual void OnStomped( const FVector3& PlayerPosition );

	// 横から蹴られた（イベント "Kicked" を送る。動かない甲羅だけが受ける）
	virtual void OnKicked( const FVector3& PlayerPosition );

	// 吹き飛ばされて、やられる（イベント "KnockedOff" を送る）。Direction は、飛んでいく向き（水平）
	void KnockOff( const FVector3& InDirection );

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
	// 頭脳のビヘイビアツリー（.gxbt）のファイル。無ければ nullptr（ステートマシンだけで動く）
	virtual const char* GetBrainPath() const { return nullptr; }

	// センサー: ブラックボードへ、遷移・ツリーの条件に使う値を書く（既定は何もしない）
	virtual void Sense( CBlackboard& Blackboard ) {}

	bool IsInMachineState( const char* Path ) const;
	float32 GetMachineStateTime() const;

	// 主人公の近くにいるか（遠いと、動かない）
	bool IsActive() const;

	TSharedPtr<FPlatformer3DState> State;
	CStateMachineComponent* Machine = nullptr;
	CBehaviorTreeComponent* Brain = nullptr;
	FString StatePath;
	FVector3 Position;
	FVector3 Velocity;
	float32 HalfWidth;
	float32 Height;

	// 水平の向き（長さ 1）
	FVector3 Direction = FVector3( 0.0f, 0.0f, -1.0f );
	FVector3 FlyDirection = FVector3( 0.0f, 0.0f, 1.0f );

	// 最後に触れた主人公の位置（蹴られた向きを決める）
	FVector3 LastPlayerPosition;
	bool bSquashed = false;
	bool bKnockedOff = false;
};
