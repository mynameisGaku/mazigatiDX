#pragma once
#include "Library/Framework/Scene/Scene.h"

//=============================================================================
// CTitleScene
//   タイトル画面。Enter でゲーム（"Play" シーン）へ進む。
//=============================================================================
class CTitleScene : public CScene
{
protected:
	void OnTick( float32 DeltaTime ) override;
	void OnDraw() override;
};
