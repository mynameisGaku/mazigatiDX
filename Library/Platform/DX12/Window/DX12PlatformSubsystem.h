// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"
#include "Library/Core/Interfaces/Render/IRenderSurface.h"
#include "Library/Core/Interfaces/Render/IRenderTextureResources.h"
#include "Library/Core/Interfaces/Render/IRenderBufferResources.h"
#include "Library/Core/Interfaces/Render/IComputePipelineResources.h"
#include "Library/Core/Interfaces/Render/IComputeQueue.h"
#include "Library/Core/Interfaces/Platform/INativeWindow.h"
#include "Library/Platform/ImGui/Render/ImGuiCanvas2D.h"
#include "Library/Platform/ImGui/Render/ImGuiViewportScheduler.h"

class FDX12Device;

// Windowsの窓とフレーム進行を所有する。GPUの資源管理はFDX12Deviceへ委譲する。
class CDX12PlatformSubsystem : public CAppSubsystem, public IPlatform, public IRenderSurface, public IRenderTextureResources, public IRenderBufferResources, public IComputePipelineResources, public IComputeQueue, public INativeWindow
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
	// フレーム中だけ基本3Dの実行先を貸す。
	IRenderGeometry3D* GetGeometry3D() override;
	// 色を所有コピーして消去命令を積む。
	void RecordClear( FColor32 Color ) override;
	// アセットの終了時にGPUを待つ。
	bool WaitForGPU() override;
	// RGBA8の画像資源を、UIを介さず作る。
	FTextureResourceHandle CreateTextureRGBA8( int32 Width, int32 Height, const FColor32* Pixels ) override;
	// 記録済みの画像を保ったまま更新する。
	bool UpdateTextureRGBA8( FTextureResourceHandle Texture, int32 Width, int32 Height, const FColor32* Pixels ) override;
	// GPU完了後に画像資源を解放する。
	void ReleaseTexture( FTextureResourceHandle Texture ) override;
	// 所有先から画像を借りる。失敗時はOutを保つ。
	bool GetTextureResourceView( FTextureResourceHandle Texture, FTextureDrawView& Out ) const override;
	// 初期バイト列のGPU転送を待ち、完成した資源だけを登録する。
	FBufferResourceHandle CreateBuffer( TArrayView<const uint8> Bytes, ERenderBufferShaderAccess Access = ERenderBufferShaderAccess::None ) override;
	// 番号を無効にし、同じキューの完了後に実体を解放する。
	void ReleaseBuffer( FBufferResourceHandle Buffer ) override;
	// 有効なバッファのバイト数を返す。失敗時はOutSizeを保つ。
	bool GetBufferSize( FBufferResourceHandle Buffer, uint64& OutSize ) const override;
	// バッファの範囲を同期で読み戻す。初期化前・不正な範囲・GPU失敗時はOutを保つ。
	bool ReadBuffer( FBufferResourceHandle Buffer, uint64 Offset, int32 NumBytes, TArray<uint8>& Out ) override;
	// バイナリとslot定義を検査してからプログラムを登録する。失敗時は無効値。
	FComputePipelineHandle CreateComputePipeline( TArrayView<const uint8> Bytecode, TArrayView<const FComputeBufferSlot> Slots ) override;
	// 番号を直ちに無効にし、提出済み処理が終わってから実体を解放する。
	void ReleaseComputePipeline( FComputePipelineHandle Pipeline ) override;
	// 全compute命令の検査後に、完了を待たず同じキューへ提出する。
	FComputeSubmission SubmitCompute( TArrayView<const FComputeDispatch> Dispatches ) override;
	// 有効な提出の完了状態を照会する。失敗時はOutCompletedを保つ。
	bool GetComputeCompletion( FComputeSubmission Submission, bool& OutCompleted ) override;
	// 有効な提出を指定した時間だけ待つ。タイムアウトしても参照を保持する。
	bool WaitCompute( FComputeSubmission Submission, uint32 TimeoutMs ) override;
	// 今フレームのホイールを返す。
	void GetWheel( float32& X, float32& Y ) const override;
	// OSのホイール通知を貯める。ウィンドウ手続きからのみ呼ぶ。
	void AddWheel( float32 X, float32 Y );
private:
	FImGuiCanvas2D Canvas; // API共通の2D図形生成。
	FImGuiViewportScheduler Viewports; // 外部窓の更新頻度と表示順。
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
