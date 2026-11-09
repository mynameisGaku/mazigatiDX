// SPDX-License-Identifier: MIT
#include "DX12ComputeQueue.h"
#include "Library/Core/Render/RenderResourceSerial.h"
#include "Library/Core/Types/Numeric.h"

FDX12ComputeQueue::~FDX12ComputeQueue()
{
	ResetAfterGPUIdle();
}

bool FDX12ComputeQueue::Initialize( ID3D12Device* InDevice, ID3D12CommandQueue* InQueue, FDX12BufferStore& InBuffers, FDX12ComputePipelineStore& InPipelines )
{
	if ( Device || !InDevice || !InQueue || InQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT || !InBuffers.UsesDeviceAndQueue( InDevice, InQueue ) || !InPipelines.UsesDevice( InDevice ) )
	{
		return false;
	}
	Device = InDevice;
	Queue = InQueue;
	Buffers = &InBuffers;
	Pipelines = &InPipelines;
	if ( FAILED( Device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &Fence ) ) ) )
	{
		ResetAfterGPUIdle();
		return false;
	}
	FenceEvent = CreateEventW( nullptr, FALSE, FALSE, nullptr );
	QueueSerial = NRenderResourceSerial::Allocate();
	bHealthy = FenceEvent != nullptr && QueueSerial != 0;
	if ( !bHealthy )
	{
		ResetAfterGPUIdle();
	}
	return bHealthy;
}

void FDX12ComputeQueue::CollectAfterGPUIdle()
{
	Pending.Reset();
	Notifications.Reset();
}

void FDX12ComputeQueue::ResetAfterGPUIdle()
{
	Pending.Reset();
	Notifications.Reset();
	Fence.Reset();
	Queue.Reset();
	Device.Reset();
	Buffers = nullptr;
	Pipelines = nullptr;
	QueueSerial = LastSubmitted = NextValue = 0;
	bHealthy = false;
	if ( FenceEvent )
	{
		CloseHandle( FenceEvent );
		FenceEvent = nullptr;
	}
}

bool FDX12ComputeQueue::CollectCompleted()
{
	if ( !bHealthy )
	{
		return false;
	}
	const uint64 Completed = Fence->GetCompletedValue(); // 最大値はデバイス喪失の通知。
	if ( Completed == TNumericLimits<uint64>::Max() || FAILED( Device->GetDeviceRemovedReason() ) )
	{
		bHealthy = false;
		return false;
	}
	Pending.RemoveAll( [Completed]( const FSubmissionRecord& Record ) { return Record.Value <= Completed; } );
	Notifications.RemoveAll( [Completed]( uint64 Value ) { return Value <= Completed; } );
	return true;
}

bool FDX12ComputeQueue::IsKnown( FComputeSubmission Submission ) const
{
	return bHealthy && Submission.Queue == QueueSerial && Submission.Value != 0 && Submission.Value <= LastSubmitted;
}

