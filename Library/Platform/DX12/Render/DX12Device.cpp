// SPDX-License-Identifier: MIT
#include "DX12Device.h"
#include "DX12GeometryRenderer.h"
#include "Library/Core/Log/Log.h"
#include "Library/Platform/DX12/DX12Log.h"
#include "Library/Core/Memory/Memory.h"

#pragma comment( lib, "d3d12.lib" )
#pragma comment( lib, "dxgi.lib" )
#pragma comment( lib, "d3dcompiler.lib" )
DEFINE_LOG_CATEGORY( LogDX12 )

FDX12Device::~FDX12Device()
{
	Shutdown();
}

bool FDX12Device::Initialize( HWND Window, int32 Width, int32 Height, bool bWarp )
{
	if ( Device || bClosing || !IsWindow( Window ) || Width <= 0 || Height <= 0 || Width > 16384 || Height > 16384 )
	{
		return false;
	}
	using Microsoft::WRL::ComPtr;
	ComPtr<IDXGIFactory4> Factory; // アダプターの列挙元。
	ComPtr<IDXGIAdapter> Adapter; // WARP指定時のソフトウェアGPU。
	ComPtr<ID3D12Device> CreatedDevice; // 所有先へ渡す前の描画デバイス。
	ComPtr<ID3D12CommandQueue> CreatedQueue; // 同じデバイスで作るDIRECTキュー。
	if ( FAILED( CreateDXGIFactory1( IID_PPV_ARGS( &Factory ) ) ) )
	{
		return false;
	}
	if ( bWarp && FAILED( Factory->EnumWarpAdapter( IID_PPV_ARGS( &Adapter ) ) ) )
	{
		return false;
	}
	if ( FAILED( D3D12CreateDevice( Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &CreatedDevice ) ) ) )
	{
		LOG( LogDX12, Error, "Direct3D 12 device creation failed" );
		return false;
	}
	D3D12_COMMAND_QUEUE_DESC QueueDesc = {}; // 通常の描画キュー。
	QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if ( FAILED( CreatedDevice->CreateCommandQueue( &QueueDesc, IID_PPV_ARGS( &CreatedQueue ) ) ) )
	{
		return false;
	}
	return InitializeOnQueue( Window, Width, Height, CreatedQueue.Get() );
}

bool FDX12Device::InitializeOnQueue( HWND Window, int32 Width, int32 Height, ID3D12CommandQueue* InQueue )
{
	if ( Device || bClosing || !IsWindow( Window ) || Width <= 0 || Height <= 0 || Width > 16384 || Height > 16384 || !InQueue || InQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT || !ImGui::GetCurrentContext() || ImGui::GetIO().BackendRendererUserData )
	{
		return false;
	}
	Microsoft::WRL::ComPtr<ID3D12Device> QueueDevice; // キューが実際に属するデバイスを共有する。
	if ( FAILED( InQueue->GetDevice( IID_PPV_ARGS( &QueueDevice ) ) ) || FAILED( QueueDevice->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	Device = QueueDevice;
	Queue = InQueue;
	Microsoft::WRL::ComPtr<IDXGIFactory4> Factory; // このキューへ交換用画面を接続する列挙元。
	if ( FAILED( CreateDXGIFactory1( IID_PPV_ARGS( &Factory ) ) ) || FAILED( Device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &Allocator ) ) ) || FAILED( Device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.Get(), nullptr, IID_PPV_ARGS( &Commands ) ) ) || FAILED( Commands->Close() ) || FAILED( Device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &Fence ) ) ) )
	{
		return false;
	}
	FenceEvent = CreateEventW( nullptr, FALSE, FALSE, nullptr );
	if ( !FenceEvent )
	{
		return false;
	}
	DXGI_SWAP_CHAIN_DESC1 Desc = {}; // 論理解像度を固定して表示先へ拡大する。
	Desc.Width = Width;
	Desc.Height = Height;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	Desc.BufferCount = BufferCount;
	Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	Desc.Scaling = DXGI_SCALING_STRETCH;
	Microsoft::WRL::ComPtr<IDXGISwapChain1> InitialSwapChain; // 交換先の作成結果。
	if ( FAILED( Factory->CreateSwapChainForHwnd( Queue.Get(), Window, &Desc, nullptr, nullptr, &InitialSwapChain ) )
		|| FAILED( InitialSwapChain.As( &SwapChain ) )
		|| FAILED( Factory->MakeWindowAssociation( Window, DXGI_MWA_NO_ALT_ENTER ) ) )
	{
		return false;
	}
	D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {}; // 画面用の記述子。
	HeapDesc.NumDescriptors = BufferCount;
	HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	if ( FAILED( Device->CreateDescriptorHeap( &HeapDesc, IID_PPV_ARGS( &RtvHeap ) ) ) )
	{
		return false;
	}
	RtvStride = Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
	D3D12_CPU_DESCRIPTOR_HANDLE Rtv = RtvHeap->GetCPUDescriptorHandleForHeapStart(); // 順に画面を登録する。
	for ( int32 Index = 0; Index < BufferCount; ++Index )
	{
		if ( FAILED( SwapChain->GetBuffer( Index, IID_PPV_ARGS( &Buffers[Index] ) ) ) )
		{
			return false;
		}
		Device->CreateRenderTargetView( Buffers[Index].Get(), nullptr, Rtv );
		Rtv.ptr += RtvStride;
	}
	if ( !Descriptors.Initialize( Device.Get(), DescriptorCount ) || !Textures.Initialize( Device.Get(), Queue.Get(), Descriptors ) || !BufferResources.Initialize( Device.Get(), Queue.Get() ) )
	{
		return false;
	}
	if ( !ComputePipelines.Initialize( Device.Get() ) || !ComputeQueue.Initialize( Device.Get(), Queue.Get(), BufferResources, ComputePipelines ) )
	{
		return false;
	}
	Geometry = new FDX12GeometryRenderer();
	if ( !Geometry->Initialize( Device.Get(), Width, Height ) )
	{
		return false;
	}
	bInitialized = ImGuiRenderer.Initialize( Device.Get(), Queue.Get(), BufferCount, Desc.Format, Descriptors );
	return bInitialized;
}

