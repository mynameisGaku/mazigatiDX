// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"

struct FAdventureState;

//=============================================================================
// CAdventureView
//   遺跡を描く: 空 → カメラ → ライト → ワールドの置き物のメッシュ（地面・壁・柱・祭壇）とコードのアクター →
//   粒子のエフェクト（ワールドの炎・たいまつ・出口と、その場に出した火花）。
//   置き物の絵（ECS が自動で描くもの）は、自動描画を切って（シーンが切る）、ここで、カメラの中で記録する。
//   その場に出したエフェクトの時間を進め、終わったものを捨てるのも、ここ。いちばん奥に描く。
//=============================================================================
class CAdventureView : public CGameObject
{
public:
	explicit CAdventureView( const TSharedPtr<FAdventureState>& InState );

	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;

private:
	TSharedPtr<FAdventureState> State;
};
