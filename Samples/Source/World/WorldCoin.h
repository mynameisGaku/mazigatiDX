// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Math/Math.h"
#include "Source/World/WorldSampleActor.h"

struct FWorldSampleState;

//=============================================================================
// CWorldCoin
//   タグ "Coin" の置き物に結び付けるコード。ワールドのファイルには、位置と、点数のプロパティ "Value" だけがある。
//   回りながら浮かぶコインを描き、主人公が近づいたら、集める（点数を足して、消える）。
//   置き物の子として作られるので、ワールドを Despawn すると、置き物と一緒に消える。
//=============================================================================
class CWorldCoin : public CWorldSampleActor
{
public:
	CWorldCoin( const TSharedPtr<FWorldSampleState>& InState, int32 InValue );

	void Tick( float32 DeltaTime ) override;
	void Record3D( FDrawContext& CommandList ) override;

private:
	// 置き物（親）の、世界での位置
	FVector3 GetPlaceholderPosition() const;

	TSharedPtr<FWorldSampleState> State;
	int32 Value;
	float32 Age = 0.0f;
};
