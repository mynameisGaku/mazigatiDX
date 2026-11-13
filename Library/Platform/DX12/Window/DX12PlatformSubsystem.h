// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"
#include "Library/Core/Interfaces/Render/IRenderSurface.h"
#include "Library/Core/Interfaces/Render/IRenderTextureResources.h"
#include "Library/Core/Interfaces/Render/IRenderBufferResources.h"
#include "Library/Core/Interfaces/Render/IComputePipelineResources.h"
#include "Library/Core/Interfaces/Render/IGraphicsPipelineResources.h"
#include "Library/Core/Interfaces/Render/IGpuTimestampResources.h"
#include "Library/Core/Interfaces/Render/IComputeQueue.h"
#include "Library/Core/Interfaces/Render/IRenderCommandQueue.h"
#include "Library/Core/Interfaces/Platform/INativeWindow.h"
#include "Library/Platform/Windows/Text/WindowsTextRasterizer.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Platform/ImGui/Render/ImGuiViewportScheduler.h"

class CRenderCommandThread;
class CThreadedRenderSurface;
class CDX12Device;
class CDX12GameRenderer;
class CDX12GameSurface;
class CDX12ImGuiFrameRenderer;
class CDX12GameCapture;
class CDX12RenderTargetPass;

// Windowsの窓とフレーム進行を所有する。GPUを所有し、ImGuiへは描画に必要な参照だけを貸す。
class CDX12PlatformSubsystem : public CAppSubsystem, public IPlatform, public IRenderSurface, public IRenderTextureResources, public IRenderBufferResources, public IComputePipelineResources, public IGraphicsPipelineResources, public IGpuTimestampResources, public IComputeQueue, public IRenderCommandQueue, public INativeWindow
{
  public:
	// 途中の初期化失敗時も資源を解放する。
	~CDX12PlatformSubsystem() override;
	// --backend=dx12のときだけ作る。
	bool ShouldCreateSubsystem( CEngine &Engine ) const override;
	// 窓・GPU・UIを作り、描画の実行を専用スレッドへ引き渡す。失敗時はIsInitializedがfalse。
	void Initialize( FSubsystemCollection &Collection ) override;
	// GPU完了を待ち、生成と逆順に解放する。
	void Deinitialize() override;
	// 起動成功時だけtrue。
	bool IsInitialized() const override;
	// OSのイベントを取り込み、フレームを開始する。
	bool BeginFrame() override;
	// ゲームと編集画面の描画を表示する。
	void EndFrame() override;
	// 主画面のゲーム描画とUI込みの画素を受け取る借用先を設定する。nullptrで解除する。
	void SetFrameCaptureSink( IFrameCaptureSink *Sink ) override;
	// 通常のフレーム記録を保持して、別の大きさへ描き直す。
	bool BeginOffscreenCapture( const FOffscreenCaptureDesc &Desc ) override;
	// 描き直しの結果を同期で読む。失敗時はOutを保つ。
	bool EndOffscreenCapture( TArray<FColor32> &Out ) override;
	// 通常のフレーム記録中に、所有済みの描画先資源へ描くパスを開始する。
	bool BeginRenderTargetPass( const FRenderTargetPassDesc &Desc ) override;
	// 描画先パスの命令を同じDIRECTキューへ提出し、通常のフレーム記録へ戻す。
	bool EndRenderTargetPass() override;
	// 現在の記録先（主画面または描画先パス）へGPUタイムスタンプの開始点を積む。
	bool BeginGpuTimestamp( FGpuTimestampHandle Query ) override;
	// 開始済みのGPUタイムスタンプを閉じ、解決を記録する。
	bool EndGpuTimestamp( FGpuTimestampHandle Query ) override;
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
	const FString &GetWindowTitle() const override;
	// UTF-8の題をOSへ渡す。
	void SetWindowTitle( const FString &Title ) override;
	// カーソル表示の設定値。
	bool IsMouseCursorVisible() const override;
	// クライアント領域のカーソル表示を切り替える。
	void SetMouseCursorVisible( bool bVisible ) override;
	// DX12内の実装へ窓を貸す。
	void *GetNativeWindow() const override;
	// フレーム中だけゲームの描画先を貸す。
	IRenderCanvas2D *GetCanvas2D() override;
	// フレーム中だけ基本3Dの実行先を貸す。
	IRenderGeometry3D *GetGeometry3D() override;
	// 色を所有コピーして消去命令を積む。
	void RecordClear( FColor32 Color ) override;
	// アセットの終了時にGPUを待つ。
	bool WaitForGPU() override;
	// RGBA8の画像資源を、UIを介さず作る。
	FTextureResourceHandle CreateTextureRGBA8( int32 Width, int32 Height, const FColor32 *Pixels ) override;
	// ゲームスレッドから能力を照会する。未初期化・GPU失敗時はOutを保ってfalse。
	bool GetComputeTextureCapabilities( FComputeTextureCapabilities &Out ) const override;
	// ゲームスレッドから、computeで読み書き可能な画像を描画所有側へ作成する。
	FTextureResourceHandle CreateComputeTextureRGBA8( int32 Width, int32 Height, const FColor32 *Pixels ) override;
	// ゲームスレッドから、computeで読み書き可能なRGBA16F画像を描画所有側へ作成する。
	FTextureResourceHandle CreateComputeTextureRGBA16F( int32 Width, int32 Height, const FColor *Pixels ) override;
	// ゲームスレッドから描画先画像と読み取れる深度の能力を照会する。
	bool GetRenderTargetCapabilities( FRenderTargetCapabilities &Out ) const override;
	// ゲームスレッドからRGBA8の色描画先画像を描画所有側へ作成する。
	FTextureResourceHandle CreateRenderTargetRGBA8( int32 Width, int32 Height, FColor32 ClearColor = FColor32( 0, 0, 0, 255 ) ) override;
	// ゲームスレッドからRGBA16FのHDR色描画先画像を描画所有側へ作成する。
	FTextureResourceHandle CreateRenderTargetRGBA16F( int32 Width, int32 Height, FColor ClearColor = FColor( 0.0f, 0.0f, 0.0f, 1.0f ) ) override;
	// ゲームスレッドからR32Float単一成分カラー描画先画像を描画所有側へ作成する。
	FTextureResourceHandle CreateRenderTargetR32Float( int32 Width, int32 Height, float32 ClearValue = 1.0f ) override;
	// ゲームスレッドから読み取れるD32深度を描画所有側へ作成する。
	FTextureResourceHandle CreateDepthTextureD32( int32 Width, int32 Height, float32 ClearDepth = 1.0f ) override;
	// ゲームスレッドから同一寸法・同色グループの画像をコピーする。
	bool CopyTexture( FTextureResourceHandle Source, FTextureResourceHandle Destination ) override;
	// ゲームスレッドからRGBA8画像をCPUへ読み戻す。
	bool ReadTextureRGBA8( FTextureResourceHandle Texture, TArray<FColor32> &OutPixels, int32 &OutWidth, int32 &OutHeight ) override;
	// ゲームスレッドからRGBA16F画像をCPUへ読み戻す。
	bool ReadTextureRGBA16F( FTextureResourceHandle Texture, TArray<FColor> &OutPixels, int32 &OutWidth, int32 &OutHeight ) override;
	// ゲームスレッドからR32Floatカラー画像をCPUへ読み戻す。
	bool ReadTextureR32Float( FTextureResourceHandle Texture, TArray<float32> &OutValues, int32 &OutWidth, int32 &OutHeight ) override;
	// 記録済みの画像を保ったまま更新する。
	bool UpdateTextureRGBA8( FTextureResourceHandle Texture, int32 Width, int32 Height, const FColor32 *Pixels ) override;
	// GPU完了後に画像資源を解放する。
	void ReleaseTexture( FTextureResourceHandle Texture ) override;
	// 所有先から画像を借りる。失敗時はOutを保つ。
	bool GetTextureResourceView( FTextureResourceHandle Texture, FTextureDrawView &Out ) const override;
	// 初期バイト列のGPU転送を待ち、完成した資源だけを登録する。
	FBufferResourceHandle CreateBuffer( TArrayView<const uint8> Bytes, ERenderBufferShaderAccess Access = ERenderBufferShaderAccess::None ) override;
	// 番号を無効にし、同じキューの完了後に実体を解放する。
	void ReleaseBuffer( FBufferResourceHandle Buffer ) override;
	// 有効なバッファのバイト数を返す。失敗時はOutSizeを保つ。
	bool GetBufferSize( FBufferResourceHandle Buffer, uint64 &OutSize ) const override;
	// バッファの範囲を同期で読み戻す。初期化前・不正な範囲・GPU失敗時はOutを保つ。
	bool ReadBuffer( FBufferResourceHandle Buffer, uint64 Offset, int32 NumBytes, TArray<uint8> &Out ) override;
	// バイナリとslot定義を検査してからプログラムを登録する。失敗時は無効値。
	FComputePipelineHandle CreateComputePipeline( TArrayView<const uint8> Bytecode, TArrayView<const FComputeBufferSlot> Slots ) override;
	// ゲームスレッドから画像を含む配置を渡す。作成・検査は描画所有側で完了させる。
	FComputePipelineHandle CreateComputePipelineWithTextures( TArrayView<const uint8> Bytecode, TArrayView<const FComputeBufferSlot> Slots, TArrayView<const FComputeTextureSlot> TextureSlots ) override;
	// 番号を直ちに無効にし、提出済み処理が終わってから実体を解放する。
	void ReleaseComputePipeline( FComputePipelineHandle Pipeline ) override;
	// VS／PS DXBCと入力・スロット・描画先数・状態を照合してカスタムパイプラインを登録する。
	FGraphicsPipelineHandle CreateGraphicsPipeline( const FGraphicsPipelineDesc &Desc ) override;
	// 番号を直ちに無効にし、記録・提出済み処理が終わってから実体を解放する。
	void ReleaseGraphicsPipeline( FGraphicsPipelineHandle Pipeline ) override;
	// ゲームスレッドからGPUタイムスタンプの対応可否・周波数・上限を照会する。
	bool GetGpuTimestampCapabilities( FGpuTimestampCapabilities &Out ) const override;
	// ゲームスレッドからGPUタイムスタンプ計測ペアを作成する。
	FGpuTimestampHandle CreateGpuTimestampQuery() override;
	// 番号を直ちに無効化し、GPU完了後にスロットを回収する。
	void ReleaseGpuTimestampQuery( FGpuTimestampHandle Query ) override;
	// 提出済みタイムスタンプの完了状態を待たずに照会する。
	bool IsGpuTimestampReady( FGpuTimestampHandle Query, bool &OutReady ) override;
	// 提出済みGPUタイムスタンプの完了を最大TimeoutMs待つ。
	bool WaitGpuTimestamp( FGpuTimestampHandle Query, uint32 TimeoutMs = 10000 ) override;
	// GPU完了済みのタイムスタンプ計測結果を返す。
	bool GetGpuTimestampResult( FGpuTimestampHandle Query, FGpuTimestampResult &Out ) override;
	// 全compute命令の検査後に、完了を待たず同じキューへ提出する。
	FComputeSubmission SubmitCompute( TArrayView<const FComputeDispatch> Dispatches ) override;
	// 全compute命令の前後にGPUタイムスタンプを同時に記録して同じキューへ提出する。
	FComputeSubmission SubmitComputeWithTimestamp( TArrayView<const FComputeDispatch> Dispatches, FGpuTimestampHandle Timestamp ) override;
	// 有効な提出の完了状態を照会する。失敗時はOutCompletedを保つ。
	bool GetComputeCompletion( FComputeSubmission Submission, bool &OutCompleted ) override;
	// 有効な提出を指定した時間だけ待つ。タイムアウトしても参照を保持する。
	bool WaitCompute( FComputeSubmission Submission, uint32 TimeoutMs ) override;
	// 先行する2D記録を渡してから、ゲーム側が所有する値を専用描画スレッドへ積む。
	FTaskFence EnqueueRenderCommand( TCallable<void( const FRenderCommandContext & )> Task, const char *Name = nullptr ) override;
	// 今フレームのホイールを返す。
	void GetWheel( float32 &X, float32 &Y ) const override;
	// OSのホイール通知を貯める。ウィンドウ手続きからのみ呼ぶ。
	void AddWheel( float32 X, float32 Y );
	// OSのWM_SIZE通知を受けて最小化状態と次フレームのサイズ追従を記録する。ウィンドウ手続きからのみ呼ぶ。
	void HandleWindowResize( uint32 SizeType, int32 ClientWidth, int32 ClientHeight );

