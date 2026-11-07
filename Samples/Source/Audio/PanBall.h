// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Audio/AudioTypes.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

class CSound;

//=============================================================================
// CPanBall
//   左右に行き来する球。Enter（または、球をクリック）で、ずっと鳴る音（ループ）を始めたり、止めたりする。
//   球の左右の位置を、毎フレーム、パン（左右の振り分け）に渡すので、音も、画面の動きに合わせて左右に動く。
//   FSoundPlayParams::bLoop と、CAudioSubsystem::SetPan / Stop（FadeOut つき）の見本。
//=============================================================================
class CPanBall : public CGameObject
{
public:
	// InCenter は、動きの中心（画面の座標）、InHalfWidth は、中心から左右に動く幅の半分（ピクセル）
	CPanBall( const FVector2& InCenter, float32 InHalfWidth );

	void OnSpawn() override;
	void Tick( float32 DeltaTime ) override;
	void Draw( FDrawContext& CommandList ) override;
	void OnDestroy() override;

private:
	void Toggle();

	FVector2 Center;
	float32 HalfWidth;
	float32 Time = 0.0f;
	float32 CurrentX = 0.0f;
	TSharedPtr<CSound> Tone;
	FSoundHandle Handle;
};
