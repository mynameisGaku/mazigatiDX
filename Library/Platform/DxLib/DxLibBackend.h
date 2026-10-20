#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Result.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"
#include "Library/Platform/WindowSettings.h"

DECLARE_LOG_CATEGORY_EXTERN( LogPlatform, Log )

//=============================================================================
// CDxLibBackend
//   IPlatform の DxLib 実装。DxLib の初期化・フレーム進行・ImGui・ウィンドウモード切り替えを行う。
//   CAppSubsystem として自動生成され、CEngine::GetStartupSettings() の設定で初期化される。
//
//   ゲーム側は実装ではなくインターフェースで取得する:
//   IPlatform* Platform = GetAppSubsystem<IPlatform>();
//   Platform->SetWindowMode( EWindowMode::Fullscreen );   // 次のフレームの頭で反映される
//   Platform->ToggleFullscreen();                        // Windowed <-> 直前のフルスクリーン系モード
//
//   DxLib 固有の機能（ウィンドウ倍率など）が必要な場合だけ GetAppSubsystem<CDxLibBackend>() を使う。
//=============================================================================
class CDxLibBackend : public CAppSubsystem, public IPlatform
{
public:
	//-------------------------------------------------------------------------
	// CAppSubsystem
	//-------------------------------------------------------------------------
	void Initialize( FSubsystemCollection& Collection ) override;
	void Deinitialize() override;

	//-------------------------------------------------------------------------
	// IPlatform
	//-------------------------------------------------------------------------
	bool IsInitialized() const override;

	// フレーム開始（メッセージ処理・時間計測・ImGui 開始）。終了すべきなら false
	bool BeginFrame() override;
	// フレーム終了（ImGui 描画・画面反映・裏画面クリア）
	void EndFrame() override;

	void RequestExit() override;
	bool RequestScreenshot( const FString& Path, int32 DelayFrames = 0 ) override;
	bool IsExitRequested() const override;

	float32 GetDeltaTime() const override;  // 最大 0.25 秒
	float64 GetTime() const override;
	uint64 GetFrameCount() const override;
	float32 GetFPS() const override;  // 直近 0.5 秒の平均

	// ゲーム画面（描画先）の解像度
	int32 GetScreenWidth() const override;
	int32 GetScreenHeight() const override;

	// 初期化後に呼んだ場合は次の BeginFrame で反映される
	EWindowMode GetWindowMode() const override;
	void SetWindowMode( EWindowMode Mode ) override;
	// Windowed <-> 直前に使っていたフルスクリーン系モード（初回は Borderless）
	void ToggleFullscreen() override;

	const FString& GetWindowTitle() const override;
	void SetWindowTitle( const FString& Title ) override;

	bool IsMouseCursorVisible() const override;
	void SetMouseCursorVisible( bool bVisible ) override;

	//-------------------------------------------------------------------------
	// DxLib 固有
	//-------------------------------------------------------------------------
	// Windowed 時のウィンドウ倍率
	void SetWindowExtendRate( float64 Rate );

	// メインモニタのデスクトップ解像度
	void GetDesktopSize( int32& OutWidth, int32& OutHeight ) const;

	FORCEINLINE const FWindowSettings& GetSettings() const { return Settings; }

private:
	TResult<void> Init( const FWindowSettings& InSettings );
	void Shutdown();

	void ApplyPreInitSettings();
	void ApplyRenderState();
	// ImGui の外部ウィンドウを描画・表示する（更新頻度を絞る。理由は FWindowSettings::ImGuiViewportMaxFPS を参照）
	void RenderImGuiViewports();
	// DxLib と ImGui の両方にカーソル表示を反映する（ウィンドウモード変更でリセットされるので都度呼ぶ）
	void ApplyMouseCursor();
	bool ApplyWindowMode( EWindowMode Mode );
	void InitImGui();
	void ShutdownImGui();
	void UpdateTime();
	void WaitForFrameLimit();

	FWindowSettings Settings;
	bool bInitialized   = false;
	bool bImGuiActive   = false;
	bool bInFrame       = false;
	bool bExitRequested = false;

	TMap<uint32, int64> ViewportLastPresentTime;  // key: ImGuiViewport::ID, value: 最後に表示した時刻 (マイクロ秒)

	// 起動引数 --screenshot=パス.png [--screenshot-frame=N（既定 90）] [--screenshot-exit]: N フレーム目の画面（ImGui も含む）を保存する。
	// 画面の見た目の確認（スクリーンショットの撮影）用。--screenshot-exit なら、保存したあと終了する
	void CaptureScreenshotIfRequested();
	FString ScreenshotPath;
	int32 ScreenshotFrame = 90;
	bool bScreenshotExit = false;
	bool bScreenshotDone = false;

	TOptional<EWindowMode> PendingWindowMode;
	EWindowMode LastFullscreenMode = EWindowMode::Borderless;

	int64   LastCounter   = 0;   // マイクロ秒
	int64   FrameStart    = 0;
	float32 DeltaTime     = 0.0f;
	float64 TotalTime     = 0.0;
	uint64  FrameCount    = 0;
	float32 FPS           = 0.0f;
	float64 FPSAccumTime  = 0.0;
	int32   FPSAccumCount = 0;
};
