// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Math/Math.h"
#include "Source/Platformer3D/Platformer3DActor.h"

struct FPlatformer3DState;

//=============================================================================
// CPlatformer3DPlayer
//   3D の主人公。カメラの向きに合わせて走る（加速・減速・Shift で速く）、ジャンプ（押している間は高く）、敵を踏む、
//   ブロックを下から叩く、コインとアイテムを拾う、ファイアのとき火の玉を撃つ、穴に落ちる / 敵に当たってやられる、ゴールする。
//   当たりの結果（大きさの段階・無敵・点数・残機）は、FPlatformerStatus のルールに任せる（2D 版と同じ）。
//   カメラは、主人公の後ろから追う。
//   操作: W A S D = 動く（カメラから見た向き）、← → または Q / E = カメラを回す、Shift = 走る、Space = ジャンプ、X / J = 火の玉
//=============================================================================
class CPlatformer3DPlayer : public CPlatformer3DActor
{
public:
	explicit CPlatformer3DPlayer( const TSharedPtr<FPlatformer3DState>& InState );

	void OnSpawn() override;
	void Tick( float32 DeltaTime ) override;
	void Record3D( FDrawContext& CommandList ) override;

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
	FControls ThinkAutoPlay( float32 DeltaTime );

	float32 GetHeight() const;

	void HitBlock( int32 BlockIndex );
	void CollectTouching();
	void HandleEnemies( bool bJumpHeld );
	void Hurt();
	void StartDeath();
	void StartAgain();
	void ShootFireball();
	void UpdateCamera();
	void TickGoal( float32 DeltaTime );
	void TickDeath( float32 DeltaTime );

	TSharedPtr<FPlatformer3DState> State;
	FVector3 Position;
	FVector3 Velocity;
	float32 PreviousFeetY = 0.0f;

	// 向いている向き（ラジアン。0 で +Z）
	float32 FacingYaw = 0.0f;
	bool bOnGround = false;
	bool bDying = false;
	float32 DeathTime = 0.0f;
	float32 AirTime = 0.0f;
	float32 AutoFireTimer = 0.0f;
};
