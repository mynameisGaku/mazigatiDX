// SPDX-License-Identifier: MIT
#include "DX12Device.h"
#include "Library/Platform/DX12/Render/Capture/DX12CaptureTarget.h"
#include "Library/Platform/DX12/Resource/Texture/DX12TextureCopy.h"
#include "Library/Core/Log/Log.h"
#include "Library/Platform/DX12/DX12Log.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Time/PlatformTime.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Platform/System/CommandLine.h"
#include <d3d12sdklayers.h>

#pragma comment( lib, "d3d12.lib" )
#pragma comment( lib, "dxgi.lib" )
#pragma comment( lib, "d3dcompiler.lib" )
DEFINE_LOG_CATEGORY( LogDX12 )

namespace
{
	// SDKのデバッグレイヤーのメッセージを、ログへ出す（--dx12-debug-layer=true のとき）。警告以上だけ。
	void __stdcall LogDebugLayerMessage( D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY Severity, D3D12_MESSAGE_ID Id, LPCSTR Description, void* )
	{
		if ( Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION || Severity == D3D12_MESSAGE_SEVERITY_ERROR )
		{
			LOG( LogDX12, Error, "D3D12 debug layer (%d): %s", static_cast<int>( Id ), Description );
		}
		else if ( Severity == D3D12_MESSAGE_SEVERITY_WARNING )
		{
			LOG( LogDX12, Warning, "D3D12 debug layer (%d): %s", static_cast<int>( Id ), Description );
		}
	}
}

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
	// 確認用: --dx12-debug-layer=true で、SDKのデバッグレイヤーを有効にして、警告以上のメッセージをログへ出す。
	const auto DebugLayerOption = NCommandLine::GetValue( "dx12-debug-layer" );
	const bool bDebugLayer = DebugLayerOption.IsSet() && DebugLayerOption.GetValue().Equals( "true", ESearchCase::IgnoreCase );
	if ( bDebugLayer )
	{
		ComPtr<ID3D12Debug> DebugInterface;
		if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &DebugInterface ) ) ) )
		{
			DebugInterface->EnableDebugLayer();
			LOG( LogDX12, Log, "The D3D12 debug layer is enabled" );
		}
		else
		{
			LOG( LogDX12, Warning, "The D3D12 debug layer is not available (install the Graphics Tools feature)" );
		}
	}
	if ( FAILED( D3D12CreateDevice( Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &CreatedDevice ) ) ) )
	{
		LOG( LogDX12, Error, "Direct3D 12 device creation failed" );
		return false;
	}
	if ( bDebugLayer )
	{
		ComPtr<ID3D12InfoQueue1> InfoQueue;
		DWORD Cookie = 0;
		if ( SUCCEEDED( CreatedDevice.As( &InfoQueue ) ) )
		{
			InfoQueue->RegisterMessageCallback( LogDebugLayerMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &Cookie );
		}
	}
	// 使うGPUの名前と専用メモリを、ログへ出す（内蔵GPUと別のGPUがあるPCで、どちらで動いているかを見分ける）。
	ComPtr<IDXGIAdapter1> UsedAdapter; // 作ったデバイスのアダプター。
	DXGI_ADAPTER_DESC1 AdapterDesc = {};
	if ( SUCCEEDED( Factory->EnumAdapterByLuid( CreatedDevice->GetAdapterLuid(), IID_PPV_ARGS( &UsedAdapter ) ) ) && SUCCEEDED( UsedAdapter->GetDesc1( &AdapterDesc ) ) )
	{
		char AdapterName[256] = {}; // UTF-8 のアダプター名。
		WideCharToMultiByte( CP_UTF8, 0, AdapterDesc.Description, -1, AdapterName, static_cast<int>( sizeof( AdapterName ) ) - 1, nullptr, nullptr );
		LOG( LogDX12, Log, "DX12 adapter: %s (%d MB dedicated video memory%s)", AdapterName, static_cast<int>( AdapterDesc.DedicatedVideoMemory / ( 1024 * 1024 ) ), bWarp ? ", WARP" : "" );
	}
	D3D12_COMMAND_QUEUE_DESC QueueDesc = {}; // 通常の描画キュー。
	QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if ( FAILED( CreatedDevice->CreateCommandQueue( &QueueDesc, IID_PPV_ARGS( &CreatedQueue ) ) ) )
	{
		return false;
	}
	return InitializeOnQueue( Window, Width, Height, CreatedQueue.Get() );
}

