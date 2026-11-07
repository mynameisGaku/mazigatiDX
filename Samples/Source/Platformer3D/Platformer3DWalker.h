// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Platformer3D/Platformer3DEnemy.h"

//=============================================================================
// CPlatformer3DWalker
//   いちばん基本の敵。ふだんは、うろうろ歩くが、主人公が近づく（10 メートル以内で、ほぼ同じ高さ）と、追いかけてくる。
//   上から踏まれると、ぺしゃんこになって消える。横からふれると、ダメージ。崖では止まる（落ちない）。
//   頭脳（ビヘイビアツリー）: Samples/Assets/Platformer/Chaser3D.gxbt（死んでいたら何もしない / PlayerNear なら追いかける /
//   そうでなければ うろうろして休む）。体の状態（ステートマシン）: Chaser3D.gxstate（Alive → Squashed → Gone、どこからでも Flying）。
//   PlayerNear と Alive を書くのは、この敵のセンサー（Sense）。
//=============================================================================
class CPlatformer3DWalker : public CPlatformer3DEnemy
{
public:
	CPlatformer3DWalker( const TSharedPtr<FPlatformer3DState>& InState, const FVector3& InFeetPosition );

	void Record3D( FDrawContext& CommandList ) override;
	void OnStomped( const FVector3& PlayerPosition ) override;

	// IPlatformerEnemyBody（追いかける動き）
	void StepChase( float32 DeltaTime, float32 Speed ) override;

protected:
	const char* GetBrainPath() const override { return "Platformer/Chaser3D.gxbt"; }
	void Sense( CBlackboard& Blackboard ) override;
};
