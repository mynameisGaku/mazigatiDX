// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Math/Math.h"
#include "Source/Platformer3D/Platformer3DActor.h"

struct FPlatformer3DState;

//=============================================================================
// CPlatformer3DFireball
//   ファイアの主人公が撃つ、火の玉。向いている方へ進み、地面で跳ねる。壁に当たると消える。敵に当たると、その敵を倒して消える。
//=============================================================================
class CPlatformer3DFireball : public CPlatformer3DActor
{
public:
	// InCenter は玉の中心、InDirection は進む水平の向き（長さ 1）
	CPlatformer3DFireball( const TSharedPtr<FPlatformer3DState>& InState, const FVector3& InCenter, const FVector3& InDirection );

	void Tick( float32 DeltaTime ) override;
	void Record3D( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FPlatformer3DState> State;

	// 箱の足元の中心（玉は小さい箱として、足場に当てる）
	FVector3 Position;
	FVector3 Velocity;
	FVector3 Direction;
	float32 Age = 0.0f;
};
