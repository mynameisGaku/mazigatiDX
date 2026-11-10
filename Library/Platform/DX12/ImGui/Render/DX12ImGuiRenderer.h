// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Platform/DX12/Resource/Descriptor/DX12DescriptorPool.h"
#include "Library/Core/Collection/Map.h"
#include "VendorModules/ImGui/imgui.h"
#include "VendorModules/ImGui/imgui_impl_dx12.h"

// ImGuiのDX12描画と、UIが借りた記述子だけを管理する。ゲーム画像を所有しない。
class CDX12ImGuiRenderer
{
public:
	// UI接続の所有は複写しない。
	CDX12ImGuiRenderer() = default;
	CDX12ImGuiRenderer( const CDX12ImGuiRenderer& ) = delete;
	CDX12ImGuiRenderer& operator=( const CDX12ImGuiRenderer& ) = delete;
	// GPU完了後、ImGui文脈・device・queue・poolより先に破棄する。
	~CDX12ImGuiRenderer();
	// 現在のImGui文脈へ公式DX12実装を接続する。依存先はShutdownまで生存させる。
	bool Initialize( ID3D12Device* Device, ID3D12CommandQueue* Queue, int32 FramesInFlight, DXGI_FORMAT Format, CDX12DescriptorPool& InPool );
	// GPU完了後に接続とUIの資源を外す。ゲーム画像には触れない。
	void Shutdown();
	// 所有文脈が現在の文脈ならフレームを準備する。
	bool BeginFrame();
	// 所有文脈のデータを記録する。別の文脈や未初期化ではfalse。
	bool Render( ImDrawData* Data, ID3D12GraphicsCommandList* Commands );
private:
	// ImGuiが要求する記述子だけをpoolから借りる。
	static void AllocateDescriptor( ImGui_ImplDX12_InitInfo* Info, D3D12_CPU_DESCRIPTOR_HANDLE* Cpu, D3D12_GPU_DESCRIPTOR_HANDLE* Gpu );
	// UIが借りた区画だけを戻す。ゲーム画像の区画は受け付けない。
	static void FreeDescriptor( ImGui_ImplDX12_InitInfo* Info, D3D12_CPU_DESCRIPTOR_HANDLE Cpu, D3D12_GPU_DESCRIPTOR_HANDLE Gpu );
	CDX12DescriptorPool* Pool = nullptr; // device所有者から借りる記述子のpool。
	ImGuiContext* Context = nullptr; // 接続した文脈。所有は呼び出し側。
	TMap<uint64, CDX12DescriptorPool::FAllocation> Descriptors; // UIの貸出分だけを追跡する。
	bool bInitialized = false; // 公式実装への接続済み印。
};
