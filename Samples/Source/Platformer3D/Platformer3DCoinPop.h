// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Math/Math.h"
#include "Source/Platformer3D/Platformer3DActor.h"

struct FPlatformer3DState;

//=============================================================================
// CPlatformer3DCoinPop
//   ブロックから飛び出すコインの、見た目だけの動き（回りながら跳ね上がって、消える）。コインそのものは、叩いたときに、すでに取っている。
//=============================================================================
class CPlatformer3DCoinPop : public CPlatformer3DActor
{
public:
	// InStart は、ブロックの上の面の中心
	CPlatformer3DCoinPop( const TSharedPtr<FPlatformer3DState>& InState, const FVector3& InStart );

	void Tick( float32 DeltaTime ) override;
	void Record3D( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FPlatformer3DState> State;
	FVector3 Start;
	float32 Age = 0.0f;
};
