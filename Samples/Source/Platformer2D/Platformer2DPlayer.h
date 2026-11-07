// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

struct FPlatformer2DState;

//=============================================================================
// CPlatformer2DPlayer
//   主人公。走る（加速・減速・Shift で速く）、ジャンプ（ボタンを押している間は高く跳べる）、敵を踏む、ブロックを下から叩く、
//   アイテムを拾う、ファイアのとき火の玉を撃つ、穴に落ちる / 敵に当たってやられる、ゴールする。
//   当たりの結果（大きさの段階・無敵・点数・残機）は、FPlatformerStatus のルールに任せる。
//   操作: A / D または左右キー = 動く、Shift = 走る、Space / Z = ジャンプ、X / J = 火の玉
//=============================================================================
class CPlatformer2DPlayer : public CGameObject
{
public:
	explicit CPlatformer2DPlayer( const TSharedPtr<FPlatformer2DState>& InState );

	void OnSpawn() override;
	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

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
	FControls ThinkAutoPlay( float32 DeltaTime );

	// 足元の中心から、いまの姿の高さ
	float32 GetHeight() const;

	void HitBlock( int32 TileX, int32 TileY );
	void CollectTouching();
	void HandleEnemies( bool bJumpHeld );
	void Hurt();
	void StartDeath();
	void StartAgain();
	void ShootFireball();
	void UpdateCamera();
	void TickGoal( float32 DeltaTime );
	void TickDeath( float32 DeltaTime );

	TSharedPtr<FPlatformer2DState> State;
	FVector2 Position;
	FVector2 Velocity;
	float32 PreviousFeetY = 0.0f;
	float32 Facing = 1.0f;
	bool bOnGround = false;
	bool bDying = false;
	float32 DeathTime = 0.0f;
	float32 AirTime = 0.0f;
	float32 AutoFireTimer = 0.0f;
	float32 GoalSlide = 0.0f;
};
