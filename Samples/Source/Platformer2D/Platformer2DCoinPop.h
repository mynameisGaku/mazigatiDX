// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

struct FPlatformer2DState;

//=============================================================================
// CPlatformer2DCoinPop
//   ブロックから飛び出すコインの、見た目だけの動き（回りながら跳ね上がって、消える）。コインそのものは、叩いたときに、すでに取っている。
//=============================================================================
class CPlatformer2DCoinPop : public CGameObject
{
public:
	// InStart は、ブロックの上の面の中心（画面の座標ではなく、面の上の位置）
	CPlatformer2DCoinPop( const TSharedPtr<FPlatformer2DState>& InState, const FVector2& InStart );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FPlatformer2DState> State;
	FVector2 Start;
	float32 Age = 0.0f;
};