  private:
	// 記録済みの2Dを同じ列へ渡し、借用値を使う処理が終わるまで待つ。初期化・解放時は所有スレッドで直接実行する。
	bool ExecuteRender( TCallable<void()> Task ) const;
	// 通常のcompute提出とタイムスタンプ付き提出を共有経路で実行する。
	FComputeSubmission SubmitComputeInternal( TArrayView<const FComputeDispatch> Dispatches, const FGpuTimestampHandle *Timestamp );
	CRenderCommandThread *RenderCommands = nullptr; // 描画の専用実行列。GPU利用者より先に終了する。
	CThreadedRenderSurface *ThreadedSurface = nullptr; // ゲーム側の値を所有コピーする記録窓口。
	CWindowsTextRasterizer GameText; // UI設定とGPUから独立したゲーム文字の画像化。
	CImGuiViewportScheduler Viewports; // 外部窓の更新頻度と表示順。
	FWindowSettings Settings; // 起動設定と現在の表示設定。
	CDX12Device *Device = nullptr; // ゲームとUIが共有するGPU所有者。利用者より後に破棄する。
	CDX12GameSurface *NativeSurface = nullptr; // 描画側だけが借りる実行先。窓側の公開窓口とは分離する。
	CDX12GameRenderer *GameRenderer = nullptr; // ゲームの2D・3DをUIと別の順序へ記録する。
	CDX12ImGuiFrameRenderer *ImGuiFrameRenderer = nullptr; // GPUを借りるUIの記録先。GPUは所有しない。
	CDX12GameCapture *CaptureRenderer = nullptr; // UIとは独立したゲーム描画の撮影先。
	CDX12RenderTargetPass *TargetPassRenderer = nullptr; // UIとは独立したオフスクリーン描画先パスの記録・提出先。
	IFrameCaptureSink *FrameCaptureSink = nullptr; // 撮影サービスの借用先。終了前に解除する。
	TArray<FColor32> CapturePixels; // 通知中だけ受け取り手へ借用する画素。
	void *Window = nullptr; // 所有するHWND。公開ヘッダーにWindowsを出さない。
	bool bContext = false; // ImGui文脈の所有印。
	bool bWin32 = false; // ImGuiのOS接続済み印。
	bool bInitialized = false; // 全初期化の成功印。
	bool bWindowRenderPhase = false; // Presentと外部窓の間だけ、窓側がGPU接続を排他的に借りる。
	bool bInFrame = false; // 描画記録中の印。
	bool bExit = false; // 終了要求。
	bool bMinimized = false; // 最小化中の0x0リサイズを避けて既存バックバッファを維持する印。
	bool bPendingResize = false; // Resizable有効時に次フレーム冒頭でSwapChainと深度を再構築する印。
	int32 PendingResizeWidth = 0; // WM_SIZEで受け取った保留幅。
	int32 PendingResizeHeight = 0; // WM_SIZEで受け取った保留高さ。
	int32 MaxFrames = 0; // 検査用のフレーム上限。0なら制限なし。
	int64 LastTick = 0; // 前フレームの時刻。
	float32 DeltaTime = 0; // 今フレームの秒数。
	float64 TotalTime = 0; // 累積秒数。
	uint64 FrameCount = 0; // 表示した回数。
	float32 PendingWheelX = 0, PendingWheelY = 0; // OSから貯めた回転量。
	float32 WheelX = 0, WheelY = 0; // フレームに固定した回転量。
	int32 SavedRect[4] = {}; // 通常ウィンドウへ戻す位置と大きさ。
};
