#pragma once
#include "Library/Framework/Scene/Scene.h"

//=============================================================================
// CPlayScene
//   遊ぶ画面。プレイヤーを 1 つ置いて、Esc でタイトルへ戻る。
//   ゲームの中身（敵・ステージ・UI など）は、ここにオブジェクトを足して作る。
//=============================================================================
class CPlayScene : public CScene
{
protected:
	void OnEnter() override;
	void OnTick( float32 DeltaTime ) override;
	void OnDraw() override;
};
