// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Platformer2D/Platformer2DEnemy.h"

//=============================================================================
// CPlatformer2DShelled
//   甲羅の敵。歩いているとき踏むと、甲羅にこもって止まる。止まった甲羅は、ふれる（または、また踏む）と蹴り出せて、
//   すべる甲羅は、ぶつかった敵を倒す（主人公にも当たる）。
//   ステートマシン: Samples/Assets/Platformer/Shelled2D.gxstate（Walk → Still → Sliding。どこからでも Flying）。
//   クラスに、状態の enum はない。「いま止まった甲羅か」などは、ステートマシンに聞く。
//=============================================================================
class CPlatformer2DShelled : public CPlatformer2DEnemy
{
public:
	CPlatformer2DShelled( const TSharedPtr<FPlatformer2DState>& InState, const FVector2& InFeetPosition );

	void Draw( FDrawContext& CommandList ) override;

	bool IsDeadly() const override;
	bool IsStill() const override;
	bool IsDangerousToEnemies() const override;

	// IPlatformerEnemyBody（ステートの動き）
	void BeginShell() override;
	void BeginSlide() override;
	void StepSlide( float32 DeltaTime, float32 Speed ) override;
};
