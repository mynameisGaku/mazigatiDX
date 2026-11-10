// SPDX-License-Identifier: MIT
#include "DX12Device.h"
#include "Library/Platform/DX12/Render/Capture/DX12CaptureTarget.h"
#include "Library/Core/Log/Log.h"
#include "Library/Platform/DX12/DX12Log.h"
#include "Library/Core/Memory/Memory.h"

#pragma comment( lib, "d3d12.lib" )
#pragma comment( lib, "dxgi.lib" )
#pragma comment( lib, "d3dcompiler.lib" )
DEFINE_LOG_CATEGORY( LogDX12 )

CDX12Device::~CDX12Device()
{
	Shutdown();
}

bool CDX12Device::Initialize( HWND Window, int32 Width, int32 Height, bool bWarp )
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

bool CDX12Device::InitializeOnQueue( HWND Window, int32 Width, int32 Height, ID3D12CommandQueue* InQueue )
{
	if ( Device || bClosing || !IsWindow( Window ) || Width <= 0 || Height <= 0 || Width > 16384 || Height > 16384 || !InQueue || InQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT )
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
	if ( FAILED( CreateDXGIFactory1( IID_PPV_ARGS( &Factory ) ) ) || FAILED( Device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &Allocator ) ) ) || FAILED( Device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.Get(), nullptr, IID_PPV_ARGS( &Commands ) ) ) || FAILED( Commands->Close() ) || !Completion.Initialize( Device.Get(), Queue.Get() ) )
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
	this->Width = Width;
	this->Height = Height;
	bInitialized = true;
	return true;
}

bool CDX12Device::Wait( uint32 TimeoutMs )
{
	return Completion.Wait( TimeoutMs );
}

