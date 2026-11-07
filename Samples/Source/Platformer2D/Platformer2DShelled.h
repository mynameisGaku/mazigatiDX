// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Platformer2D/Platformer2DEnemy.h"

//=============================================================================
// CPlatformer2DShelled
//   甲羅の敵。歩いているとき踏むと、甲羅にこもって止まる。止まった甲羅は、ふれる（または、また踏む）と蹴り出せて、
//   すべる甲羅は、ぶつかった敵を倒す（主人公にも当たる）。状態: Walking → Still（止まった甲羅）→ Sliding（すべる甲羅）。
//=============================================================================
class CPlatformer2DShelled : public CPlatformer2DEnemy
{
public:
	CPlatformer2DShelled( const TSharedPtr<FPlatformer2DState>& InState, const FVector2& InFeetPosition );

	void Draw( FDrawContext& CommandList ) override;

	bool IsDeadly() const override;
	bool IsStill() const override { return Mode == EMode::Still; }
	bool IsDangerousToEnemies() const override { return Mode == EMode::Sliding; }
	void OnStomped( float32 PlayerX ) override;
	void OnKicked( float32 PlayerX ) override;

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

	// 蹴り出した直後の、主人公にダメージを与えない時間（蹴った足が、そのまま当たらないように）
	float32 KickGrace = 0.0f;
};
