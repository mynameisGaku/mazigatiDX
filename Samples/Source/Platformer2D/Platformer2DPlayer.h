// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"
#include "Source/Common/PlatformerBody.h"

struct FPlatformer2DState;
class CStateMachineComponent;
class CBehaviorTreeComponent;

//=============================================================================
// CPlatformer2DPlayer
//   主人公。走る（加速・減速・Shift で速く）、ジャンプ（ボタンを押している間は高く跳べる）、敵を踏む、ブロックを下から叩く、
//   アイテムを拾う、ファイアのとき火の玉を撃つ、穴に落ちる / 敵に当たってやられる、ゴールする。
//   生きている・やられた・ゴール・ゲームオーバーの切り替えと、姿の段階（ちいさい / おおきい / ファイア / 小さくなった直後）の
//   遷移は、ステートマシン（Samples/Assets/Platformer/Player.gxstate。階層）が決める。このクラスは、各ステートの「動き」
//   （IPlatformerPlayerBody）と、センサー（残機・おおきいかを、ブラックボードへ書く）と、イベント（Mushroom / Flower / Hurt / Died / Goal）を送ることだけ。
//   自動操作（--autoplay）は、ビヘイビアツリー（Autopilot2D.gxbt）が、操作をブラックボードへ書く。主人公は、それを読む。
//   操作: A / D または左右キー = 動く、Shift = 走る、Space / Z = ジャンプ、X / J = 火の玉
//=============================================================================
class CPlatformer2DPlayer : public CGameObject, public IPlatformerPlayerBody, public IPlatformerPilotBody
{
public:
	explicit CPlatformer2DPlayer( const TSharedPtr<FPlatformer2DState>& InState );

	void OnSpawn() override;
	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

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
	// 1 フレームぶんの操作（キー、または、自動で遊ぶときの判断）
	struct FControls
	{
		float32 Move = 0.0f;
		bool bRun = false;
		bool bJumpPressed = false;
		bool bJumpHeld = false;
		bool bFirePressed = false;
	};

	FControls ReadControls( float32 DeltaTime );
	FControls ReadPilotControls();

	// 足元の中心から、いまの姿の高さ
	float32 GetHeight() const;

	void HitBlock( int32 TileX, int32 TileY );
	void CollectTouching();
	void HandleEnemies( bool bJumpHeld );
	void PostEvent( const char* Name );
	void ShootFireball();
	void UpdateCamera();

	TSharedPtr<FPlatformer2DState> State;
	CStateMachineComponent* Machine = nullptr;

	// 自動操作（--autoplay）のビヘイビアツリー
	CBehaviorTreeComponent* Pilot = nullptr;
	FVector2 Position;
	FVector2 Velocity;
	float32 PreviousFeetY = 0.0f;
	float32 Facing = 1.0f;
	bool bOnGround = false;
	float32 AirTime = 0.0f;
	float32 DeathTime = 0.0f;

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
