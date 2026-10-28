// SPDX-License-Identifier: MIT
#pragma once

#include "Library/DebugUI/DebugOverlay.h"
#include "Library/Framework/GameInstance/GameInstance.h"

//=============================================================================
// CSamplesApplication
//   サンプル集のゲームインスタンス。起動したときに、一覧の画面と、登録されたすべてのサンプルのシーンを登録して、一覧から始める。
//   起動引数 --sample=Name を付けると、一覧を飛ばして、そのサンプルから始める。
//   デバッグ表示（F1: 窓、F2: プロファイラー、F3: コンソール、F11: フルスクリーン）は、最初は隠してある（状態行も、サンプルの見出しと重なるので隠す）。
//=============================================================================
class CSamplesApplication : public CGameInstance
{
protected:
	void OnInit() override;
	void OnTick( float32 DeltaTime ) override;
	void OnDraw() override;
	void OnTerm() override;

private:
	CDebugOverlay Overlay;
};
