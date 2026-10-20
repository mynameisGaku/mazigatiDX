#pragma once
#include "Library/Core/Log/Log.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/DebugUI/AssetTools.h"
#include "Library/DebugUI/ConsoleWindow.h"
#include "Library/DebugUI/ProfilerWindow.h"
#include "Library/Framework/Console/ConsoleConfig.h"
#include "Library/Framework/GameInstance/GameInstance.h"

//=============================================================================
// CGameApplication
//   このゲームのゲームインスタンス。CEngine::Run<CGameApplication> から
//   OnInit -> ( OnTick -> シーン -> OnDraw ) * N -> OnTerm の順に呼ばれる。
//   シーンの登録と、全シーン共通のデバッグ UI を担当する。
//=============================================================================
class CGameApplication : public CGameInstance
{
protected:
	void OnInit() override;
	void OnTick( float32 DeltaTime ) override;
	void OnDraw() override;
	void OnTerm() override;

private:
	void DrawDebugWindow();
	// プロファイラーの記録をオン / オフする（F2 とデバッグウィンドウのボタン）。オンにしたときは画面も出す
	void ToggleProfiler();

	TSharedPtr<CImGuiLogSink> LogWindow;
	bool bShowDebugWindow = true;

	bool bShowProfilerWindow = false;
	FProfilerWindowState ProfilerWindowState;

	// アセットの画面・エディタ（Assets / Audio / スプライトシートエディタ / モデルのアニメーションエディタ）
	FAssetToolsState AssetTools;

#if COG_DEBUG_CONSOLE
	// デバッグ限定のコンソール（F3 で開閉。コマンドは FConsoleRegistry::Global() に登録する）
	FConsoleWindowState ConsoleWindow;
	bool bShowConsole = false;
#endif
};
