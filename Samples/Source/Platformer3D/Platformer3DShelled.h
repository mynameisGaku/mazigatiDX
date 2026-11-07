// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Platformer3D/Platformer3DEnemy.h"

//=============================================================================
// CPlatformer3DShelled
//   甲羅の敵。歩いているとき踏むと、甲羅にこもって止まる。止まった甲羅は、ふれる（または、また踏む）と蹴り出せて、
//   すべる甲羅は、ぶつかった敵を倒す（主人公にも当たる）。2D 版（CPlatformer2DShelled）と同じ仕組み。
//   ステートマシン: Samples/Assets/Platformer/Shelled3D.gxstate（Walk → Still → Sliding。どこからでも Flying）。
//   クラスに、状態の enum はない。「いま止まった甲羅か」などは、ステートマシンに聞く。
//=============================================================================
class CPlatformer3DShelled : public CPlatformer3DEnemy
{
public:
	CPlatformer3DShelled( const TSharedPtr<FPlatformer3DState>& InState, const FVector3& InFeetPosition );

	void Record3D( FDrawContext& CommandList ) override;

	bool IsDeadly() const override;
	bool IsStill() const override;
	bool IsDangerousToEnemies() const override;

	// IPlatformerEnemyBody（ステートの動き）
	void BeginShell() override;
	void BeginSlide() override;
	void StepSlide( float32 DeltaTime, float32 Speed ) override;
};
