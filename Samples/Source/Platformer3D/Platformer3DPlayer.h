// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Math/Math.h"
#include "Source/Common/PlatformerBody.h"
#include "Source/Platformer3D/Platformer3DActor.h"

struct FPlatformer3DState;
class CStateMachineComponent;
class CBehaviorTreeComponent;

//=============================================================================
// CPlatformer3DPlayer
//   3D の主人公。カメラの向きに合わせて走る（加速・減速・Shift で速く）、ジャンプ（押している間は高く）、敵を踏む、
//   ブロックを下から叩く、コインとアイテムを拾う、ファイアのとき火の玉を撃つ、穴に落ちる / 敵に当たってやられる、ゴールする。
//   生きている・やられた・ゴール・ゲームオーバーの切り替えと、姿の段階（ちいさい / おおきい / ファイア / 小さくなった直後）の
//   遷移は、ステートマシン（Samples/Assets/Platformer/Player.gxstate。階層。2D 版と同じファイル）が決める。
//   このクラスは、各ステートの「動き」（IPlatformerPlayerBody）と、センサー（残機・おおきいかを、ブラックボードへ書く）と、
//   イベント（Mushroom / Flower / Hurt / Died / Goal）を送ることだけ。カメラは、主人公の後ろから追う。
//   操作: W A S D = 動く（カメラから見た向き）、← → または Q / E = カメラを回す、Shift = 走る、Space = ジャンプ、X / J = 火の玉
//=============================================================================
class CPlatformer3DPlayer : public CPlatformer3DActor, public IPlatformerPlayerBody, public IPlatformerPilotBody
{
public:
	explicit CPlatformer3DPlayer( const TSharedPtr<FPlatformer3DState>& InState );

	void OnSpawn() override;
	void Tick( float32 DeltaTime ) override;
	void Record3D( FDrawContext& CommandList ) override;

	//-------------------------------------------------------------------------
	// IPlatformerPlayerBody（ステートの動き）
	//-------------------------------------------------------------------------
	void OnFormEntered( EPlatformerForm Form ) override;
	void BeginHurtBreather() override;
	void StepAlive( float32 DeltaTime ) override;
	void EnterAlive() override;
	void BeginDeath() override;
	void StepDying( float32 DeltaTime ) override;
	void BeginGoal() override;
	void StepGoal( float32 DeltaTime ) override;
	void BeginGameOver() override;

	// IPlatformerPilotBody（自動操作のセンサー）
	void Sense( CBlackboard& Blackboard ) override;

private:
	// 1 フレームぶんの操作（キー、または、自動で遊ぶときの判断）。Move はカメラから見た向き（X = 右、Y = 前）
	struct FControls
	{
		FVector2 Move;
		float32 Turn = 0.0f;
		bool bRun = false;
		bool bJumpPressed = false;
		bool bJumpHeld = false;
		bool bFirePressed = false;
	};

	FControls ReadControls( float32 DeltaTime );
	FControls ReadPilotControls();

	float32 GetHeight() const;

	void HitBlock( int32 BlockIndex );
	void CollectTouching();
	void HandleEnemies( bool bJumpHeld );
	void PostEvent( const char* Name );
	void ShootFireball();
	void UpdateCamera();

	TSharedPtr<FPlatformer3DState> State;
	CStateMachineComponent* Machine = nullptr;

	// 自動操作（--autoplay）のビヘイビアツリー
	CBehaviorTreeComponent* Pilot = nullptr;
	FVector3 Position;
	FVector3 Velocity;
	float32 PreviousFeetY = 0.0f;

	// 向いている向き（ラジアン。0 で +Z）
	float32 FacingYaw = 0.0f;
	bool bOnGround = false;
	float32 DeathTime = 0.0f;
	float32 AirTime = 0.0f;

	// やられたあと、続きから始めるか（Alive に入るとき、最初の位置へ戻る）
	bool bRespawnPending = false;

	// やられている間か（小さい姿で描く）
	bool bDying = false;

	// 最初の姿（--form=big / fire）へ、ステートマシンを進めるための、送るイベント（1 フレームに 1 つ）
	TArray<FString> StartEvents;

	// ステートマシンへ送るイベント（Mushroom / Flower / Hurt / Died / Goal）。ステートマシンの Tick の最中（StepAlive など）に
	// 送ると、その Tick の終わりに消えてしまうので、いったん溜めて、Tick（Update の段階）で送る。次のフレームの遷移に使われる
	TArray<FString> OutgoingEvents;
};
