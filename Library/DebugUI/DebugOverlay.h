// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Input/IKeyboardInput.h"
#include "Library/Core/Log/LogSink.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Types/Callable.h"
#include "Library/DebugUI/ConsoleWindow.h"
#include "Library/DebugUI/ProfilerWindow.h"
#include "Library/Framework/Console/ConsoleConfig.h"

class CGameInstance;
class IPlatform;
class IProfiler;

//=============================================================================
// CDebugOverlay
//   ゲームの上に重ねる、組み込みのデバッグ表示（ImGui）。ゲームインスタンスが持って、4 か所から呼ぶだけで使える。
//
//     F1   デバッグ窓（Window / Input / Scene / Output Log）の表示切り替え
//     F2   プロファイラーの記録のオン / オフ（オンにしたとき、プロファイラーの窓も出る）
//     F3   コンソールの開閉（Debug ビルドだけ。COG_DEBUG_CONSOLE が 1 のとき）
//     F11  フルスクリーンの切り替え
//
//   class CMyGame : public CGameInstance
//   {
//   protected:
//       void OnInit() override { Overlay.Init(); }
//       void OnTick( float32 DeltaTime ) override { Overlay.Tick(); }
//       void OnDraw() override { Overlay.Draw( *this ); }
//       void OnTerm() override { Overlay.Term(); }
//   private:
//       CDebugOverlay Overlay;
//   };
//
//   最初からデバッグ窓を隠したいときは、Init のあとに SetDebugWindowVisible( false ) を呼ぶ。
//   メインスレッドから使う（ImGui の NewFrame のあと、つまり OnDraw の中で Draw を呼ぶ）。
//=============================================================================
class CDebugOverlay
{
public:
	// ログの窓（Output Log）を使えるようにし、コンソールのコマンド・autoexec.cfg・起動引数 --console を準備する。
	void Init();

	// 毎フレーム 1 回、キー（F1 / F2 / F3 / F11）を処理する。
	void Tick();

	// 毎フレーム 1 回、画面の左上の文字と、ImGui の窓（デバッグ窓・プロファイラー・コンソール）を出す。Game は、シーンとサブシステムの表示に使う。
	void Draw( CGameInstance& Game );

	// ログの窓を外す。
	void Term();

	// キーを処理する本体（Tick が呼ぶ。確認用に、入力・プロファイラー・プラットフォームを差し込める）。
	// Keys は押されたキーの問い合わせ、Profiler は F2 で切り替える相手（nullptr なら何もしない）、Platform は F11 で切り替える相手（nullptr なら何もしない）。
	void HandleKeys( const IKeyboardInput& Keys, IProfiler* Profiler, IPlatform* Platform );

	//-------------------------------------------------------------------------
	// 状態
	//-------------------------------------------------------------------------
	// デバッグ窓（Window / Input / Scene / Output Log）が出ているか（既定は出す）
	FORCEINLINE bool IsDebugWindowVisible() const { return bShowDebugWindow; }
	FORCEINLINE void SetDebugWindowVisible( bool bVisible ) { bShowDebugWindow = bVisible; }

	// 画面の左上の状態行（FPS・窓の種類・シーン・主なキー）が出ているか（既定は出す。自分の文字を左上に出すゲームは、隠す）
	FORCEINLINE bool IsStatusLineVisible() const { return bShowStatusLine; }
	FORCEINLINE void SetStatusLineVisible( bool bVisible ) { bShowStatusLine = bVisible; }

	// プロファイラーの窓が出ているか
	FORCEINLINE bool IsProfilerWindowVisible() const { return bShowProfilerWindow; }

	// コンソールの窓が出ているか（コンソールを使えない構成では、いつも false）
	bool IsConsoleVisible() const;

	//-------------------------------------------------------------------------
	// 項目・窓を足す（ゲームの側で、自分の道具を同じ画面に並べるため）
	//-------------------------------------------------------------------------
	// 「Window」の窓の、プロファイラーの操作の下に、項目を足す（ImGui の呼び出しを書く）
	TCallable<void()> OnDrawPanelItems;

	// デバッグ窓・プロファイラーの窓のあと、コンソールの前に、窓を足す（ImGui::Begin / End を書く）
	TCallable<void()> OnDrawWindows;

private:
	// プロファイラーの記録のオン / オフ（オンにしたときは、窓も出す）。Profiler が nullptr なら何もしない
	void ToggleProfiler( IProfiler* Profiler );
	// アプリのプロファイラーを切り替える
	void ToggleProfiler();
	void DrawDebugWindow( CGameInstance& Game );

	TSharedPtr<CImGuiLogSink> LogWindow;
	bool bShowDebugWindow = true;
	bool bShowStatusLine = true;

	bool bShowProfilerWindow = false;
	FProfilerWindowState ProfilerWindowState;

#if COG_DEBUG_CONSOLE
	// デバッグ限定のコンソール（F3 で開閉。コマンドは FConsoleRegistry::Global() に登録する）
	FConsoleWindowState ConsoleWindow;
	bool bShowConsole = false;
#endif
};
