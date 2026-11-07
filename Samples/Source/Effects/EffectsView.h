// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"

struct FEffectsState;

//=============================================================================
// CEffectsView
//   夜の舞台を描く: 暗い空 → カメラ → ライト → 床と台座 → エフェクト（IEffectPlayback::Record）。
//   エフェクトの 3D は、カメラが決まったあとでないと正しく記録できないので、この 1 つのオブジェクトが、順番を受け持つ。
//   台座の名前と、選んでいる台座の説明は、画面の上に（2D で）描く。いちばん奥に描く。
//=============================================================================
class CEffectsView : public CGameObject
{
public:
	explicit CEffectsView( const TSharedPtr<FEffectsState>& InState );

	void Draw( FDrawContext& CommandList ) override;

private:
	void DrawBackground( FDrawContext& CommandList ) const;
	void RecordStage( FDrawContext& CommandList ) const;
	void DrawLabels( FDrawContext& CommandList ) const;

	TSharedPtr<FEffectsState> State;
};
