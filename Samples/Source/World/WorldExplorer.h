// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Math/Math.h"
#include "Source/World/WorldSampleActor.h"

struct FWorldSampleState;

//=============================================================================
// CWorldExplorer
//   中庭を歩き回る主人公。W A S D（カメラから見た向き）で動き、← → または Q / E でカメラを回す。
//   ワールドのタグ "PlayerStart" の置き物の位置から始まる（コードから置き物を探す FWorldInstance::FindFirst の見本）。
//   柱（タグ "Pillar"）と、庭の端は、通れない（簡単な円の当たり）。カメラは、後ろから追う。
//=============================================================================
class CWorldExplorer : public CWorldSampleActor
{
public:
	explicit CWorldExplorer( const TSharedPtr<FWorldSampleState>& InState );

	void OnSpawn() override;
	void Tick( float32 DeltaTime ) override;
	void Record3D( FDrawContext& CommandList ) override;

	// ワールドを置き直したあと、開始の位置へ戻す
	void ResetToStart();

private:
	void UpdateCamera();

	TSharedPtr<FWorldSampleState> State;
	FVector3 Position;
	float32 FacingYaw = 0.0f;
	bool bMoving = false;
};
