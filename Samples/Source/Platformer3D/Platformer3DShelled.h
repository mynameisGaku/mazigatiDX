// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Platformer3D/Platformer3DEnemy.h"

//=============================================================================
// CPlatformer3DShelled
//   甲羅の敵。歩いているとき踏むと、甲羅にこもって止まる。止まった甲羅は、ふれる（または、また踏む）と蹴り出せて、
//   すべる甲羅は、ぶつかった敵を倒す（主人公にも当たる）。状態: Walking → Still（止まった甲羅）→ Sliding（すべる甲羅）。
//   2D 版（CPlatformer2DShelled）と同じ仕組み。
//=============================================================================
class CPlatformer3DShelled : public CPlatformer3DEnemy
{
public:
	CPlatformer3DShelled( const TSharedPtr<FPlatformer3DState>& InState, const FVector3& InFeetPosition );

	void Record3D( FDrawContext& CommandList ) override;

	bool IsDeadly() const override;
	bool IsStill() const override { return Mode == EMode::Still; }
	bool IsDangerousToEnemies() const override { return Mode == EMode::Sliding; }
	void OnStomped( const FVector3& PlayerPosition ) override;
	void OnKicked( const FVector3& PlayerPosition ) override;

protected:
	void OnTickActive( float32 DeltaTime ) override;

private:
	enum class EMode : uint8
	{
		Walking,
		Still,
		Sliding,
	};

	EMode Mode = EMode::Walking;

	// 蹴り出した直後の、主人公にダメージを与えない時間
	float32 KickGrace = 0.0f;
};
