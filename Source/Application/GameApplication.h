#pragma once
#include "Library/DebugUI/AssetTools.h"
#include "Library/DebugUI/DebugOverlay.h"
#include "Library/Framework/GameInstance/GameInstance.h"

//=============================================================================
// CGameApplication
//   このゲームのゲームインスタンス。CEngine::Run<CGameApplication> から
//   OnInit -> ( OnTick -> シーン -> OnDraw ) * N -> OnTerm の順に呼ばれる。
//   シーンの登録と、全シーン共通のデバッグ表示（CDebugOverlay）にアセットの道具を並べることを担当する。
//=============================================================================
class CGameApplication : public CGameInstance
{
protected:
	void OnInit() override;
	void OnTick( float32 DeltaTime ) override;
	void OnDraw() override;
	void OnTerm() override;

private:
	// 組み込みのデバッグ表示（F1: 窓、F2: プロファイラー、F3: コンソール、F11: フルスクリーン）
	CDebugOverlay Overlay;

	// アセットの画面・エディタ（Assets / Audio / スプライトシートエディタ / モデルのアニメーションエディタ）。デバッグ表示の窓に並べる
	FAssetToolsState AssetTools;
};
