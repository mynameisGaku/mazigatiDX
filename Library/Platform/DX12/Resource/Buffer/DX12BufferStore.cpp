// SPDX-License-Identifier: MIT
#include "DX12BufferStore.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"

CDX12BufferStore::~CDX12BufferStore()
{
	ResetAfterGPUIdle();
}

bool CDX12BufferStore::Initialize( ID3D12Device* InDevice, ID3D12CommandQueue* InQueue )
{
	if ( Device || !InDevice || !InQueue || InQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT )
	{
		return false;
	}
	Microsoft::WRL::ComPtr<ID3D12Device> QueueDevice; // 異なるデバイスのキューを拒否する。
	if ( FAILED( InQueue->GetDevice( IID_PPV_ARGS( &QueueDevice ) ) ) || QueueDevice.Get() != InDevice )
	{
		return false;
	}
	Device = InDevice;
	Queue = InQueue;
	if ( FAILED( Device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &Allocator ) ) ) || FAILED( Device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.Get(), nullptr, IID_PPV_ARGS( &Commands ) ) ) || FAILED( Commands->Close() ) || FAILED( Device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &Fence ) ) ) )
	{
		ResetAfterGPUIdle();
		return false;
	}
	FenceEvent = CreateEventW( nullptr, FALSE, FALSE, nullptr );
	bHealthy = FenceEvent != nullptr;
	if ( !bHealthy )
	{
		ResetAfterGPUIdle();
	}
	return bHealthy;
}

void CDX12BufferStore::CollectAfterGPUIdle()
{
	for ( const FBufferRecord& Buffer : Retired )
	{
		ResidentBytes -= Buffer.Size;
	}
	Retired.Reset();
	PendingSource.Reset();
	PendingTarget.Reset();
}

void CDX12BufferStore::ResetAfterGPUIdle()
{
	CollectAfterGPUIdle();
	Buffers.Reset();
	ResidentBytes = 0;
	Commands.Reset();
	Allocator.Reset();
	Fence.Reset();
	Queue.Reset();
	Device.Reset();
	bHealthy = false;
	FenceValue = 0;
	if ( FenceEvent )
	{
		CloseHandle( FenceEvent );
		FenceEvent = nullptr;
	}
}

bool CDX12BufferStore::CreateResource( uint64 Size, D3D12_HEAP_TYPE HeapType, D3D12_RESOURCE_STATES State, Microsoft::WRL::ComPtr<ID3D12Resource>& Out, D3D12_RESOURCE_FLAGS Flags )
{
	D3D12_HEAP_PROPERTIES Heap = {}; // GPU・upload・readbackのいずれかの置き場。
	Heap.Type = HeapType;
	D3D12_RESOURCE_DESC Desc = {}; // 生のバイト列としてコピーする一次元バッファ。
	Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	Desc.Width = Size;
	Desc.Height = Desc.DepthOrArraySize = Desc.MipLevels = Desc.SampleDesc.Count = 1;
	Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	Desc.Flags = Flags;
	Microsoft::WRL::ComPtr<ID3D12Resource> Created; // 成功するまで呼出側へ公開しない。
	if ( FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Desc, State, nullptr, IID_PPV_ARGS( &Created ) ) ) )
	{
		return false;
	}
	Out = MoveTemp( Created );
	return true;
}

bool CDX12BufferStore::BeginCopy()
{
	if ( !bHealthy || !Device || PendingSource || PendingTarget || FenceValue == TNumericLimits<uint64>::Max() )
	{
		return false;
	}
	if ( FAILED( Allocator->Reset() ) || FAILED( Commands->Reset( Allocator.Get(), nullptr ) ) )
	{
		bHealthy = false;
		return false;
	}
	return true;
}

