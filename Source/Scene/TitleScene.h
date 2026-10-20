#pragma once
#include "Library/Framework/Scene/Scene.h"

//=============================================================================
// CTitleScene
//   Enter でゲームへ
//=============================================================================
class CTitleScene : public CScene
{
protected:
	void OnTick( float32 DeltaTime ) override;
	void OnDraw() override;
};
