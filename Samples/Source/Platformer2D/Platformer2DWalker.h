// SPDX-License-Identifier: MIT
#pragma once

#include "Source/Platformer2D/Platformer2DEnemy.h"

//=============================================================================
// CPlatformer2DWalker
//   いちばん基本の敵。まっすぐ歩いて、壁で向きを変える。上から踏まれると、ぺしゃんこになって消える。横からふれると、ダメージ。
//=============================================================================
class CPlatformer2DWalker : public CPlatformer2DEnemy
{
public:
	CPlatformer2DWalker( const TSharedPtr<FPlatformer2DState>& InState, const FVector2& InFeetPosition );

	void Draw( FDrawContext& CommandList ) override;
	void OnStomped( float32 PlayerX ) override;
};