bool CDX12Device::InitializeOnQueue( HWND Window, int32 Width, int32 Height, ID3D12CommandQueue *InQueue )
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
	// 初期化中はこの所有者のGPU命令を提出しないため、失敗時は他のキュー利用者を待たずに新規資源だけを戻せる。
	auto FailInitialization = [this]() { ReleaseOwnedResources(); return false; };
	Microsoft::WRL::ComPtr<IDXGIFactory4> Factory; // このキューへ交換用画面を接続する列挙元。
	if ( FAILED( CreateDXGIFactory1( IID_PPV_ARGS( &Factory ) ) ) || FAILED( Device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &Allocator ) ) ) || FAILED( Device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.Get(), nullptr, IID_PPV_ARGS( &Commands ) ) ) || FAILED( Commands->Close() ) || !Completion.Initialize( Device.Get(), Queue.Get() ) )
	{
		return FailInitialization();
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
	if ( FAILED( Factory->CreateSwapChainForHwnd( Queue.Get(), Window, &Desc, nullptr, nullptr, &InitialSwapChain ) ) || FAILED( InitialSwapChain.As( &SwapChain ) ) || FAILED( Factory->MakeWindowAssociation( Window, DXGI_MWA_NO_ALT_ENTER ) ) )
	{
		return FailInitialization();
	}
	D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {}; // 画面用の記述子。
	HeapDesc.NumDescriptors = BufferCount;
	HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	if ( FAILED( Device->CreateDescriptorHeap( &HeapDesc, IID_PPV_ARGS( &RtvHeap ) ) ) )
	{
		return FailInitialization();
	}
	RtvStride = Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
	D3D12_CPU_DESCRIPTOR_HANDLE Rtv = RtvHeap->GetCPUDescriptorHandleForHeapStart(); // 順に画面を登録する。
	for ( int32 Index = 0; Index < BufferCount; ++Index )
	{
		if ( FAILED( SwapChain->GetBuffer( Index, IID_PPV_ARGS( &Buffers[Index] ) ) ) )
		{
			return FailInitialization();
		}
		Device->CreateRenderTargetView( Buffers[Index].Get(), nullptr, Rtv );
		Rtv.ptr += RtvStride;
	}
	if ( !Descriptors.Initialize( Device.Get(), DescriptorCount ) || !Textures.Initialize( Device.Get(), Queue.Get(), Descriptors ) || !BufferResources.Initialize( Device.Get(), Queue.Get() ) )
	{
		return FailInitialization();
	}
	if ( !ComputePipelines.Initialize( Device.Get() ) || !GraphicsPipelines.Initialize( Device.Get() ) || !Timestamps.Initialize( Device.Get(), Queue.Get() ) || !UploadArena.Initialize( Device.Get(), Queue.Get() ) || !ComputeQueue.Initialize( Device.Get(), Queue.Get(), BufferResources, ComputePipelines, &Textures, &Timestamps ) )
	{
		return FailInitialization();
	}
	this->Width = Width;
	this->Height = Height;
	bLogFrameTimings = NCommandLine::HasFlag( "dx12-frame-timings" ); // 確認用: フレーム時間の内訳を、一定フレームごとにログへ出す
	SegmentTimer.Initialize( &Timestamps, NCommandLine::HasFlag( "dx12-gpu-timings" ) ); // 確認用: 主画面の区間と描画先パスごとのGPU時間を、ログとプロファイラーへ出す
	bInitialized = true;
	return true;
}

CDX12Device::FFrameTimings CDX12Device::GetLastFrameTimings() const
{
	return LastTimings;
}

void CDX12Device::AccumulateFrameTimings( const FFrameTimings &Frame )
{
	LastTimings = Frame;
	if ( !bLogFrameTimings )
	{
		return;
	}
	SumTimings.IntervalMs += Frame.IntervalMs;
	SumTimings.GPUWaitMs += Frame.GPUWaitMs;
	SumTimings.OpenMs += Frame.OpenMs;
	SumTimings.PresentMs += Frame.PresentMs;
	MaxTimings.IntervalMs = NMath::Max( MaxTimings.IntervalMs, Frame.IntervalMs );
	MaxTimings.GPUWaitMs = NMath::Max( MaxTimings.GPUWaitMs, Frame.GPUWaitMs );
	MaxTimings.OpenMs = NMath::Max( MaxTimings.OpenMs, Frame.OpenMs );
	MaxTimings.PresentMs = NMath::Max( MaxTimings.PresentMs, Frame.PresentMs );
	if ( ++TimingFrameCount >= TimingLogFrames )
	{
		const float64 Count = static_cast<float64>( TimingFrameCount );
		LOG( LogDX12, Log, "DX12 frame timings (%d frames, ms, average / max): interval %.2f / %.2f, gpu wait %.2f / %.2f, open %.2f / %.2f, present %.2f / %.2f", TimingFrameCount, SumTimings.IntervalMs / Count, MaxTimings.IntervalMs, SumTimings.GPUWaitMs / Count, MaxTimings.GPUWaitMs, SumTimings.OpenMs / Count, MaxTimings.OpenMs, SumTimings.PresentMs / Count, MaxTimings.PresentMs );
		SumTimings = FFrameTimings();
		MaxTimings = FFrameTimings();
		TimingFrameCount = 0;
	}
}