bool FDX12Device::Wait( uint32 TimeoutMs )
{
	if ( TimeoutMs > 60000 || !Device || !Queue || !Fence || !FenceEvent || FenceValue >= UINT64_MAX - 1 || FAILED( Device->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	++FenceValue;
	if ( FAILED( Queue->Signal( Fence.Get(), FenceValue ) ) )
	{
		return false;
	}
	const uint64 Started = GetTickCount64(); // 古い通知で起こされても今回の期限は延長しない。
	for ( ;; )
	{
		const uint64 Completed = Fence->GetCompletedValue(); // 喪失を通常の完了番号と混同しない。
		if ( Completed == UINT64_MAX || FAILED( Device->GetDeviceRemovedReason() ) )
		{
			return false;
		}
		if ( Completed >= FenceValue )
		{
			return true;
		}
		const uint64 Elapsed = GetTickCount64() - Started; // 経過時間を単調な時計で求める。
		if ( Elapsed >= TimeoutMs )
		{
			return false;
		}
		if ( NotifiedValue == 0 || Completed >= NotifiedValue )
		{
			if ( FAILED( Fence->SetEventOnCompletion( FenceValue, FenceEvent ) ) )
			{
				return false;
			}
			NotifiedValue = FenceValue;
		}
		const DWORD Result = WaitForSingleObject( FenceEvent, static_cast<DWORD>( TimeoutMs - Elapsed ) ); // 残り時間だけ待つ。
		if ( Result != WAIT_OBJECT_0 && Result != WAIT_TIMEOUT )
		{
			return false;
		}
	}
}

bool FDX12Device::TryShutdown( uint32 TimeoutMs )
{
	if ( TimeoutMs > 60000 )
	{
		return false;
	}
	bClosing = true;
	// 待機機構の作成前はまだGPUへ提出していない。完成後は停止の確認を必須にする。
	if ( Device && Queue && Fence && FenceEvent && !Wait( TimeoutMs ) && SUCCEEDED( Device->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	ReleaseAfterGPUStopped();
	return true;
}

void FDX12Device::Shutdown()
{
	if ( TryShutdown() )
	{
		return;
	}
	LOG( LogDX12, Warning, "DX12 shutdown is retaining resources until GPU completion or device removal" );
	while ( !TryShutdown( 1000 ) )
	{
		Sleep( 1 );
	}
}

void FDX12Device::ReleaseAfterGPUStopped()
{
	ImGuiRenderer.Shutdown();
	for ( auto& Buffer : Buffers )
	{
		Buffer.Reset();
	}
	delete Geometry;
	Geometry = nullptr;
	Commands.Reset();
	Allocator.Reset();
	SwapChain.Reset();
	ComputeQueue.ResetAfterGPUIdle();
	ComputePipelines.ResetAfterGPUIdle();
	Textures.ResetAfterGPUIdle();
	BufferResources.ResetAfterGPUIdle();
	Descriptors.Reset();
	RtvHeap.Reset();
	Fence.Reset();
	Queue.Reset();
	Device.Reset();
	if ( FenceEvent )
	{
		CloseHandle( FenceEvent );
		FenceEvent = nullptr;
	}
	FenceValue = 0;
	NotifiedValue = 0;
	BufferIndex = RtvStride = 0;
	CurrentRtv = {};
	bRecording = false;
	bInitialized = false;
	bClosing = false;
}

bool FDX12Device::BeginFrame()
{
	if ( !bInitialized || bClosing || bRecording || !Wait() || FAILED( Allocator->Reset() ) || FAILED( Commands->Reset( Allocator.Get(), nullptr ) ) )
	{
		return false;
	}
	ComputeQueue.CollectAfterGPUIdle();
	ComputePipelines.CollectAfterGPUIdle();
	Textures.CollectAfterGPUIdle(); // 記録済みの描画が残らない時点でだけ旧版を戻す。
	BufferResources.CollectAfterGPUIdle();
	BufferIndex = SwapChain->GetCurrentBackBufferIndex();
	CurrentRtv = RtvHeap->GetCPUDescriptorHandleForHeapStart();
	CurrentRtv.ptr += BufferIndex * RtvStride;
	D3D12_RESOURCE_BARRIER Barrier = {}; // 表示から描画へ切り替える。
	Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Barrier.Transition.pResource = Buffers[BufferIndex].Get();
	Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	Commands->ResourceBarrier( 1, &Barrier );
	Commands->OMSetRenderTargets( 1, &CurrentRtv, FALSE, nullptr );
	ID3D12DescriptorHeap* Heaps[] = { Descriptors.GetHeap() }; // 画像を描くための記述子。
	Commands->SetDescriptorHeaps( 1, Heaps );
	const float Black[4] = { 0, 0, 0, 1 }; // 毎フレームの背景。
	Commands->ClearRenderTargetView( CurrentRtv, Black, 0, nullptr );
	Geometry->BeginFrame( Commands.Get(), CurrentRtv, Descriptors.GetHeap() );
	if ( !ImGuiRenderer.BeginFrame() )
	{
		return false;
	}
	bRecording = true;
	return true;
}

bool FDX12Device::EndFrame( ImDrawData* Data, bool bVSync )
{
	if ( !bInitialized || bClosing || !bRecording )
	{
		return false;
	}
	if ( Data && Data->DisplaySize.x > 0 && Data->DisplaySize.y > 0 )
	{
		Data->FramebufferScale = ImVec2( Geometry->GetWidth() / Data->DisplaySize.x, Geometry->GetHeight() / Data->DisplaySize.y );
	}
	if ( !ImGuiRenderer.Render( Data, Commands.Get() ) )
	{
		return false;
	}
	Geometry->EndFrame();
	D3D12_RESOURCE_BARRIER Barrier = {}; // 描画結果を表示用へ戻す。
	Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Barrier.Transition.pResource = Buffers[BufferIndex].Get();
	Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	Commands->ResourceBarrier( 1, &Barrier );
	bRecording = false;
	if ( FAILED( Commands->Close() ) )
	{
		return false;
	}
	ID3D12CommandList* Lists[] = { Commands.Get() }; // 今フレームを一度だけ送る。
	Queue->ExecuteCommandLists( 1, Lists );
	return SUCCEEDED( SwapChain->Present( bVSync ? 1 : 0, 0 ) );
}

void FDX12Device::RecordClear( ImDrawList& List, ImU32 Color )
{
	if ( !bInitialized || bClosing || !bRecording )
	{
		return;
	}
	FClearCommand Clear = { this, Color }; // 実行まで残す所有先と消去色。
	List.AddCallback( ClearCallback, &Clear, sizeof( Clear ) );
}

void FDX12Device::ClearCallback( const ImDrawList*, const ImDrawCmd* Command )
{
	const auto& Clear = *static_cast<const FClearCommand*>( Command->UserCallbackData ); // 記録済みの消去。
	const ImVec4 Rgba = ImGui::ColorConvertU32ToFloat4( Clear.Color ); // GPUへ渡すRGBA。
	const float Values[4] = { Rgba.x, Rgba.y, Rgba.z, Rgba.w }; // 消去色。
	Clear.Owner->Commands->ClearRenderTargetView( Clear.Owner->CurrentRtv, Values, 0, nullptr );
}

IRenderGeometry3D* FDX12Device::GetGeometry3D()
{
	return bInitialized && !bClosing && bRecording ? Geometry : nullptr;
}

IRenderTextureResources* FDX12Device::GetTextureResources()
{
	return bInitialized && !bClosing ? &Textures : nullptr;
}

IRenderBufferResources* FDX12Device::GetBufferResources()
{
	return bInitialized && !bClosing ? &BufferResources : nullptr;
}

IComputePipelineResources* FDX12Device::GetComputePipelineResources()
{
	return bInitialized && !bClosing ? &ComputePipelines : nullptr;
}

IComputeQueue* FDX12Device::GetComputeQueue()
{
	return bInitialized && !bClosing ? &ComputeQueue : nullptr;
}
