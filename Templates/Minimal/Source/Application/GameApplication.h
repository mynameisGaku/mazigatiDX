#pragma once
#include "Library/Framework/GameInstance/GameInstance.h"

//=============================================================================
// CGameApplication
//   このゲームのゲームインスタンス。起動したときに、シーンを登録して、最初のシーンへ入る。
//   OnInit -> ( OnTick -> シーン -> OnDraw ) * N -> OnTerm の順に呼ばれる。
//   ゲーム全体で 1 つだけ持つ処理（設定・共通のデバッグ表示など）は、ここへ足す。
//=============================================================================
class CGameApplication : public CGameInstance
{
protected:
	void OnInit() override;
};
