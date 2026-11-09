// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "Library/Core/Collection/Array.h"
#include "VendorModules/ImGui/imgui.h"
#include "VendorModules/ImGui/imgui_impl_dx12.h"

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
	// 途中の初期化失敗でも所有した資源を解放する。
	~FDX12Device();
	// Windowへ表示するデバイスを作る。WARP指定はGPUなしの確認用。
	bool Initialize( HWND Window, int32 Width, int32 Height, bool bWarp );
	// 使用中のGPU処理が終わるまで待って、すべて解放する。
	void Shutdown();
	// 前フレームを待ち、記録を開始する。失敗時はfalse。
	bool BeginFrame();
	// ImGuiの描画データを表示する。失敗時はfalse。
	bool EndFrame( ImDrawData* Data, bool bVSync );
	// 過去の描画より後で画面全体を消すコールバックを記録する。
	void RecordClear( ImDrawList& List, ImU32 Color );
	// 記録中だけ基本3Dの実行先を貸す。
	IRenderGeometry3D* GetGeometry3D();
	// GPU完了を待つ。デバイス喪失時はfalse。
	bool Wait();
private:
	// ImGuiが要求する画像の記述子を貸し出す。
	static void AllocateDescriptor( ImGui_ImplDX12_InitInfo* Info, D3D12_CPU_DESCRIPTOR_HANDLE* Cpu, D3D12_GPU_DESCRIPTOR_HANDLE* Gpu );
	// 貸し出した記述子を戻す。
	static void FreeDescriptor( ImGui_ImplDX12_InitInfo* Info, D3D12_CPU_DESCRIPTOR_HANDLE Cpu, D3D12_GPU_DESCRIPTOR_HANDLE Gpu );
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
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> SrvHeap; // 画像の記述子。
	Microsoft::WRL::ComPtr<ID3D12Resource> Buffers[BufferCount]; // 交換用画面。
	Microsoft::WRL::ComPtr<ID3D12Fence> Fence; // 完了通知。
	TArray<int32> FreeDescriptors; // 未使用の画像記述子。
	FDX12GeometryRenderer* Geometry = nullptr; // 所有する基本3Dパイプライン。
	HANDLE FenceEvent = nullptr; // CPUが完了を待つイベント。
	UINT64 FenceValue = 0; // 次の完了番号。
	UINT BufferIndex = 0; // 今描く画面。
	UINT RtvStride = 0; // 画面記述子の幅。
	UINT SrvStride = 0; // 画像記述子の幅。
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentRtv = {}; // 今描く画面の記述子。
	bool bRendererInitialized = false; // ImGui描画実装の生成済み印。
	bool bRecording = false; // 記録中の印。
};