bool FDX12ComputeQueue::Record( TArrayView<const FComputeDispatch> Dispatches, FSubmissionRecord& Out )
{
	if ( !Dispatches.GetData() || Dispatches.Num() <= 0 || Dispatches.Num() > MaxDispatches || !Buffers->UsesDeviceAndQueue( Device.Get(), Queue.Get() ) )
	{
		return false;
	}
	FSubmissionRecord Created; // 最後の命令も検査できるまでGPUへ渡さない。
	int32 DescriptorCount = 0; // 提出専用heapに必要な記述子数。
	for ( const FComputeDispatch& Dispatch : Dispatches )
	{
		FDispatchRecord Copy; // 呼出側の配列と独立させる実体の参照。
		if ( Dispatch.GroupsX == 0 || Dispatch.GroupsX > 65535 || Dispatch.GroupsY == 0 || Dispatch.GroupsY > 65535 || Dispatch.GroupsZ == 0 || Dispatch.GroupsZ > 65535 || !Pipelines->AcquirePipeline( Dispatch.Pipeline, Copy.Pipeline ) || !Dispatch.Buffers.GetData() || Dispatch.Buffers.Num() != Copy.Pipeline.Slots.Num() )
		{
			return false;
		}
		Copy.GroupsX = Dispatch.GroupsX;
		Copy.GroupsY = Dispatch.GroupsY;
		Copy.GroupsZ = Dispatch.GroupsZ;
		for ( int32 Index = 0; Index < Dispatch.Buffers.Num(); ++Index )
		{
			for ( int32 Earlier = 0; Earlier < Index; ++Earlier )
			{
				if ( Dispatch.Buffers[Earlier] == Dispatch.Buffers[Index] )
				{
					return false;
				}
			}
			FDX12BufferBinding Buffer; // 宣言した用途と登録番号を照合した参照。
			if ( !Buffers->AcquireComputeBuffer( Dispatch.Buffers[Index], Copy.Pipeline.Slots[Index].Access, Buffer ) )
			{
				return false;
			}
			Copy.Buffers.Add( MoveTemp( Buffer ) );
		}
		DescriptorCount += Copy.Buffers.Num();
		Created.Dispatches.Add( MoveTemp( Copy ) );
	}
	D3D12_DESCRIPTOR_HEAP_DESC Heap = {}; // UIと共有せず、提出が終わるまで保持する記述子。
	Heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	Heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	Heap.NumDescriptors = static_cast<UINT>( DescriptorCount );
	if ( FAILED( Device->CreateDescriptorHeap( &Heap, IID_PPV_ARGS( &Created.Descriptors ) ) ) || FAILED( Device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &Created.Allocator ) ) ) || FAILED( Device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, Created.Allocator.Get(), nullptr, IID_PPV_ARGS( &Created.Commands ) ) ) )
	{
		return false;
	}
	const UINT Stride = Device->GetDescriptorHandleIncrementSize( Heap.Type ); // 一つの記述子の間隔。
	D3D12_CPU_DESCRIPTOR_HANDLE Cpu = Created.Descriptors->GetCPUDescriptorHandleForHeapStart(); // 次に記録するサイズ付きview。
	D3D12_GPU_DESCRIPTOR_HANDLE Gpu = Created.Descriptors->GetGPUDescriptorHandleForHeapStart(); // 対応するGPU参照。
	ID3D12DescriptorHeap* Heaps[] = { Created.Descriptors.Get() }; // この命令列専用のheap。
	Created.Commands->SetDescriptorHeaps( 1, Heaps );
	for ( const FDispatchRecord& Dispatch : Created.Dispatches )
	{
		Created.Commands->SetPipelineState( Dispatch.Pipeline.Pipeline.Get() );
		Created.Commands->SetComputeRootSignature( Dispatch.Pipeline.Root.Get() );
		for ( int32 Index = 0; Index < Dispatch.Buffers.Num(); ++Index )
		{
			const FDX12BufferBinding& Buffer = Dispatch.Buffers[Index]; // このslotに固定したバッファ。
			const bool bWrite = Dispatch.Pipeline.Slots[Index].Access == ERenderBufferShaderAccess::ReadWrite; // 書き込み用途に合わせて遷移する。
			if ( bWrite )
			{
				D3D12_UNORDERED_ACCESS_VIEW_DESC View = {}; // 範囲を持つRAWのUAV。
				View.Format = DXGI_FORMAT_R32_TYPELESS;
				View.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
				View.Buffer.NumElements = static_cast<UINT>( Buffer.Size / 4 );
				View.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
				Device->CreateUnorderedAccessView( Buffer.Resource.Get(), nullptr, &View, Cpu );
			}
			else
			{
				D3D12_SHADER_RESOURCE_VIEW_DESC View = {}; // 範囲を持つRAWのSRV。
				View.Format = DXGI_FORMAT_R32_TYPELESS;
				View.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
				View.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				View.Buffer.NumElements = static_cast<UINT>( Buffer.Size / 4 );
				View.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
				Device->CreateShaderResourceView( Buffer.Resource.Get(), &View, Cpu );
			}
			D3D12_RESOURCE_BARRIER Barrier = {}; // 一命令の入口で用途に合わせる。
			Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			Barrier.Transition.pResource = Buffer.Resource.Get();
			Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
			Barrier.Transition.StateAfter = bWrite ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			Created.Commands->ResourceBarrier( 1, &Barrier );
			Created.Commands->SetComputeRootDescriptorTable( static_cast<UINT>( Index ), Gpu );
			Cpu.ptr += Stride;
			Gpu.ptr += Stride;
		}
		Created.Commands->Dispatch( Dispatch.GroupsX, Dispatch.GroupsY, Dispatch.GroupsZ );
		for ( int32 Index = 0; Index < Dispatch.Buffers.Num(); ++Index )
		{
			const bool bWrite = Dispatch.Pipeline.Slots[Index].Access == ERenderBufferShaderAccess::ReadWrite; // 書き込み完了を次の用途へ渡す。
			D3D12_RESOURCE_BARRIER Barrier = {}; // 同じキューの次の命令へCOMMONで引き渡す。
			if ( bWrite )
			{
				Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
				Barrier.UAV.pResource = Dispatch.Buffers[Index].Resource.Get();
				Created.Commands->ResourceBarrier( 1, &Barrier );
			}
			Barrier = {};
			Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			Barrier.Transition.pResource = Dispatch.Buffers[Index].Resource.Get();
			Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			Barrier.Transition.StateBefore = bWrite ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
			Created.Commands->ResourceBarrier( 1, &Barrier );
		}
	}
	if ( FAILED( Created.Commands->Close() ) )
	{
		return false;
	}
	Out = MoveTemp( Created );
	return true;
}

