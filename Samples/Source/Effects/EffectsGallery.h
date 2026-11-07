// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Object/GameObject.h"

struct FEffectsState;

//=============================================================================
// CEffectsGallery
//   エフェクトを並べて、動かす。エフェクトの再生を作り（FEffectBackendRegistry::Play）、毎フレーム Update し、
//   選んでいるものを、マウスに合わせて動かす（雷の終点・帯の先頭）。カメラは、選んでいる台座へ、なめらかに寄る。
//   操作: ← → = 選ぶ、Space = 最初から、↑ ↓ = 大きさ、マウス = 雷の先と帯の先頭
//   描くのは CEffectsView が受け持つ（エフェクトの 3D は、カメラが決まったあとにしか、正しく記録できない）。
//=============================================================================
class CEffectsGallery : public CGameObject
{
public:
	explicit CEffectsGallery( const TSharedPtr<FEffectsState>& InState );

	void OnSpawn() override;
	void Tick( float32 DeltaTime ) override;

private:
	// 番号のエフェクトを、最初から再生する
	void Restart( int32 Index );

	TSharedPtr<FEffectsState> State;

	// カメラが見ている、横の位置（選んでいる台座へ、なめらかに寄る）
	float32 CameraX = 0.0f;
};
