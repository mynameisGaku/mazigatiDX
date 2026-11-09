// SPDX-License-Identifier: MIT
#include "DX12ImGuiRenderer.h"

FDX12ImGuiRenderer::~FDX12ImGuiRenderer()
{
	Shutdown();
}

bool FDX12ImGuiRenderer::Initialize( ID3D12Device* Device, ID3D12CommandQueue* Queue, int32 FramesInFlight, DXGI_FORMAT Format, FDX12DescriptorPool& InPool )
{
	if ( bInitialized || !Device || !Queue || FramesInFlight <= 0 || !InPool.GetHeap() || !ImGui::GetCurrentContext() || ImGui::GetIO().BackendRendererUserData )
	{
		return false;
	}
	Microsoft::WRL::ComPtr<ID3D12Device> QueueDevice, HeapDevice; // 他のdeviceのqueueやheapを接続しない。
	if ( Queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT || FAILED( Queue->GetDevice( IID_PPV_ARGS( &QueueDevice ) ) )
		|| FAILED( InPool.GetHeap()->GetDevice( IID_PPV_ARGS( &HeapDevice ) ) ) || QueueDevice.Get() != Device || HeapDevice.Get() != Device )
	{
		return false;
	}
	Pool = &InPool;
	Context = ImGui::GetCurrentContext();
	ImGui_ImplDX12_InitInfo Info; // 公式実装にだけAPI固有の型を渡す。
	Info.Device = Device;
	Info.CommandQueue = Queue;
	Info.NumFramesInFlight = FramesInFlight;
	Info.RTVFormat = Format;
	Info.UserData = this;
	Info.SrvDescriptorHeap = Pool->GetHeap();
	Info.SrvDescriptorAllocFn = AllocateDescriptor;
	Info.SrvDescriptorFreeFn = FreeDescriptor;
	bInitialized = ImGui_ImplDX12_Init( &Info );
	if ( !bInitialized || !ImGui_ImplDX12_CreateDeviceObjects() )
	{
		Shutdown();
		return false;
	}
	return true;
}

void FDX12ImGuiRenderer::Shutdown()
{
	if ( bInitialized )
	{
		ImGuiContext* Previous = ImGui::GetCurrentContext(); // 別の文脈を変更せず、接続した文脈を外す。
		ImGui::SetCurrentContext( Context );
		ImGui_ImplDX12_Shutdown();
		ImGui::SetCurrentContext( Previous );
		bInitialized = false;
	}
	if ( Pool )
	{
		for ( const auto& Pair : Descriptors )
		{
			Pool->Release( Pair.Value );
		}
	}
	Descriptors.Reset();
	Pool = nullptr;
	Context = nullptr;
}

bool FDX12ImGuiRenderer::BeginFrame()
{
	if ( !bInitialized || ImGui::GetCurrentContext() != Context )
	{
		return false;
	}
	ImGui_ImplDX12_NewFrame();
	return true;
}

bool FDX12ImGuiRenderer::Render( ImDrawData* Data, ID3D12GraphicsCommandList* Commands )
{
	if ( !bInitialized || !Data || !Commands || ImGui::GetCurrentContext() != Context )
	{
		return false;
	}
	ImGui_ImplDX12_RenderDrawData( Data, Commands );
	return true;
}

void FDX12ImGuiRenderer::AllocateDescriptor( ImGui_ImplDX12_InitInfo* Info, D3D12_CPU_DESCRIPTOR_HANDLE* Cpu, D3D12_GPU_DESCRIPTOR_HANDLE* Gpu )
{
	auto* Self = static_cast<FDX12ImGuiRenderer*>( Info->UserData ); // UI接続だけがImGuiの型を解釈する。
	FDX12DescriptorPool::FAllocation Allocation; // UIの所有分として区画を借りる。
	*Cpu = {};
	*Gpu = {};
	if ( Self->Pool && Self->Pool->Allocate( Allocation ) )
	{
		Self->Descriptors.Add( Allocation.Cpu.ptr, Allocation );
		*Cpu = Allocation.Cpu;
		*Gpu = Allocation.Gpu;
	}
}

void FDX12ImGuiRenderer::FreeDescriptor( ImGui_ImplDX12_InitInfo* Info, D3D12_CPU_DESCRIPTOR_HANDLE Cpu, D3D12_GPU_DESCRIPTOR_HANDLE Gpu )
{
	auto* Self = static_cast<FDX12ImGuiRenderer*>( Info->UserData ); // UIの貸出分だけを返す。
	if ( const auto* Allocation = Self->Descriptors.Find( Cpu.ptr ) )
	{
		if ( Self->Pool && Allocation->Gpu.ptr == Gpu.ptr )
		{
			Self->Pool->Release( *Allocation );
			Self->Descriptors.Remove( Cpu.ptr );
		}
	}
}
