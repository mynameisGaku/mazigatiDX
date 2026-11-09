// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "DX12DescriptorPool.h"
#include "DX12TextureStore.h"
#include "DX12BufferStore.h"
#include "DX12ComputeQueue.h"
#include "Library/Platform/DX12/ImGui/DX12ImGuiRenderer.h"

class FDX12GeometryRenderer;
class IRenderGeometry3D;

// GPU・交換用画面・同期を所有する。ウィンドウとImGui文脈は呼び出し側が保持する。
class FDX12Device
{
public:
	// 所有するGPU資源とフレーム状態は複写しない。
	FDX12Device() = default;
	FDX12Device( const FDX12Device& ) = delete;
	FDX12Device& operator=( const FDX12Device& ) = delete;
	// GPU完了またはデバイス喪失まで保持して破棄する。停止しないGPUでは待機を続ける。
	~FDX12Device();
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
	// ImGuiの描画データを表示する。失敗時はfalse。
	bool EndFrame( ImDrawData* Data, bool bVSync );
	// 過去の描画より後で画面全体を消すコールバックを記録する。
	void RecordClear( ImDrawList& List, ImU32 Color );
	// 記録中だけ基本3Dの実行先を貸す。
	IRenderGeometry3D* GetGeometry3D();
	// ファイルやUIを介さず、画像資源の所有先を貸す。
	IRenderTextureResources* GetTextureResources();
	// UIと独立したバッファ資源の所有先を貸す。
	IRenderBufferResources* GetBufferResources();
	// computeプログラムの所有先を貸す。
	IComputePipelineResources* GetComputePipelineResources();
	// 同じ描画キューへcomputeを提出する窓口を貸す。
	IComputeQueue* GetComputeQueue();
	// 呼出前に提出したGPU処理を最大60000msまで待つ。0は待たず、失敗・喪失・タイムアウトはfalse。
	bool Wait( uint32 TimeoutMs = 10000 );
private:
	// GPU完了またはデバイス喪失を確認してから資源とUI接続を解放する。
	void ReleaseAfterGPUStopped();
	// GPUの記録中に画面を消す。
	static void ClearCallback( const ImDrawList*, const ImDrawCmd* Command );
	// 消去命令の所有コピー。実行までGPU所有者を保持する。
	struct FClearCommand
	{
		FDX12Device* Owner = nullptr; // 実行先。プラットフォームより先には実行しない。
		ImU32 Color = 0; // 消去色。
	};
	static constexpr int32 BufferCount = 2; // 交換用画面の枚数。
	static constexpr int32 DescriptorCount = 2048; // 同時に持てる画像の上限。
	Microsoft::WRL::ComPtr<ID3D12Device> Device; // 描画デバイス。
	Microsoft::WRL::ComPtr<IDXGISwapChain3> SwapChain; // 画面の交換先。
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> Queue; // GPUへ渡す列。
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> Allocator; // 記録領域。
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> Commands; // 今フレームの命令。
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> RtvHeap; // 画面の記述子。
	FDX12DescriptorPool Descriptors; // ゲーム画像とUIへ独立した区画を貸す。
	FDX12TextureStore Textures; // poolより先に破棄するゲーム画像の所有者。
	FDX12BufferStore BufferResources; // 同じキューへバイト列を転送する独立した所有者。
	FDX12ComputePipelineStore ComputePipelines; // プログラムとその配置の所有者。
	FDX12ComputeQueue ComputeQueue; // 上の所有者より先に借用参照を戻す提出先。
	Microsoft::WRL::ComPtr<ID3D12Resource> Buffers[BufferCount]; // 交換用画面。
	Microsoft::WRL::ComPtr<ID3D12Fence> Fence; // 完了通知。
	FDX12ImGuiRenderer ImGuiRenderer; // UIへの接続とその資源は専用の実装が所有する。
	FDX12GeometryRenderer* Geometry = nullptr; // 所有する基本3Dパイプライン。
	HANDLE FenceEvent = nullptr; // CPUが完了を待つイベント。
	UINT64 FenceValue = 0; // 次の完了番号。
	UINT64 NotifiedValue = 0; // まだ到達していないイベント登録値。同時に一つだけ登録する。
	UINT BufferIndex = 0; // 今描く画面。
	UINT RtvStride = 0; // 画面記述子の幅。
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentRtv = {}; // 今描く画面の記述子。
	bool bRecording = false; // 記録中の印。
	bool bInitialized = false; // 初期化が最後まで成功した印。
	bool bClosing = false; // 終了開始後は新しい描画と窓口取得を禁止する。
};
