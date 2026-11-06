// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/AI/AIAnimationControl.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

//=============================================================================
// CAIActor
//   AI のサンプルの登場人物の土台。丸い体で、向きと体力を持ち、クリップ（Idle / Run / Attack / Hurt）を名前で再生する。
//   フレームワークはゲームのアニメーターを知らないので、IAIAnimationControl をここで実装して、AI の部品（ステートマシン・ビヘイビアツリー）へ渡す。
//
//   - クリップの長さは決まっている（Attack と Hurt は一度きり。Idle と Run は繰り返す）。Attack は始まって 0.2 秒で "Hit" の出来事が起きる
//   - 速度は、AI の部品が SetVelocity で決める。動きの範囲は、サンプルの舞台（ArenaMin / ArenaMax）の中
//=============================================================================
class CAIActor : public CGameObject, public IAIAnimationControl
{
public:
	// 舞台の範囲（画面の座標）
	static const FVector2 ArenaMin;
	static const FVector2 ArenaMax;

	CAIActor( const FString& InName, const FVector2& InPosition, const FColor32& InColor );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

	//-------------------------------------------------------------------------
	// IAIAnimationControl
	//-------------------------------------------------------------------------
	bool PlayClip( const FString& Clip, bool bRestart ) override;
	bool IsClipFinished() const override;
	bool ConsumeClipEvent( const FString& Event ) override;
	bool HasClip( const FString& Clip ) const override;

	//-------------------------------------------------------------------------
	// AI の部品が使う
	//-------------------------------------------------------------------------
	const FVector2& GetPosition() const { return Position; }
	void SetVelocity( const FVector2& InVelocity );
	// 向きを、x の方向（正で右、負で左）へ
	void FaceToward( float32 X );
	float32 GetFacing() const { return Facing; }
	const FString& GetClip() const { return Clip; }
	float32 GetClipTime() const { return ClipTime; }
	int32 GetHp() const { return Hp; }
	const FString& GetActorName() const { return ActorName; }
	// 傷を受ける（体力を減らし、点滅する）。サブクラスが、AI へ知らせる
	void ApplyDamage( int32 Damage );

protected:
	// ApplyDamage の後に呼ばれる（AI へ「やられた」を知らせる）
	virtual void OnDamaged();

	FString ActorName;
	FVector2 Position;
	FVector2 Velocity;
	FColor32 Color;
	float32 Facing = 1.0f;
	int32 Hp = 5;
	float32 FlashTime = 0.0f;

private:
	FString Clip;
	float32 ClipTime = 0.0f;
	bool bClipLoops = true;
	bool bHitEventPending = false;
	bool bHitEventFired = false;
};
