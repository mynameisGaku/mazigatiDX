// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

struct FPlatformer2DState;

//=============================================================================
// CPlatformer2DFireball
//   ファイアの主人公が撃つ、火の玉。地面で跳ねながら進み、壁に当たると消える。敵に当たると、その敵を倒して消える。
//=============================================================================
class CPlatformer2DFireball : public CGameObject
{
public:
	// InPosition は玉の中心、InDirection は進む向き（-1 / 1）
	CPlatformer2DFireball( const TSharedPtr<FPlatformer2DState>& InState, const FVector2& InPosition, float32 InDirection );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FPlatformer2DState> State;

	// 箱の足元の中心（玉は小さい箱として、タイルに当てる）
	FVector2 Position;
	FVector2 Velocity;
	float32 Direction;
	float32 Age = 0.0f;
};