FComputeSubmission FDX12ComputeQueue::SubmitCompute( TArrayView<const FComputeDispatch> Dispatches )
{
	if ( !CollectCompleted() || Pending.Num() >= MaxPending || NextValue >= TNumericLimits<uint64>::Max() - 1 )
	{
		return {};
	}
	FSubmissionRecord Created; // 全命令の検証後にだけ提出する。
	if ( !Record( Dispatches, Created ) )
	{
		return {};
	}
	Created.Value = ++NextValue;
	Pending.Add( MoveTemp( Created ) );
	FSubmissionRecord& Submitted = Pending.Last(); // Signal失敗時にも実体と命令を保持する。
	ID3D12CommandList* Lists[] = { Submitted.Commands.Get() }; // この提出だけの命令列。
	Queue->ExecuteCommandLists( 1, Lists );
	if ( FAILED( Queue->Signal( Fence.Get(), Submitted.Value ) ) )
	{
		bHealthy = false;
		return {};
	}
	LastSubmitted = Submitted.Value;
	return { QueueSerial, LastSubmitted };
}

bool FDX12ComputeQueue::GetComputeCompletion( FComputeSubmission Submission, bool& OutCompleted )
{
	if ( !IsKnown( Submission ) || !CollectCompleted() )
	{
		return false;
	}
	const uint64 Completed = Fence->GetCompletedValue(); // 照会の間に起きたデバイス喪失も完了と混同しない。
	if ( Completed == TNumericLimits<uint64>::Max() )
	{
		bHealthy = false;
		return false;
	}
	OutCompleted = Completed >= Submission.Value;
	return true;
}

bool FDX12ComputeQueue::WaitCompute( FComputeSubmission Submission, uint32 TimeoutMs )
{
	bool bCompleted = false; // 不正値では既存の状態に作用しない。
	if ( TimeoutMs > 60000 || !GetComputeCompletion( Submission, bCompleted ) )
	{
		return false;
	}
	if ( bCompleted )
	{
		return true;
	}
	if ( TimeoutMs == 0 )
	{
		return false;
	}
	if ( !Notifications.Contains( Submission.Value ) && FAILED( Fence->SetEventOnCompletion( Submission.Value, FenceEvent ) ) )
	{
		bHealthy = false;
		return false;
	}
	if ( !Notifications.Contains( Submission.Value ) )
	{
		Notifications.Add( Submission.Value );
	}
	const uint64 Started = GetTickCount64(); // 過去の短い待機の通知で起きても、今回の期限まで待つ。
	for ( ;; )
	{
		const uint64 Elapsed = GetTickCount64() - Started; // 壁時計の調整に影響されない経過ミリ秒。
		if ( Elapsed >= TimeoutMs )
		{
			return GetComputeCompletion( Submission, bCompleted ) && bCompleted;
		}
		const DWORD Result = WaitForSingleObject( FenceEvent, static_cast<DWORD>( TimeoutMs - Elapsed ) ); // 残り時間だけ待つ。
		if ( Result != WAIT_OBJECT_0 )
		{
			return false;
		}
		if ( !GetComputeCompletion( Submission, bCompleted ) )
		{
			return false;
		}
		if ( bCompleted )
		{
			return true;
		}
	}
}