bool CDX12BufferStore::SubmitCopy( ID3D12Resource* Source, ID3D12Resource* Target )
{
	if ( FAILED( Commands->Close() ) )
	{
		bHealthy = false;
		return false;
	}
	PendingSource = Source;
	PendingTarget = Target;
	ID3D12CommandList* Lists[] = { Commands.Get() }; // 資源を所有してからGPUへ提出する。
	Queue->ExecuteCommandLists( 1, Lists );
	++FenceValue;
	bHealthy = SUCCEEDED( Queue->Signal( Fence.Get(), FenceValue ) );
	uint64 Completed = Fence->GetCompletedValue(); // 最大値はデバイス喪失を表す。
	bHealthy = bHealthy && Completed != TNumericLimits<uint64>::Max();
	if ( bHealthy && Completed < FenceValue )
	{
		bHealthy = SUCCEEDED( Fence->SetEventOnCompletion( FenceValue, FenceEvent ) ) && WaitForSingleObject( FenceEvent, 10000 ) == WAIT_OBJECT_0;
		Completed = Fence->GetCompletedValue();
		bHealthy = bHealthy && Completed != TNumericLimits<uint64>::Max() && Completed >= FenceValue;
	}
	if ( bHealthy )
	{
		PendingSource.Reset();
		PendingTarget.Reset();
	}
	return bHealthy;
}

FBufferResourceHandle CDX12BufferStore::CreateBuffer( TArrayView<const uint8> Bytes, ERenderBufferShaderAccess Access )
{
	if ( !bHealthy || !Bytes.GetData() || Bytes.Num() <= 0 || static_cast<uint64>( Bytes.Num() ) > MaxBufferBytes || Buffers.Num() + Retired.Num() >= MaxBuffers || static_cast<uint64>( Bytes.Num() ) > MaxResidentBytes - ResidentBytes || ( Access != ERenderBufferShaderAccess::None && Access != ERenderBufferShaderAccess::Read && Access != ERenderBufferShaderAccess::ReadWrite ) || ( Access != ERenderBufferShaderAccess::None && Bytes.Num() % 4 != 0 ) )
	{
		return FBufferResourceHandle();
	}
	FBufferRecord Created; // GPU転送が終わった資源だけを登録する。
	Created.Size = static_cast<uint64>( Bytes.Num() );
	Created.Access = Access;
	Microsoft::WRL::ComPtr<ID3D12Resource> Upload; // 完了まで残すCPU書き込み用の領域。
	if ( !CreateResource( Created.Size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON, Created.Resource, Access == ERenderBufferShaderAccess::ReadWrite ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE ) || !CreateResource( Created.Size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, Upload ) )
	{
		return FBufferResourceHandle();
	}
	void* Mapped = nullptr; // upload領域のCPUアドレス。
	const D3D12_RANGE NoRead = { 0, 0 }; // CPUからは読み取らない。
	if ( FAILED( Upload->Map( 0, &NoRead, &Mapped ) ) )
	{
		return FBufferResourceHandle();
	}
	FMemory::Memcpy( Mapped, Bytes.GetData(), static_cast<usize>( Bytes.Num() ) );
	const D3D12_RANGE Written = { 0, static_cast<SIZE_T>( Bytes.Num() ) }; // 変更した範囲。
	Upload->Unmap( 0, &Written );
	if ( !BeginCopy() )
	{
		return FBufferResourceHandle();
	}
	D3D12_RESOURCE_BARRIER Barrier = {}; // 転送元との初期状態の違いを明示する。
	Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Barrier.Transition.pResource = Created.Resource.Get();
	Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
	Commands->ResourceBarrier( 1, &Barrier );
	Commands->CopyBufferRegion( Created.Resource.Get(), 0, Upload.Get(), 0, Created.Size );
	Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
	Commands->ResourceBarrier( 1, &Barrier );
	if ( !SubmitCopy( Upload.Get(), Created.Resource.Get() ) )
	{
		return FBufferResourceHandle();
	}
	const FBufferResourceHandle Handle = FBufferResourceHandle::Allocate(); // 他の所有先や過去の初期化と重ならない番号。
	if ( !Handle.IsValid() )
	{
		return FBufferResourceHandle();
	}
	ResidentBytes += Created.Size;
	Buffers.Add( Handle.Value, MoveTemp( Created ) );
	return Handle;
}

void CDX12BufferStore::ReleaseBuffer( FBufferResourceHandle Buffer )
{
	if ( FBufferRecord* Found = Buffers.Find( Buffer.Value ) )
	{
		Retired.Add( MoveTemp( *Found ) );
		Buffers.Remove( Buffer.Value );
	}
}

