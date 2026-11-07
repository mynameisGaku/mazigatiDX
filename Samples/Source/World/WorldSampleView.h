// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"

struct FWorldSampleState;

//=============================================================================
// CWorldSampleView
//   中庭を描く: 空 → カメラ → ライト → ワールドの置き物のメッシュ（FMeshRenderer）とコードのアクター → 粒子のエフェクト。
//   置き物の絵（ECS が自動で描くもの）は、自動描画を切って（bAutoDraw = false）、ここで、カメラの中で記録する。
//   上の表示（ワールドの名前・置き物の数・コイン・操作）も、ここで描く。いちばん奥に描く。
//=============================================================================
class CWorldSampleView : public CGameObject
{
public:
	explicit CWorldSampleView( const TSharedPtr<FWorldSampleState>& InState );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FWorldSampleState> State;
};
