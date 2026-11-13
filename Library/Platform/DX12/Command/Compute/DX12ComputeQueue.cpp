// SPDX-License-Identifier: MIT
#include "DX12ComputeQueue.h"
#include "Library/Core/Render/RenderResourceSerial.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Platform/DX12/Resource/Timestamp/DX12TimestampStore.h"

CDX12ComputeQueue::~CDX12ComputeQueue()
{
	ResetAfterGPUIdle();
}

bool CDX12ComputeQueue::Initialize( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue, CDX12BufferStore &InBuffers, CDX12ComputePipelineStore &InPipelines, CDX12TextureStore *InTextures, CDX12TimestampStore *InTimestamps )
{
	if ( Device || !InDevice || !InQueue || InQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT || !InBuffers.UsesDeviceAndQueue( InDevice, InQueue ) || !InPipelines.UsesDevice( InDevice ) || ( InTextures && !InTextures->UsesDeviceAndQueue( InDevice, InQueue ) ) || ( InTimestamps && !InTimestamps->UsesDeviceAndQueue( InDevice, InQueue ) ) )
	{
		return false;
	}
	Device = InDevice;
	Queue = InQueue;
	Buffers = &InBuffers;
	Pipelines = &InPipelines;
	Textures = InTextures;
	Timestamps = InTimestamps;
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

void CDX12ComputeQueue::CollectAfterGPUIdle()
{
	Pending.Reset();
	Notifications.Reset();
}

void CDX12ComputeQueue::ResetAfterGPUIdle()
{
	Pending.Reset();
	Notifications.Reset();
	Fence.Reset();
	Queue.Reset();
	Device.Reset();
	Buffers = nullptr;
	Pipelines = nullptr;
	Textures = nullptr;
	Timestamps = nullptr;
	QueueSerial = LastSubmitted = NextValue = 0;
	bHealthy = false;
	if ( FenceEvent )
	{
		CloseHandle( FenceEvent );
		FenceEvent = nullptr;
	}
}

bool CDX12ComputeQueue::CollectCompleted()
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

bool CDX12ComputeQueue::IsKnown( FComputeSubmission Submission ) const
{
	return bHealthy && Submission.Queue == QueueSerial && Submission.Value != 0 && Submission.Value <= LastSubmitted;
}

bool CDX12ComputeQueue::Record( TArrayView<const FComputeDispatch> Dispatches, FSubmissionRecord &Out, const uint32 *TimestampBeginSlot )
{
	if ( !Dispatches.GetData() || Dispatches.Num() <= 0 || Dispatches.Num() > MaxDispatches || !Buffers->UsesDeviceAndQueue( Device.Get(), Queue.Get() ) || !Pipelines->UsesDevice( Device.Get() ) || ( Textures && !Textures->UsesDeviceAndQueue( Device.Get(), Queue.Get() ) ) || ( TimestampBeginSlot && ( !Timestamps || !Timestamps->UsesDeviceAndQueue( Device.Get(), Queue.Get() ) ) ) )
	{
		return false;
	}
	FSubmissionRecord Created; // 最後の命令も検査できるまでGPUへ渡さない。
	int32 DescriptorCount = 0; // 提出専用heapに必要な記述子数。
	for ( const FComputeDispatch &Dispatch : Dispatches )
	{
		FDispatchRecord Copy; // 呼出側の配列と独立させる実体の参照。
		if ( Dispatch.GroupsX == 0 || Dispatch.GroupsX > 65535 || Dispatch.GroupsY == 0 || Dispatch.GroupsY > 65535 || Dispatch.GroupsZ == 0 || Dispatch.GroupsZ > 65535 || !Pipelines->AcquirePipeline( Dispatch.Pipeline, Copy.Pipeline ) || ( Dispatch.Buffers.Num() != 0 && !Dispatch.Buffers.GetData() ) || Dispatch.Buffers.Num() != Copy.Pipeline.Slots.Num() || Dispatch.Textures.Num() != Copy.Pipeline.TextureSlots.Num() || ( Dispatch.Textures.Num() != 0 && ( !Textures || !Dispatch.Textures.GetData() ) ) )
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
			const FComputeBufferSlot &Slot = Copy.Pipeline.Slots[Index]; // 配置に合わないサイズは記述子を作る前に拒否する。
			if ( ( Slot.Layout == EComputeBufferLayout::Structured && Buffer.Size % Slot.ElementStride != 0 ) || ( Slot.Layout == EComputeBufferLayout::Constant && ( Buffer.Size % 256 != 0 || Buffer.Size > 65536 || Buffer.Size < Copy.Pipeline.MinimumBytes[Index] ) ) )
			{
				return false;
			}
			Copy.Buffers.Add( MoveTemp( Buffer ) );
		}
		for ( int32 Index = 0; Index < Dispatch.Textures.Num(); ++Index )
		{
			for ( int32 Earlier = 0; Earlier < Index; ++Earlier )
			{
				if ( Dispatch.Textures[Earlier] == Dispatch.Textures[Index] )
				{
					return false;
				}
			}
			FDX12TextureBinding Texture; // 用途・形式と版を確定してから命令全体へ保持する。
			if ( !Textures->AcquireComputeTexture( Dispatch.Textures[Index], Copy.Pipeline.TextureSlots[Index].Access, Texture, Copy.Pipeline.TextureSlots[Index].Format ) )
			{
				return false;
			}
			Copy.Textures.Add( MoveTemp( Texture ) );
		}
		DescriptorCount += Copy.Buffers.Num() + Copy.Textures.Num();
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
	ID3D12DescriptorHeap *Heaps[] = { Created.Descriptors.Get() }; // この命令列専用のheap。
	Created.Commands->SetDescriptorHeaps( 1, Heaps );
	if ( TimestampBeginSlot )
	{
		Timestamps->RecordBeginAtSlot( Created.Commands.Get(), *TimestampBeginSlot );
	}
	for ( const FDispatchRecord &Dispatch : Created.Dispatches )
	{
		Created.Commands->SetPipelineState( Dispatch.Pipeline.Pipeline.Get() );
		Created.Commands->SetComputeRootSignature( Dispatch.Pipeline.Root.Get() );
		for ( int32 Index = 0; Index < Dispatch.Buffers.Num(); ++Index )
		{
			const FDX12BufferBinding &Buffer = Dispatch.Buffers[Index]; // このslotに固定したバッファ。
			const FComputeBufferSlot &Slot = Dispatch.Pipeline.Slots[Index]; // プログラム作成時に確定した用途と配置。
			const bool bConstant = Slot.Layout == EComputeBufferLayout::Constant; // 定数はCBVと専用の読み取り状態へ対応する。
			const bool bStructured = Slot.Layout == EComputeBufferLayout::Structured; // 構造体配列には型付きformatを指定しない。
			const bool bWrite = Slot.Access == ERenderBufferShaderAccess::ReadWrite; // 書き込み用途に合わせて遷移する。
			if ( bConstant )
			{
				D3D12_CONSTANT_BUFFER_VIEW_DESC View = {}; // 整列と最小範囲の検査が済んだ定数全体。
				View.BufferLocation = Buffer.Resource->GetGPUVirtualAddress();
				View.SizeInBytes = static_cast<UINT>( Buffer.Size );
				Device->CreateConstantBufferView( &View, Cpu );
			}
			else if ( bWrite )
			{
				D3D12_UNORDERED_ACCESS_VIEW_DESC View = {}; // 範囲を持つRAWまたは構造体配列のUAV。
				View.Format = bStructured ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
				View.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
				View.Buffer.NumElements = static_cast<UINT>( Buffer.Size / ( bStructured ? Slot.ElementStride : 4 ) );
				View.Buffer.StructureByteStride = Slot.ElementStride;
				View.Buffer.Flags = bStructured ? D3D12_BUFFER_UAV_FLAG_NONE : D3D12_BUFFER_UAV_FLAG_RAW;
				Device->CreateUnorderedAccessView( Buffer.Resource.Get(), nullptr, &View, Cpu );
			}
			else
			{
				D3D12_SHADER_RESOURCE_VIEW_DESC View = {}; // 範囲を持つRAWまたは構造体配列のSRV。
				View.Format = bStructured ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_R32_TYPELESS;
				View.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
				View.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				View.Buffer.NumElements = static_cast<UINT>( Buffer.Size / ( bStructured ? Slot.ElementStride : 4 ) );
				View.Buffer.StructureByteStride = Slot.ElementStride;
				View.Buffer.Flags = bStructured ? D3D12_BUFFER_SRV_FLAG_NONE : D3D12_BUFFER_SRV_FLAG_RAW;
				Device->CreateShaderResourceView( Buffer.Resource.Get(), &View, Cpu );
			}
			D3D12_RESOURCE_BARRIER Barrier = {}; // 一命令の入口で用途に合わせる。
			Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			Barrier.Transition.pResource = Buffer.Resource.Get();
			Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
			Barrier.Transition.StateAfter = bConstant ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER : bWrite ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
																												 : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			Created.Commands->ResourceBarrier( 1, &Barrier );
			Created.Commands->SetComputeRootDescriptorTable( static_cast<UINT>( Index ), Gpu );
			Cpu.ptr += Stride;
			Gpu.ptr += Stride;
		}
		for ( int32 Index = 0; Index < Dispatch.Textures.Num(); ++Index )
		{
			const FDX12TextureBinding &Texture = Dispatch.Textures[Index]; // 提出が保持する画像の版。
			const bool bWrite = Dispatch.Pipeline.TextureSlots[Index].Access == EComputeTextureAccess::ReadWrite; // 書き込みだけUAV状態へ移す。
			if ( bWrite )
			{
				D3D12_UNORDERED_ACCESS_VIEW_DESC View = {}; // RGBA8またはRGBA16Fの二次元UAV。
				View.Format = Texture.Format == EComputeTextureFormat::RGBA16F ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
				View.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
				Device->CreateUnorderedAccessView( Texture.Resource.Get(), nullptr, &View, Cpu );
				D3D12_RESOURCE_BARRIER Barrier = {}; // 描画とcompute読み取りを許す共通の状態から入る。
				Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				Barrier.Transition.pResource = Texture.Resource.Get();
				Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
				Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
				Created.Commands->ResourceBarrier( 1, &Barrier );
			}
			else
			{
				D3D12_SHADER_RESOURCE_VIEW_DESC View = {}; // 通常描画用とは独立した、この提出専用のSRV。
				View.Format = Texture.Format == EComputeTextureFormat::R32Float ? DXGI_FORMAT_R32_FLOAT : ( Texture.Format == EComputeTextureFormat::RGBA16F ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM );
				View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				View.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				View.Texture2D.MipLevels = 1;
				Device->CreateShaderResourceView( Texture.Resource.Get(), &View, Cpu );
			}
			Created.Commands->SetComputeRootDescriptorTable( static_cast<UINT>( Dispatch.Buffers.Num() + Index ), Gpu );
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
			Barrier.Transition.StateBefore = Dispatch.Pipeline.Slots[Index].Layout == EComputeBufferLayout::Constant ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER : bWrite ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
																																												: D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
			Created.Commands->ResourceBarrier( 1, &Barrier );
		}
		for ( int32 Index = 0; Index < Dispatch.Textures.Num(); ++Index )
		{
			if ( Dispatch.Pipeline.TextureSlots[Index].Access != EComputeTextureAccess::ReadWrite )
			{
				continue;
			}
			D3D12_RESOURCE_BARRIER Barrier = {}; // 書き込み完了を次のcompute・描画へ渡す。
			Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			Barrier.UAV.pResource = Dispatch.Textures[Index].Resource.Get();
			Created.Commands->ResourceBarrier( 1, &Barrier );
			Barrier = {};
			Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			Barrier.Transition.pResource = Dispatch.Textures[Index].Resource.Get();
			Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			Created.Commands->ResourceBarrier( 1, &Barrier );
		}
	}
	if ( TimestampBeginSlot )
	{
		Timestamps->RecordEndAndResolveAtSlot( Created.Commands.Get(), *TimestampBeginSlot );
	}
	if ( FAILED( Created.Commands->Close() ) )
	{
		return false;
	}
	Out = MoveTemp( Created );
	return true;
}

FComputeSubmission CDX12ComputeQueue::SubmitComputeInternal( TArrayView<const FComputeDispatch> Dispatches, FGpuTimestampHandle Timestamp )
{
	if ( !CollectCompleted() || Pending.Num() >= MaxPending || NextValue >= TNumericLimits<uint64>::Max() - 1 )
	{
		return {};
	}
	uint32 TimestampBeginSlot = 0; // 先行検査に通過した場合だけ計測ペアへ記録する。
	const bool bWithTimestamp = Timestamp.IsValid();
	if ( bWithTimestamp )
	{
		if ( !Timestamps || !Timestamps->UsesDeviceAndQueue( Device.Get(), Queue.Get() ) || !Timestamps->TryPrepareComputeSlot( Timestamp, TimestampBeginSlot ) )
		{
			return {};
		}
	}
	FSubmissionRecord Created; // 全命令の検証後にだけ提出する。
	if ( !Record( Dispatches, Created, bWithTimestamp ? &TimestampBeginSlot : nullptr ) )
	{
		return {};
	}
	Created.Value = ++NextValue;
	Pending.Add( MoveTemp( Created ) );
	FSubmissionRecord &Submitted = Pending.Last(); // Signal失敗時にも実体と命令を保持する。
	ID3D12CommandList *Lists[] = { Submitted.Commands.Get() }; // この提出だけの命令列。
	Queue->ExecuteCommandLists( 1, Lists );
	if ( FAILED( Queue->Signal( Fence.Get(), Submitted.Value ) ) )
	{
		bHealthy = false;
		return {};
	}
	if ( bWithTimestamp && !Timestamps->CommitComputeSlot( Timestamp, TimestampBeginSlot ) )
	{
		bHealthy = false;
		return {};
	}
	LastSubmitted = Submitted.Value;
	return { QueueSerial, LastSubmitted };
}

FComputeSubmission CDX12ComputeQueue::SubmitCompute( TArrayView<const FComputeDispatch> Dispatches )
{
	return SubmitComputeInternal( Dispatches, FGpuTimestampHandle() );
}

FComputeSubmission CDX12ComputeQueue::SubmitComputeWithTimestamp( TArrayView<const FComputeDispatch> Dispatches, FGpuTimestampHandle Timestamp )
{
	if ( !Timestamp.IsValid() )
	{
		return {};
	}
	return SubmitComputeInternal( Dispatches, Timestamp );
}

bool CDX12ComputeQueue::GetComputeCompletion( FComputeSubmission Submission, bool &OutCompleted )
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

bool CDX12ComputeQueue::WaitCompute( FComputeSubmission Submission, uint32 TimeoutMs )
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
