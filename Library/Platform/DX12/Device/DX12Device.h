// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "Library/Platform/DX12/Resource/Descriptor/DX12DescriptorPool.h"
#include "Library/Platform/DX12/Resource/Texture/DX12TextureStore.h"
#include "Library/Platform/DX12/Resource/Buffer/DX12BufferStore.h"
#include "Library/Platform/DX12/Command/Compute/DX12ComputeQueue.h"
#include "Library/Platform/DX12/Resource/Readback/DX12TextureReadback.h"
#include "DX12QueueCompletion.h"
#include "Library/Core/Interfaces/Platform/FrameCapture.h"

class CDX12CaptureTarget;

// GPU・交換用画面・同期を所有する。UIの文脈や描画実装を必要としない。
class CDX12Device
{
public:
	// 所有するGPU資源とフレーム状態は複写しない。
	CDX12Device() = default;
	CDX12Device( const CDX12Device& ) = delete;
	CDX12Device& operator=( const CDX12Device& ) = delete;
	// GPU完了またはデバイス喪失まで保持して破棄する。停止しないGPUでは待機を続ける。
	~CDX12Device();
	// Windowへ表示するデバイスを作る。WARP指定はGPUなしの確認用。
	bool Initialize( HWND Window, int32 Width, int32 Height, bool bWarp );
	// 既存のDIRECTキューとそのデバイスを共有してWindowへ表示する。参照は完了まで保持する。
	// 終了中は呼出側も新しい命令を提出しない。無効な窓・寸法・種類・二重初期化ではfalse。
	bool InitializeOnQueue( HWND Window, int32 Width, int32 Height, ID3D12CommandQueue* InQueue );
	// 新しい利用を止め、最大60000msの範囲で終了を試す。タイムアウト・待機失敗では全資源を保持してfalse。
	// falseの後もWaitと終了の再試行は可能。デバイス喪失またはGPU完了後だけ解放してtrue。
	bool TryShutdown( uint32 TimeoutMs = 10000 );
	// 使用中のGPU処理が終わるか、デバイスが失われるまで待って解放する。タイムアウトで早期解放しない。
	void Shutdown();
	// 前フレームを待ち、記録を開始する。失敗時はfalse。
	bool BeginFrame();
	// 呼出側がGPUへ記録した画面を提出して表示する。未開始・終了中・GPU失敗はfalse。
	bool EndFrame( bool bVSync );
	// 要求された段階のコピー先だけを用意する。描画順への挿入は接続層が行う。
	bool PrepareFrameCapture( bool bGame, bool bWithUI );
	// 表示の提出後に完了を待ち、要求した主画面の画素を読む。失敗時はOutを保つ。
	bool ReadFrameCapture( EFrameCaptureStage Stage, TArray<FColor32>& Out );
	// GPU側だけの撮影先を、既存のデバイスとキューへ接続する。
	bool InitializeCaptureTarget( CDX12CaptureTarget& Target );
	// 外側の描画実装へGPUを貸す。終了中・未初期化はnullptr。
	ID3D12Device* GetNativeDevice();
	// 同じデバイスのDIRECTキューを貸す。終了中・未初期化はnullptr。
	ID3D12CommandQueue* GetNativeQueue();
	// 画像と描画実装で共有する貸出表。終了中・未初期化はnullptr。
	CDX12DescriptorPool* GetDescriptorPool();
	// 記録中のGPU命令列だけを貸す。フレーム外・終了中はnullptr。
	ID3D12GraphicsCommandList* GetFrameCommands();
	// 記録中の色の描画先。フレーム外・終了中は空の値。
	D3D12_CPU_DESCRIPTOR_HANDLE GetFrameColorView() const;
	// GPUの固定描画幅。未初期化・終了後は0。
	int32 GetWidth() const;
	// GPUの固定描画高さ。未初期化・終了後は0。
	int32 GetHeight() const;
	// 新規記録を止め、借用先を外せるまで待つ。失敗時は全資源を保持し、再試行できる。
	bool PrepareShutdown( uint32 TimeoutMs = 10000 );
	// API固有の描画実装へ既存の画像heapを貸す。終了中はnullptr。
	ID3D12DescriptorHeap* GetImageDescriptorHeap();
	// GPUが喪失した場合だけtrue。UI等の借用先が安全に終了するために使う。
	bool IsDeviceRemoved() const;
	// 通常フレームへのGPU記録中だけtrue。
	bool IsRecording() const;
	// 呼出側の描画が完了した位置で、ゲーム画面をコピーする。
	void RecordGameFrameCopy();
	// 現在のGPU記録位置で色を消す。描画順の管理は呼出側が行う。
	void ClearFrameColor( FColor32 Color );
	// ファイルやUIを介さず、画像資源の所有先を貸す。
	IRenderTextureResources* GetTextureResources();
	// UIと独立したバッファ資源の所有先を貸す。
	CDX12BufferStore* GetBufferResources();
	// computeプログラムの所有先を貸す。
	IComputePipelineResources* GetComputePipelineResources();
	// 同じ描画キューへcomputeを提出する窓口を貸す。
	IComputeQueue* GetComputeQueue();
	// 呼出前に提出したGPU処理を最大60000msまで待つ。0は待たず、失敗・喪失・タイムアウトはfalse。
	bool Wait( uint32 TimeoutMs = 10000 );
private:
	// GPU完了またはデバイス喪失を確認してから資源を解放する。
	void ReleaseAfterGPUStopped();
	static constexpr int32 BufferCount = 2; // 交換用画面の枚数。
	static constexpr int32 DescriptorCount = 2048; // 同時に持てる画像の上限。
	Microsoft::WRL::ComPtr<ID3D12Device> Device; // 描画デバイス。
	Microsoft::WRL::ComPtr<IDXGISwapChain3> SwapChain; // 画面の交換先。
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> Queue; // GPUへ渡す列。
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> Allocator; // 記録領域。
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> Commands; // 今フレームの命令。
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> RtvHeap; // 画面の記述子。
	CDX12DescriptorPool Descriptors; // ゲーム画像とUIへ独立した区画を貸す。
	CDX12TextureStore Textures; // poolより先に破棄するゲーム画像の所有者。
	CDX12BufferStore BufferResources; // 同じキューへバイト列を転送する独立した所有者。
	CDX12ComputePipelineStore ComputePipelines; // プログラムとその配置の所有者。
	CDX12ComputeQueue ComputeQueue; // 上の所有者より先に借用参照を戻す提出先。
	Microsoft::WRL::ComPtr<ID3D12Resource> Buffers[BufferCount]; // 交換用画面。
	CDX12QueueCompletion Completion; // UIと独立したキューの完了待機。
	CDX12TextureReadback GameReadback, UIReadback; // 必要な段階だけ準備する画素のコピー先。
	UINT BufferIndex = 0; // 今描く画面。
	UINT RtvStride = 0; // 画面記述子の幅。
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentRtv = {}; // 今描く画面の記述子。
	int32 Width = 0, Height = 0; // 交換用画面の固定描画寸法。
	bool bGPUStopped = false; // 終了開始後に借用資源を返せる完了確認。
	bool bRecording = false; // 記録中の印。
	bool bInitialized = false; // 初期化が最後まで成功した印。
	bool bClosing = false; // 終了開始後は新しい描画と窓口取得を禁止する。
	bool bCaptureGame = false, bCaptureUI = false; // このフレームで要求された撮影段階。
	bool bGameCopied = false, bUICopied = false; // 実際にコピーを記録できた段階。
	bool bFrameSubmitted = false; // 命令列の失敗で古いコピーを読み出さないための提出印。
};