bool CDX12Device::Wait( uint32 TimeoutMs )
{
	const bool bCompleted = Completion.Wait( TimeoutMs );
	if ( bCompleted )
	{
		Timestamps.CollectCompleted();
		UploadArena.CollectCompleted();
	}
	return bCompleted;
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
	ReleaseOwnedResources();
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

void CDX12Device::ReleaseOwnedResources()
{
	GameReadback.ResetAfterGPUIdle();
	UIReadback.ResetAfterGPUIdle();
	for ( auto &Buffer : Buffers )
	{
		Buffer.Reset();
	}
	Commands.Reset();
	Allocator.Reset();
	SwapChain.Reset();
	ComputeQueue.ResetAfterGPUIdle();
	UploadArena.ResetAfterGPUIdle();
	Timestamps.ResetAfterGPUIdle();
	SegmentTimer.Reset();
	ComputePipelines.ResetAfterGPUIdle();
	GraphicsPipelines.ResetAfterGPUIdle();
	Textures.ResetAfterGPUIdle();
	BufferResources.ResetAfterGPUIdle();
	Descriptors.Reset();
	RtvHeap.Reset();
	Completion.ResetAfterGPUIdle();
	Queue.Reset();
	Device.Reset();
	BufferIndex = RtvStride = 0;
	CurrentTimings = LastTimings = SumTimings = MaxTimings = FFrameTimings();
	FrameBeginTicks = 0;
	TimingFrameCount = 0;
	bLogFrameTimings = false;
	MainTimestampBatchId = 0;
	CurrentRtv = {};
	bRecording = false;
	bSegmentSubmittedThisFrame = false;
	bPresentPending = false;
	bCaptureGame = bCaptureUI = bGameCopied = bUICopied = bFrameSubmitted = false;
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
	if ( !bInitialized || bClosing || bRecording || bPresentPending || Index >= BufferCount || !Buffers[Index] )
	{
		return false;
	}
	const int64 BeginTicks = NPlatformTime::GetTicks(); // フレームの開始時刻（前のフレームの GPU を待つ前）
	const bool bWaited = Wait();
	const int64 WaitedTicks = NPlatformTime::GetTicks();
	if ( !bWaited || !Timestamps.CollectCompleted() || FAILED( Allocator->Reset() ) || FAILED( Commands->Reset( Allocator.Get(), nullptr ) ) )
	{
		return false;
	}
	if ( FrameBeginTicks != 0 ) // 前のフレームが終わった（この開始で、間隔が決まる）
	{
		CurrentTimings.IntervalMs = NPlatformTime::TicksToMs( BeginTicks - FrameBeginTicks );
		AccumulateFrameTimings( CurrentTimings );
	}
	CurrentTimings = FFrameTimings();
	CurrentTimings.GPUWaitMs = NPlatformTime::TicksToMs( WaitedTicks - BeginTicks );
	FrameBeginTicks = BeginTicks;
	ComputeQueue.CollectAfterGPUIdle();
	ComputePipelines.CollectAfterGPUIdle();
	GraphicsPipelines.CollectAfterGPUIdle();
	Textures.CollectAfterGPUIdle(); // 記録済みの描画が残らない時点でだけ旧版を戻す。
	BufferResources.CollectAfterGPUIdle();
	UploadArena.CollectCompleted();
	const uint64 CreatedBatch = Timestamps.BeginBatch();
	if ( CreatedBatch == 0 )
	{
		return false;
	}
	MainTimestampBatchId = CreatedBatch;
	SegmentTimer.NotifyFrame();
	SegmentTimer.OpenSegment( CDX12GpuSegmentTimer::ESegment::Main, Commands.Get(), CreatedBatch, CDX12GpuSegmentTimer::FLabel() );
	bCaptureGame = bCaptureUI = bGameCopied = bUICopied = false;
	bFrameSubmitted = false;
	bSegmentSubmittedThisFrame = false;
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
	ID3D12DescriptorHeap *Heaps[] = { Descriptors.GetHeap() }; // 画像を描くための記述子。
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

bool CDX12Device::SubmitFrame( uint64 UploadTicket )
{
	if ( !bInitialized || bClosing || !bRecording )
	{
		ResolvePendingUploadTicket( UploadTicket, false );
		return false;
	}
	SegmentTimer.CloseSegment( CDX12GpuSegmentTimer::ESegment::Main, Commands.Get(), MainTimestampBatchId );
	if ( MainTimestampBatchId != 0 )
	{
		Timestamps.AbortUnclosedInBatch( MainTimestampBatchId );
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
		if ( MainTimestampBatchId != 0 )
		{
			Timestamps.AbortBatch( MainTimestampBatchId );
			MainTimestampBatchId = 0;
		}
		ResolvePendingUploadTicket( UploadTicket, false );
		return false;
	}
	ID3D12CommandList *Lists[] = { Commands.Get() }; // 今フレームを一度だけ送る。
	Queue->ExecuteCommandLists( 1, Lists );
	if ( FrameBeginTicks != 0 ) // 提出までの時間（GPU を待った時間は、別に数える）
	{
		CurrentTimings.OpenMs = NMath::Max( 0.0, NPlatformTime::TicksToMs( NPlatformTime::GetTicks() - FrameBeginTicks ) - CurrentTimings.GPUWaitMs );
	}
	const uint64 BatchToCommit = MainTimestampBatchId;
	MainTimestampBatchId = 0;
	const bool bTimestampsCommitted = BatchToCommit == 0 || Timestamps.CommitBatch( BatchToCommit );
	const bool bUploadCommitted = UploadTicket == 0 || UploadArena.CommitTicket( UploadTicket );
	bSegmentSubmittedThisFrame = false;
	if ( !bTimestampsCommitted || !bUploadCommitted )
	{
		return false;
	}
	bFrameSubmitted = true;
	bPresentPending = true;
	return true;
}

bool CDX12Device::Resize( int32 NewWidth, int32 NewHeight )
{
	if ( !bInitialized || bClosing || bRecording || bPresentPending || !SwapChain || !Device || !RtvHeap || NewWidth <= 0 || NewHeight <= 0 || NewWidth > 16384 || NewHeight > 16384 || FAILED( Device->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	if ( !Wait() )
	{
		return false;
	}
	ComputeQueue.CollectAfterGPUIdle();
	ComputePipelines.CollectAfterGPUIdle();
	GraphicsPipelines.CollectAfterGPUIdle();
	Textures.CollectAfterGPUIdle();
	BufferResources.CollectAfterGPUIdle();
	GameReadback.ResetAfterGPUIdle();
	UIReadback.ResetAfterGPUIdle();
	if ( Width == NewWidth && Height == NewHeight && Buffers[0] && Buffers[1] )
	{
		BufferIndex = SwapChain->GetCurrentBackBufferIndex();
		CurrentRtv = RtvHeap->GetCPUDescriptorHandleForHeapStart();
		CurrentRtv.ptr += BufferIndex * RtvStride;
		bCaptureGame = bCaptureUI = bGameCopied = bUICopied = bFrameSubmitted = false;
		return true;
	}
	for ( int32 Index = 0; Index < BufferCount; ++Index )
	{
		Buffers[Index].Reset();
	}
	if ( FAILED( SwapChain->ResizeBuffers( BufferCount, static_cast<UINT>( NewWidth ), static_cast<UINT>( NewHeight ), DXGI_FORMAT_R8G8B8A8_UNORM, 0 ) ) )
	{
		bClosing = true;
		bGPUStopped = true;
		ReleaseOwnedResources();
		return false;
	}
	D3D12_CPU_DESCRIPTOR_HANDLE Rtv = RtvHeap->GetCPUDescriptorHandleForHeapStart(); // 新しい寸法のバックバッファRTVを貼り直す。
	for ( int32 Index = 0; Index < BufferCount; ++Index )
	{
		if ( FAILED( SwapChain->GetBuffer( Index, IID_PPV_ARGS( &Buffers[Index] ) ) ) )
		{
			bClosing = true;
			bGPUStopped = true;
			ReleaseOwnedResources();
			return false;
		}
		Device->CreateRenderTargetView( Buffers[Index].Get(), nullptr, Rtv );
		Rtv.ptr += RtvStride;
	}
	Width = NewWidth;
	Height = NewHeight;
	BufferIndex = SwapChain->GetCurrentBackBufferIndex();
	CurrentRtv = RtvHeap->GetCPUDescriptorHandleForHeapStart();
	CurrentRtv.ptr += BufferIndex * RtvStride;
	bCaptureGame = bCaptureUI = bGameCopied = bUICopied = bFrameSubmitted = false;
	return true;
}

bool CDX12Device::SubmitFrameSegment()
{
	if ( !IsRecording() )
	{
		return false;
	}
	SegmentTimer.CloseSegment( CDX12GpuSegmentTimer::ESegment::Main, Commands.Get(), MainTimestampBatchId );
	if ( MainTimestampBatchId != 0 && Timestamps.HasActiveRecording( MainTimestampBatchId ) )
	{
		return false;
	}
	bRecording = false;
	if ( FAILED( Commands->Close() ) )
	{
		if ( MainTimestampBatchId != 0 )
		{
			Timestamps.AbortBatch( MainTimestampBatchId );
			MainTimestampBatchId = 0;
		}
		return false;
	}
	ID3D12CommandList *Lists[] = { Commands.Get() }; // 後続の別パスより前に、ここまでの描画を確定する。
	Queue->ExecuteCommandLists( 1, Lists );
	bSegmentSubmittedThisFrame = true;
	const uint64 BatchToCommit = MainTimestampBatchId;
	MainTimestampBatchId = 0;
	// allocatorは次のフレーム冒頭でGPU完了を待つまでResetしない。色と深度の内容も保つ。
	if ( ( BatchToCommit != 0 && !Timestamps.CommitBatch( BatchToCommit ) ) || FAILED( Device->GetDeviceRemovedReason() ) || FAILED( Commands->Reset( Allocator.Get(), nullptr ) ) )
	{
		return false;
	}
	const uint64 NextBatch = Timestamps.BeginBatch();
	if ( NextBatch == 0 )
	{
		return false;
	}
	MainTimestampBatchId = NextBatch;
	SegmentTimer.OpenSegment( CDX12GpuSegmentTimer::ESegment::Main, Commands.Get(), NextBatch, CDX12GpuSegmentTimer::FLabel() );
	bRecording = true;
	return true;
}

void CDX12Device::ResolvePendingUploadTicket( uint64 UploadTicket, bool bExecuted )
{
	if ( UploadTicket == 0 )
	{
		return;
	}
	if ( bExecuted || bSegmentSubmittedThisFrame )
	{
		UploadArena.CommitTicket( UploadTicket );
	}
	else
	{
		UploadArena.AbortTicket( UploadTicket );
	}
	bSegmentSubmittedThisFrame = false;
}

CDX12UploadArena *CDX12Device::GetUploadArena()
{
	return bInitialized && !bClosing ? &UploadArena : nullptr;
}

bool CDX12Device::PresentFrame( bool bVSync )
{
	if ( !bInitialized || bClosing || bRecording || !bPresentPending )
	{
		return false;
	}
	bPresentPending = false;
	const int64 PresentTicks = NPlatformTime::GetTicks();
	const bool bPresented = SUCCEEDED( SwapChain->Present( bVSync ? 1 : 0, 0 ) );
	CurrentTimings.PresentMs = NPlatformTime::TicksToMs( NPlatformTime::GetTicks() - PresentTicks );
	return bPresented;
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

bool CDX12Device::RecordFrameColorCopy( int32 X0, int32 Y0, int32 X1, int32 Y1, ID3D12Resource *Dest )
{
	return bInitialized && !bClosing && bRecording && Buffers[BufferIndex] && NDX12TextureCopy::RecordRegionCopy( Commands.Get(), Buffers[BufferIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, Dest, X0, Y0, X1, Y1 );
}

bool CDX12Device::ReadFrameCapture( EFrameCaptureStage Stage, TArray<FColor32> &Out )
{
	if ( !bInitialized || bClosing || bRecording || !bFrameSubmitted || ( Stage != EFrameCaptureStage::Game && Stage != EFrameCaptureStage::WithDebugUI ) || !( Stage == EFrameCaptureStage::Game ? bGameCopied : bUICopied ) || !Wait() )
	{
		return false;
	}
	return ( Stage == EFrameCaptureStage::Game ? GameReadback : UIReadback ).ReadAfterGPUIdle( Out, true );
}

bool CDX12Device::InitializeCaptureTarget( CDX12CaptureTarget &Target )
{
	return bInitialized && !bClosing && Target.Initialize( Device.Get(), Queue.Get() );
}

bool CDX12Device::QueryVideoMemory( int64 &OutUsedBytes, int64 &OutBudgetBytes ) const
{
	if ( !bInitialized || bClosing || !Device )
	{
		return false;
	}
	Microsoft::WRL::ComPtr<IDXGIFactory4> Factory; // このデバイスのアダプターを、LUIDで引く。
	Microsoft::WRL::ComPtr<IDXGIAdapter3> Adapter;
	DXGI_QUERY_VIDEO_MEMORY_INFO Info = {};
	if ( FAILED( CreateDXGIFactory1( IID_PPV_ARGS( &Factory ) ) ) || FAILED( Factory->EnumAdapterByLuid( Device->GetAdapterLuid(), IID_PPV_ARGS( &Adapter ) ) ) || FAILED( Adapter->QueryVideoMemoryInfo( 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &Info ) ) )
	{
		return false;
	}
	OutUsedBytes = static_cast<int64>( Info.CurrentUsage );
	OutBudgetBytes = static_cast<int64>( Info.Budget );
	return true;
}

ID3D12Device *CDX12Device::GetNativeDevice() { return bInitialized && !bClosing ? Device.Get() : nullptr; }
ID3D12CommandQueue *CDX12Device::GetNativeQueue() { return bInitialized && !bClosing ? Queue.Get() : nullptr; }
CDX12DescriptorPool *CDX12Device::GetDescriptorPool() { return bInitialized && !bClosing ? &Descriptors : nullptr; }
ID3D12GraphicsCommandList *CDX12Device::GetFrameCommands() { return IsRecording() ? Commands.Get() : nullptr; }
D3D12_CPU_DESCRIPTOR_HANDLE CDX12Device::GetFrameColorView() const { return IsRecording() ? CurrentRtv : D3D12_CPU_DESCRIPTOR_HANDLE{}; }
int32 CDX12Device::GetWidth() const { return Width; }
int32 CDX12Device::GetHeight() const { return Height; }

ID3D12DescriptorHeap *CDX12Device::GetImageDescriptorHeap()
{
	return bInitialized && !bClosing ? Descriptors.GetHeap() : nullptr;
}

bool CDX12Device::IsDeviceRemoved() const { return Device && FAILED( Device->GetDeviceRemovedReason() ); }

bool CDX12Device::IsRecording() const { return bInitialized && !bClosing && bRecording; }

bool CDX12Device::IsInitialized() const { return bInitialized; }

CDX12TextureStore *CDX12Device::GetTextureResources()
{
	return bInitialized && !bClosing ? &Textures : nullptr;
}

CDX12BufferStore *CDX12Device::GetBufferResources()
{
	return bInitialized && !bClosing ? &BufferResources : nullptr;
}

IComputePipelineResources *CDX12Device::GetComputePipelineResources()
{
	return bInitialized && !bClosing ? &ComputePipelines : nullptr;
}

IGraphicsPipelineResources *CDX12Device::GetGraphicsPipelineResources()
{
	return bInitialized && !bClosing ? &GraphicsPipelines : nullptr;
}

CDX12GraphicsPipelineStore *CDX12Device::GetGraphicsPipelineStore()
{
	return bInitialized && !bClosing ? &GraphicsPipelines : nullptr;
}

IComputeQueue *CDX12Device::GetComputeQueue()
{
	return bInitialized && !bClosing ? &ComputeQueue : nullptr;
}

IGpuTimestampResources *CDX12Device::GetTimestampResources()
{
	return bInitialized && !bClosing ? &Timestamps : nullptr;
}

CDX12TimestampStore *CDX12Device::GetTimestampStore()
{
	return bInitialized && !bClosing ? &Timestamps : nullptr;
}

CDX12GpuSegmentTimer *CDX12Device::GetSegmentTimer()
{
	return bInitialized && !bClosing ? &SegmentTimer : nullptr;
}

bool CDX12Device::BeginMainTimestamp( FGpuTimestampHandle Query )
{
	return IsRecording() && MainTimestampBatchId != 0 && Timestamps.BeginRecording( Commands.Get(), MainTimestampBatchId, Query );
}

bool CDX12Device::EndMainTimestamp( FGpuTimestampHandle Query )
{
	return IsRecording() && MainTimestampBatchId != 0 && Timestamps.EndRecording( Commands.Get(), MainTimestampBatchId, Query );
}
