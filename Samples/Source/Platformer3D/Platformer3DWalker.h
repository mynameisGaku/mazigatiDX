// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Platformer3D/Platformer3DEnemy.h"

//=============================================================================
// CPlatformer3DWalker
//   いちばん基本の敵。ふだんは、うろうろ歩くが、主人公が近づく（10 メートル以内で、ほぼ同じ高さ）と、追いかけてくる。
//   上から踏まれると、ぺしゃんこになって消える。横からふれると、ダメージ。崖では止まる（落ちない）。
//=============================================================================
class CPlatformer3DWalker : public CPlatformer3DEnemy
{
public:
	CPlatformer3DWalker( const TSharedPtr<FPlatformer3DState>& InState, const FVector3& InFeetPosition );

	void Record3D( FDrawContext& CommandList ) override;
	void OnStomped( const FVector3& PlayerPosition ) override;

protected:
	void OnTickActive( float32 DeltaTime ) override;
};