bool CDX12Device::PrepareShutdown( uint32 TimeoutMs )
{
	if ( TimeoutMs > 60000 )
	{
		return false;
	}
	bClosing = true;
	// 完了後は呼出側も提出しない契約。借用先を外す前後に異なる待機を追加しない。
	if ( !bGPUStopped && Device && Queue && Completion.IsInitialized() && !Wait( TimeoutMs ) && SUCCEEDED( Device->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	bGPUStopped = true;
	return true;
}

bool CDX12Device::TryShutdown( uint32 TimeoutMs )
{
	if ( !PrepareShutdown( TimeoutMs ) )
	{
		return false;
	}
	ReleaseAfterGPUStopped();
	return true;
}

void CDX12Device::Shutdown()
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

void CDX12Device::ReleaseAfterGPUStopped()
{
	GameReadback.ResetAfterGPUIdle();
	UIReadback.ResetAfterGPUIdle();
	for ( auto& Buffer : Buffers )
	{
		Buffer.Reset();
	}
	Commands.Reset();
	Allocator.Reset();
	SwapChain.Reset();
	ComputeQueue.ResetAfterGPUIdle();
	ComputePipelines.ResetAfterGPUIdle();
	Textures.ResetAfterGPUIdle();
	BufferResources.ResetAfterGPUIdle();
	Descriptors.Reset();
	RtvHeap.Reset();
	Completion.ResetAfterGPUIdle();
	Queue.Reset();
	Device.Reset();
	BufferIndex = RtvStride = 0;
	CurrentRtv = {};
	bRecording = false;
	bPresentPending = false;
	bInitialized = false;
	bClosing = false;
	bGPUStopped = false;
	Width = Height = 0;
}

uint32 CDX12Device::GetCurrentBackBufferIndex() const
{
	return bInitialized && !bClosing && SwapChain ? SwapChain->GetCurrentBackBufferIndex() : 0;
}

bool CDX12Device::BeginFrame()
{
	return BeginFrameForBuffer( GetCurrentBackBufferIndex() );
}

bool CDX12Device::BeginFrameForBuffer( uint32 Index )
{
	if ( !bInitialized || bClosing || bRecording || bPresentPending || Index >= BufferCount || !Wait() || FAILED( Allocator->Reset() ) || FAILED( Commands->Reset( Allocator.Get(), nullptr ) ) )
	{
		return false;
	}
	ComputeQueue.CollectAfterGPUIdle();
	ComputePipelines.CollectAfterGPUIdle();
	Textures.CollectAfterGPUIdle(); // 記録済みの描画が残らない時点でだけ旧版を戻す。
	BufferResources.CollectAfterGPUIdle();
	bCaptureGame = bCaptureUI = bGameCopied = bUICopied = false;
	bFrameSubmitted = false;
	BufferIndex = Index;
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
	bRecording = true;
	return true;
}

bool CDX12Device::EndFrame( bool bVSync )
{
	return SubmitFrame() && PresentFrame( bVSync );
}

bool CDX12Device::SubmitFrame()
{
	if ( !bInitialized || bClosing || !bRecording )
	{
		return false;
	}
	if ( bCaptureUI )
	{
		bUICopied = UIReadback.RecordCopy( Commands.Get(), Buffers[BufferIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET );
	}
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
	bFrameSubmitted = true;
	bPresentPending = true;
	return true;
}

bool CDX12Device::PresentFrame( bool bVSync )
{
	if ( !bInitialized || bClosing || bRecording || !bPresentPending )
	{
		return false;
	}
	bPresentPending = false;
	return SUCCEEDED( SwapChain->Present( bVSync ? 1 : 0, 0 ) );
}

void CDX12Device::ClearFrameColor( FColor32 Color )
{
	if ( IsRecording() )
	{
		const float Values[4] = { Color.r / 255.0f, Color.g / 255.0f, Color.b / 255.0f, Color.a / 255.0f }; // GPUへ渡す正規化した色。
		Commands->ClearRenderTargetView( CurrentRtv, Values, 0, nullptr );
	}
}

bool CDX12Device::PrepareFrameCapture( bool bGame, bool bWithUI )
{
	if ( !bInitialized || bClosing || !bRecording || bCaptureGame || bCaptureUI )
	{
		return false;
	}
	if ( ( bGame && !GameReadback.InitializeAfterGPUIdle( Device.Get(), Buffers[BufferIndex].Get() ) ) || ( bWithUI && !UIReadback.InitializeAfterGPUIdle( Device.Get(), Buffers[BufferIndex].Get() ) ) )
	{
		return false;
	}
	bCaptureGame = bGame;
	bCaptureUI = bWithUI;
	return true;
}

void CDX12Device::RecordGameFrameCopy()
{
	if ( bInitialized && !bClosing && bRecording && bCaptureGame )
	{
		bGameCopied = GameReadback.RecordCopy( Commands.Get(), Buffers[BufferIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET );
	}
}

bool CDX12Device::ReadFrameCapture( EFrameCaptureStage Stage, TArray<FColor32>& Out )
{
	if ( !bInitialized || bClosing || bRecording || !bFrameSubmitted || ( Stage != EFrameCaptureStage::Game && Stage != EFrameCaptureStage::WithDebugUI ) || !( Stage == EFrameCaptureStage::Game ? bGameCopied : bUICopied ) || !Wait() )
	{
		return false;
	}
	return ( Stage == EFrameCaptureStage::Game ? GameReadback : UIReadback ).ReadAfterGPUIdle( Out, true );
}

bool CDX12Device::InitializeCaptureTarget( CDX12CaptureTarget& Target )
{
	return bInitialized && !bClosing && Target.Initialize( Device.Get(), Queue.Get() );
}

ID3D12Device* CDX12Device::GetNativeDevice() { return bInitialized && !bClosing ? Device.Get() : nullptr; }
ID3D12CommandQueue* CDX12Device::GetNativeQueue() { return bInitialized && !bClosing ? Queue.Get() : nullptr; }
CDX12DescriptorPool* CDX12Device::GetDescriptorPool() { return bInitialized && !bClosing ? &Descriptors : nullptr; }
ID3D12GraphicsCommandList* CDX12Device::GetFrameCommands() { return IsRecording() ? Commands.Get() : nullptr; }
D3D12_CPU_DESCRIPTOR_HANDLE CDX12Device::GetFrameColorView() const { return IsRecording() ? CurrentRtv : D3D12_CPU_DESCRIPTOR_HANDLE {}; }
int32 CDX12Device::GetWidth() const { return Width; }
int32 CDX12Device::GetHeight() const { return Height; }

ID3D12DescriptorHeap* CDX12Device::GetImageDescriptorHeap()
{
	return bInitialized && !bClosing ? Descriptors.GetHeap() : nullptr;
}

bool CDX12Device::IsDeviceRemoved() const { return Device && FAILED( Device->GetDeviceRemovedReason() ); }

bool CDX12Device::IsRecording() const { return bInitialized && !bClosing && bRecording; }

IRenderTextureResources* CDX12Device::GetTextureResources()
{
	return bInitialized && !bClosing ? &Textures : nullptr;
}

CDX12BufferStore* CDX12Device::GetBufferResources()
{
	return bInitialized && !bClosing ? &BufferResources : nullptr;
}

IComputePipelineResources* CDX12Device::GetComputePipelineResources()
{
	return bInitialized && !bClosing ? &ComputePipelines : nullptr;
}

IComputeQueue* CDX12Device::GetComputeQueue()
{
	return bInitialized && !bClosing ? &ComputeQueue : nullptr;
}
