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
	using Microsoft::WRL::ComPtr;
	ComPtr<IDXGIFactory4> Factory; // アダプターの列挙元。
	ComPtr<IDXGIAdapter> Adapter; // WARP指定時のソフトウェアGPU。
	if ( FAILED( CreateDXGIFactory1( IID_PPV_ARGS( &Factory ) ) ) )
	{
		return false;
	}
	if ( bWarp && FAILED( Factory->EnumWarpAdapter( IID_PPV_ARGS( &Adapter ) ) ) )
	{
		return false;
	}
	if ( FAILED( D3D12CreateDevice( Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &Device ) ) ) )
	{
		LOG( LogDX12, Error, "Direct3D 12 device creation failed" );
		return false;
	}
	D3D12_COMMAND_QUEUE_DESC QueueDesc = {}; // 通常の描画キュー。
	QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if ( FAILED( Device->CreateCommandQueue( &QueueDesc, IID_PPV_ARGS( &Queue ) ) )
		|| FAILED( Device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &Allocator ) ) )
		|| FAILED( Device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.Get(), nullptr, IID_PPV_ARGS( &Commands ) ) )
		|| FAILED( Commands->Close() )
		|| FAILED( Device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &Fence ) ) ) )
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
	ComPtr<IDXGISwapChain1> InitialSwapChain; // 交換先の作成結果。
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
	if ( !Descriptors.Initialize( Device.Get(), DescriptorCount ) || !Textures.Initialize( Device.Get(), Queue.Get(), Descriptors ) )
	{
		return false;
	}
	Geometry = new FDX12GeometryRenderer();
	if ( !Geometry->Initialize( Device.Get(), Width, Height ) )
	{
		return false;
	}
	return ImGuiRenderer.Initialize( Device.Get(), Queue.Get(), BufferCount, Desc.Format, Descriptors );
}

bool FDX12Device::Wait()
{
	if ( !Queue || !Fence || !FenceEvent )
	{
		return false;
	}
	++FenceValue;
	if ( FAILED( Queue->Signal( Fence.Get(), FenceValue ) ) )
	{
		return false;
	}
	if ( Fence->GetCompletedValue() == UINT64_MAX )
	{
		return false; // デバイス喪失。再利用せず終了する。
	}
	if ( Fence->GetCompletedValue() < FenceValue )
	{
		if ( FAILED( Fence->SetEventOnCompletion( FenceValue, FenceEvent ) ) )
		{
			return false;
		}
		// OSがデバイス喪失を検出しない場合も無期限には待たない。
		return WaitForSingleObject( FenceEvent, 10000 ) == WAIT_OBJECT_0;
	}
	return true;
}

void FDX12Device::Shutdown()
{
	Wait();
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
	Textures.ResetAfterGPUIdle();
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
	bRecording = false;
}

bool FDX12Device::BeginFrame()
{
	if ( !SwapChain || bRecording || !Wait() || FAILED( Allocator->Reset() ) || FAILED( Commands->Reset( Allocator.Get(), nullptr ) ) )
	{
		return false;
	}
	Textures.CollectAfterGPUIdle(); // 記録済みの描画が残らない時点でだけ旧版を戻す。
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
	if ( !bRecording )
	{
		return false;
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

void FDX12Device::ClearCallback( const ImDrawList*, const ImDrawCmd* Command )
{
	const auto& Clear = *static_cast<const FClearCommand*>( Command->UserCallbackData ); // 記録済みの消去。
	const ImVec4 Rgba = ImGui::ColorConvertU32ToFloat4( Clear.Color ); // GPUへ渡すRGBA。
	const float Values[4] = { Rgba.x, Rgba.y, Rgba.z, Rgba.w }; // 消去色。
	Clear.Owner->Commands->ClearRenderTargetView( Clear.Owner->CurrentRtv, Values, 0, nullptr );
}

IRenderGeometry3D* FDX12Device::GetGeometry3D()
{
	return bRecording ? Geometry : nullptr;
}

IRenderTextureResources* FDX12Device::GetTextureResources()
{
	return &Textures;
}