bool CDX12BufferStore::GetBufferSize( FBufferResourceHandle Buffer, uint64& OutSize ) const
{
	const FBufferRecord* Found = Buffers.Find( Buffer.Value ); // 現在も登録されている資源だけを返す。
	if ( !Found )
	{
		return false;
	}
	OutSize = Found->Size;
	return true;
}

bool CDX12BufferStore::UsesDeviceAndQueue( ID3D12Device* InDevice, ID3D12CommandQueue* InQueue ) const
{
	return bHealthy && Device.Get() == InDevice && Queue.Get() == InQueue;
}

bool CDX12BufferStore::AcquireComputeBuffer( FBufferResourceHandle Buffer, ERenderBufferShaderAccess Access, FDX12BufferBinding& Out ) const
{
	const FBufferRecord* Found = Buffers.Find( Buffer.Value ); // 公開番号が残っている間だけ新しい提出を許す。
	if ( !bHealthy || !Found || ( Access != ERenderBufferShaderAccess::Read && Access != ERenderBufferShaderAccess::ReadWrite ) || Found->Access == ERenderBufferShaderAccess::None || ( Access == ERenderBufferShaderAccess::ReadWrite && Found->Access != ERenderBufferShaderAccess::ReadWrite ) )
	{
		return false;
	}
	Out.Resource = Found->Resource;
	Out.Size = Found->Size;
	return true;
}

bool CDX12BufferStore::ReadBuffer( FBufferResourceHandle Buffer, uint64 Offset, int32 NumBytes, TArray<uint8>& Out )
{
	const FBufferRecord* Found = Buffers.Find( Buffer.Value ); // 範囲の検証中は既存の出力を変更しない。
	if ( !bHealthy || !Found || NumBytes < 0 || Offset > Found->Size || static_cast<uint64>( NumBytes ) > Found->Size - Offset )
	{
		return false;
	}
	if ( NumBytes == 0 )
	{
		Out.Reset();
		return true;
	}
	Microsoft::WRL::ComPtr<ID3D12Resource> Readback; // GPUが書き終えてからCPUへ公開する領域。
	if ( !CreateResource( static_cast<uint64>( NumBytes ), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, Readback ) || !BeginCopy() )
	{
		return false;
	}
	D3D12_RESOURCE_BARRIER Barrier = {}; // 提出の間はCOMMONへ戻して保持する。
	Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Barrier.Transition.pResource = Found->Resource.Get();
	Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	Commands->ResourceBarrier( 1, &Barrier );
	Commands->CopyBufferRegion( Readback.Get(), 0, Found->Resource.Get(), Offset, static_cast<uint64>( NumBytes ) );
	Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
	Commands->ResourceBarrier( 1, &Barrier );
	if ( !SubmitCopy( Found->Resource.Get(), Readback.Get() ) )
	{
		return false;
	}
	void* Mapped = nullptr; // 完了後だけ読むCPUアドレス。
	const D3D12_RANGE Read = { 0, static_cast<SIZE_T>( NumBytes ) }; // 読み取る範囲。
	if ( FAILED( Readback->Map( 0, &Read, &Mapped ) ) )
	{
		return false;
	}
	TArray<uint8> Result; // 全体を複写してから呼出側へ渡す。
	Result.SetNum( NumBytes );
	FMemory::Memcpy( Result.GetData(), Mapped, static_cast<usize>( NumBytes ) );
	const D3D12_RANGE NoWrite = { 0, 0 }; // CPUからの変更はない。
	Readback->Unmap( 0, &NoWrite );
	Out = MoveTemp( Result );
	return true;
}

bool CDX12BufferStore::AcquireGeometryBuffer( FBufferResourceHandle Buffer, FDX12BufferBinding& Out ) const
{
	const FBufferRecord* Found = Buffers.Find( Buffer.Value ); // 解除済み・別所有先の番号では新しい描画を許可しない。
	if ( !bHealthy || !Found || !Device || FAILED( Device->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	Out.Resource = Found->Resource;
	Out.Size = Found->Size;
	return true;
}
