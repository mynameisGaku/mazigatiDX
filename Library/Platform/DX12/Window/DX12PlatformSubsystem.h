// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"
#include "Library/Core/Interfaces/Render/IRenderSurface.h"
#include "Library/Core/Interfaces/Platform/INativeWindow.h"
#include "Library/Platform/ImGui/Render/ImGuiCanvas2D.h"

class FDX12Device;

// Windowsの窓とフレーム進行を所有する。GPUの資源管理はFDX12Deviceへ委譲する。
class CDX12PlatformSubsystem : public CAppSubsystem, public IPlatform, public IRenderSurface, public INativeWindow
{
public:
	// 途中の初期化失敗時も資源を解放する。
	~CDX12PlatformSubsystem() override;
	// --backend=dx12のときだけ作る。
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	// 窓・ImGui・GPUをこの順に作る。失敗時はIsInitializedがfalse。
	void Initialize( FSubsystemCollection& Collection ) override;
	// GPU完了を待ち、生成と逆順に解放する。
	void Deinitialize() override;
	// 起動成功時だけtrue。
	bool IsInitialized() const override;
	// OSのイベントを取り込み、フレームを開始する。
	bool BeginFrame() override;
	// ゲームと編集画面の描画を表示する。
	void EndFrame() override;
	// 次のフレームで終了する。
	void RequestExit() override;
	// 終了要求を返す。
	bool IsExitRequested() const override;
	// 実時間での前フレームからの秒数。
	float32 GetDeltaTime() const override;
	// 起動からの秒数。
	float64 GetTime() const override;
	// 表示したフレーム数。
	uint64 GetFrameCount() const override;
	// 前フレームの秒数から求めたFPS。
	float32 GetFPS() const override;
	// ゲームの論理幅。
	int32 GetScreenWidth() const override;
	// ゲームの論理高さ。
	int32 GetScreenHeight() const override;
	// 現在の窓モード。
	EWindowMode GetWindowMode() const override;
	// WindowedとBorderlessを切り替える。排他Fullscreenは未対応。
	void SetWindowMode( EWindowMode Mode ) override;
	// 通常の窓と枠なしの画面全体を切り替える。
	void ToggleFullscreen() override;
	// 現在の題。
	const FString& GetWindowTitle() const override;
	// UTF-8の題をOSへ渡す。
	void SetWindowTitle( const FString& Title ) override;
	// カーソル表示の設定値。
	bool IsMouseCursorVisible() const override;
	// クライアント領域のカーソル表示を切り替える。
	void SetMouseCursorVisible( bool bVisible ) override;
	// DX12内の実装へ窓を貸す。
	void* GetNativeWindow() const override;
	// フレーム中だけゲームの描画先を貸す。
	IRenderCanvas2D* GetCanvas2D() override;
	// 色を所有コピーして消去命令を積む。
	void RecordClear( FColor32 Color ) override;
	// アセットの終了時にGPUを待つ。
	bool WaitForGPU() override;
	// 今フレームのホイールを返す。
	void GetWheel( float32& X, float32& Y ) const override;
	// OSのホイール通知を貯める。ウィンドウ手続きからのみ呼ぶ。
	void AddWheel( float32 X, float32 Y );
private:
	FImGuiCanvas2D Canvas; // API共通の2D図形生成。
	FWindowSettings Settings; // 起動設定と現在の表示設定。
	FDX12Device* Device = nullptr; // 所有するGPU。Deinitializeで破棄する。
	void* Window = nullptr; // 所有するHWND。公開ヘッダーにWindowsを出さない。
	bool bContext = false; // ImGui文脈の所有印。
	bool bWin32 = false; // ImGuiのOS接続済み印。
	bool bInitialized = false; // 全初期化の成功印。
	bool bInFrame = false; // 描画記録中の印。
	bool bExit = false; // 終了要求。
	int32 MaxFrames = 0; // 検査用のフレーム上限。0なら制限なし。
	int64 LastTick = 0; // 前フレームの時刻。
	float32 DeltaTime = 0; // 今フレームの秒数。
	float64 TotalTime = 0; // 累積秒数。
	uint64 FrameCount = 0; // 表示した回数。
	float32 PendingWheelX = 0, PendingWheelY = 0; // OSから貯めた回転量。
	float32 WheelX = 0, WheelY = 0; // フレームに固定した回転量。
	int32 SavedRect[4] = {}; // 通常ウィンドウへ戻す位置と大きさ。
};
