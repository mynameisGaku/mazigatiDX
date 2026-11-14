// SPDX-License-Identifier: MIT
#include "DX12ImGuiRenderer.h"

CDX12ImGuiRenderer::~CDX12ImGuiRenderer()
{
	Shutdown();
}

bool CDX12ImGuiRenderer::Initialize( ID3D12Device* Device, ID3D12CommandQueue* Queue, int32 FramesInFlight, DXGI_FORMAT Format, CDX12DescriptorPool& InPool )
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
	if ( !Completion.Initialize( Device, Queue ) )
	{
		return false;
	}
	Pool = &InPool;
	DeviceRef = Device;
	QueueRef = Queue;
	DescriptorHeapRef = InPool.GetHeap();
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
	ImGuiIO& IO = ImGui::GetIO(); // 初期設定を保ちながら、後から使う外部窓の接続も登録する。
	const bool bInitiallyEnabled = ( IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable ) != 0; // 起動時に指定した表示モード。
	IO.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	bInitialized = ImGui_ImplDX12_Init( &Info );
	if ( !bInitiallyEnabled )
	{
		IO.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
	}
	if ( !bInitialized || !ImGui_ImplDX12_CreateDeviceObjects() )
	{
		Shutdown();
		return false;
	}
	return true;
}

bool CDX12ImGuiRenderer::TryShutdown( bool bGpuConfirmedIdleOrRemoved )
{
	if ( bInitialized )
	{
		ImGuiContext* Previous = ImGui::GetCurrentContext(); // 別の文脈を変更せず、接続した文脈を外す。
		ImGui::SetCurrentContext( Context );
		const bool bDetached = ImGui_ImplDX12_TryShutdown( bGpuConfirmedIdleOrRemoved );
		ImGui::SetCurrentContext( Previous );
		if ( !bDetached )
		{
			return false;
		}
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
	Completion.ResetAfterGPUIdle();
	DescriptorHeapRef.Reset();
	QueueRef.Reset();
	DeviceRef.Reset();
	Context = nullptr;
	return true;
}

bool CDX12ImGuiRenderer::WaitQueue( uint32 TimeoutMs )
{
	if ( DeviceRef && FAILED( DeviceRef->GetDeviceRemovedReason() ) )
	{
		return true;
	}
	return Completion.IsInitialized() ? Completion.Wait( TimeoutMs ) : !QueueRef;
}

ID3D12CommandQueue* CDX12ImGuiRenderer::GetQueue() const
{
	return QueueRef.Get();
}

void CDX12ImGuiRenderer::Shutdown()
{
	while ( !TryShutdown( WaitQueue( 1000 ) ) )
	{
		Sleep( 1 );
	}
}

bool CDX12ImGuiRenderer::BeginFrame()
{
	if ( !bInitialized || ImGui::GetCurrentContext() != Context )
	{
		return false;
	}
	ImGui_ImplDX12_NewFrame();
	return true;
}

bool CDX12ImGuiRenderer::Render( ImDrawData* Data, ID3D12GraphicsCommandList* Commands )
{
	if ( !bInitialized || !Data || !Commands || ImGui::GetCurrentContext() != Context )
	{
		return false;
	}
	ID3D12DescriptorHeap* Heap = Pool->GetHeap(); // 他のゲーム描画が使ったheapへ依存しない。
	Commands->SetDescriptorHeaps( 1, &Heap );
	ImGui_ImplDX12_RenderDrawData( Data, Commands );
	return true;
}

void CDX12ImGuiRenderer::AllocateDescriptor( ImGui_ImplDX12_InitInfo* Info, D3D12_CPU_DESCRIPTOR_HANDLE* Cpu, D3D12_GPU_DESCRIPTOR_HANDLE* Gpu )
{
	auto* Self = static_cast<CDX12ImGuiRenderer*>( Info->UserData ); // UI接続だけがImGuiの型を解釈する。
	CDX12DescriptorPool::FAllocation Allocation; // UIの所有分として区画を借りる。
	*Cpu = {};
	*Gpu = {};
	if ( Self->Pool && Self->Pool->Allocate( Allocation ) )
	{
		Self->Descriptors.Add( Allocation.Cpu.ptr, Allocation );
		*Cpu = Allocation.Cpu;
		*Gpu = Allocation.Gpu;
	}
}

void CDX12ImGuiRenderer::FreeDescriptor( ImGui_ImplDX12_InitInfo* Info, D3D12_CPU_DESCRIPTOR_HANDLE Cpu, D3D12_GPU_DESCRIPTOR_HANDLE Gpu )
{
	auto* Self = static_cast<CDX12ImGuiRenderer*>( Info->UserData ); // UIの貸出分だけを返す。
	if ( const auto* Allocation = Self->Descriptors.Find( Cpu.ptr ) )
	{
		if ( Self->Pool && Allocation->Gpu.ptr == Gpu.ptr )
		{
			Self->Pool->Release( *Allocation );
			Self->Descriptors.Remove( Cpu.ptr );
		}
	}
}
