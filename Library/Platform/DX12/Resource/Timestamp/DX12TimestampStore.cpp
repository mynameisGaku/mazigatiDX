// SPDX-License-Identifier: MIT
#include "DX12TimestampStore.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Render/RenderResourceSerial.h"
#include "Library/Core/Types/Numeric.h"

CDX12TimestampStore::~CDX12TimestampStore()
{
	ResetAfterGPUIdle();
}

bool CDX12TimestampStore::Initialize( ID3D12Device* InDevice, ID3D12CommandQueue* InQueue )
{
	if ( Device || !InDevice || !InQueue || InQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT )
	{
		return false;
	}
	Microsoft::WRL::ComPtr<ID3D12Device> QueueDevice; // 異なるGPUのキューを組み合わせない。
	if ( FAILED( InQueue->GetDevice( IID_PPV_ARGS( &QueueDevice ) ) ) || QueueDevice.Get() != InDevice || FAILED( InDevice->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	UINT64 QueueFreq = 0; // キューのタイムスタンプティック周波数（Hz）。
	if ( FAILED( InQueue->GetTimestampFrequency( &QueueFreq ) ) || QueueFreq == 0 )
	{
		return false;
	}
	D3D12_QUERY_HEAP_DESC HeapDesc = {}; // Begin / End ペアを保持するTIMESTAMPヒープ。
	HeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	HeapDesc.Count = MaxActiveQueries * 2;
	HeapDesc.NodeMask = 0;
	Microsoft::WRL::ComPtr<ID3D12QueryHeap> CreatedHeap;
	if ( FAILED( InDevice->CreateQueryHeap( &HeapDesc, IID_PPV_ARGS( &CreatedHeap ) ) ) )
	{
		return false;
	}
	D3D12_HEAP_PROPERTIES ReadbackHeap = {}; // ResolveQueryDataの出力先となるCPU読み取り用ヒープ。
	ReadbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC BufferDesc = {};
	BufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	BufferDesc.Width = static_cast<UINT64>( MaxActiveQueries ) * 2 * sizeof( uint64 );
	BufferDesc.Height = 1;
	BufferDesc.DepthOrArraySize = 1;
	BufferDesc.MipLevels = 1;
	BufferDesc.SampleDesc.Count = 1;
	BufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	Microsoft::WRL::ComPtr<ID3D12Resource> CreatedReadback;
	if ( FAILED( InDevice->CreateCommittedResource( &ReadbackHeap, D3D12_HEAP_FLAG_NONE, &BufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &CreatedReadback ) ) ) )
	{
		return false;
	}
	Microsoft::WRL::ComPtr<ID3D12Fence> CreatedFence;
	if ( FAILED( InDevice->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &CreatedFence ) ) ) )
	{
		return false;
	}
	HANDLE CreatedEvent = CreateEventW( nullptr, FALSE, FALSE, nullptr );
	if ( !CreatedEvent )
	{
		return false;
	}
	const uint64 AllocatedSerial = NRenderResourceSerial::Allocate();
	if ( AllocatedSerial == 0 )
	{
		CloseHandle( CreatedEvent );
		return false;
	}
	Device = InDevice;
	Queue = InQueue;
	QueryHeap = MoveTemp( CreatedHeap );
	ReadbackBuffer = MoveTemp( CreatedReadback );
	Fence = MoveTemp( CreatedFence );
	FenceEvent = CreatedEvent;
	FrequencyHz = QueueFreq;
	QueueSerial = AllocatedSerial;
	NextBatchId = 0;
	NextFenceValue = 0;
	FreeSlots.Reset();
	FreeSlots.Reserve( static_cast<int32>( MaxActiveQueries ) );
	for ( int32 Index = static_cast<int32>( MaxActiveQueries ) - 1; Index >= 0; --Index )
	{
		FreeSlots.Add( static_cast<uint32>( Index ) );
	}
	LiveQueries.Reset();
	RetiredSlots.Reset();
	Notifications.Reset();
	bHealthy = true;
	return true;
}

bool CDX12TimestampStore::UsesDeviceAndQueue( ID3D12Device* InDevice, ID3D12CommandQueue* InQueue ) const
{
	return bHealthy && Device.Get() == InDevice && Queue.Get() == InQueue;
}

void CDX12TimestampStore::ResetAfterGPUIdle()
{
	if ( FenceEvent )
	{
		CloseHandle( FenceEvent );
		FenceEvent = nullptr;
	}
	LiveQueries.Reset();
	RetiredSlots.Reset();
	FreeSlots.Reset();
	Notifications.Reset();
	ReadbackBuffer.Reset();
	QueryHeap.Reset();
	Fence.Reset();
	Queue.Reset();
	Device.Reset();
	FrequencyHz = 0;
	QueueSerial = 0;
	NextBatchId = 0;
	NextFenceValue = 0;
	bHealthy = false;
}

bool CDX12TimestampStore::CollectCompleted()
{
	if ( !bHealthy || !Device || !Queue || !Fence || !ReadbackBuffer )
	{
		return false;
	}
	const uint64 Completed = Fence->GetCompletedValue(); // 最大値はデバイス喪失を表す。
	if ( Completed == TNumericLimits<uint64>::Max() || FAILED( Device->GetDeviceRemovedReason() ) )
	{
		bHealthy = false;
		return false;
	}
	bool bAnyLiveCompleted = false;
	for ( const auto& Pair : LiveQueries )
	{
		if ( Pair.Value.State == EQueryState::Submitted && Pair.Value.FenceValue <= Completed )
		{
			bAnyLiveCompleted = true;
			break;
		}
	}
	if ( bAnyLiveCompleted )
	{
		const D3D12_RANGE ReadRange = { 0, static_cast<SIZE_T>( MaxActiveQueries ) * 2 * sizeof( uint64 ) };
		void* Mapped = nullptr;
		if ( FAILED( ReadbackBuffer->Map( 0, &ReadRange, &Mapped ) ) )
		{
			bHealthy = false;
			return false;
		}
		const auto* Ticks = static_cast<const uint64*>( Mapped );
		for ( auto& Pair : LiveQueries )
		{
			FQueryRecord& Record = Pair.Value;
			if ( Record.State == EQueryState::Submitted && Record.FenceValue <= Completed )
			{
				const uint64 BeginTicks = Ticks[Record.Slot * 2];
				const uint64 EndTicks = Ticks[Record.Slot * 2 + 1];
				const uint64 DeltaTicks = EndTicks >= BeginTicks ? ( EndTicks - BeginTicks ) : 0;
				Record.CachedResult.BeginTicks = BeginTicks;
				Record.CachedResult.EndTicks = EndTicks;
				Record.CachedResult.FrequencyHz = FrequencyHz;
				Record.CachedResult.ElapsedMs = ( static_cast<float64>( DeltaTicks ) * 1000.0 ) / static_cast<float64>( FrequencyHz );
				Record.State = EQueryState::Completed;
			}
		}
		const D3D12_RANGE NoWrite = { 0, 0 }; // CPUからはReadbackバッファを書き換えない。
		ReadbackBuffer->Unmap( 0, &NoWrite );
	}
	for ( int32 Index = RetiredSlots.Num() - 1; Index >= 0; --Index )
	{
		const FRetiredSlot& Retired = RetiredSlots[Index];
		if ( Retired.BatchId == 0 && Retired.FenceValue <= Completed )
		{
			FreeSlots.Add( Retired.Slot );
			RetiredSlots.RemoveAt( Index );
		}
	}
	Notifications.RemoveAll( [Completed]( uint64 Value ) { return Value <= Completed; } );
	return true;
}

bool CDX12TimestampStore::GetGpuTimestampCapabilities( FGpuTimestampCapabilities& Out ) const
{
	if ( !bHealthy || !Device || !Queue || FAILED( Device->GetDeviceRemovedReason() ) || FrequencyHz == 0 )
	{
		return false;
	}
	Out.bSupported = true;
	Out.FrequencyHz = FrequencyHz;
	Out.MaxActiveQueries = MaxActiveQueries;
	return true;
}

FGpuTimestampHandle CDX12TimestampStore::CreateGpuTimestampQuery()
{
	if ( !CollectCompleted() || FreeSlots.IsEmpty() )
	{
		return {};
	}
	const uint64 HandleValue = NRenderResourceSerial::Allocate();
	if ( HandleValue == 0 )
	{
		return {};
	}
	const uint32 Slot = FreeSlots.Last();
	FreeSlots.RemoveAt( FreeSlots.Num() - 1 );
	FQueryRecord Record;
	Record.Slot = Slot;
	Record.State = EQueryState::Idle;
	LiveQueries.Add( HandleValue, Record );
	return FGpuTimestampHandle( HandleValue );
}

void CDX12TimestampStore::ReleaseGpuTimestampQuery( FGpuTimestampHandle Query )
{
	if ( !Query.IsValid() )
	{
		return;
	}
	FQueryRecord Removed;
	if ( !LiveQueries.RemoveAndCopyValue( Query.Value, Removed ) )
	{
		return;
	}
	if ( Removed.State == EQueryState::Idle || Removed.State == EQueryState::Completed )
	{
		FreeSlots.Add( Removed.Slot );
		return;
	}
	FRetiredSlot Retired;
	Retired.Slot = Removed.Slot;
	if ( Removed.State == EQueryState::Submitted )
	{
		Retired.FenceValue = Removed.FenceValue;
	}
	else
	{
		Retired.BatchId = Removed.BatchId;
	}
	RetiredSlots.Add( Retired );
	CollectCompleted();
}

bool CDX12TimestampStore::IsGpuTimestampReady( FGpuTimestampHandle Query, bool& OutReady )
{
	if ( !Query.IsValid() || !CollectCompleted() )
	{
		return false;
	}
	const FQueryRecord* Record = LiveQueries.Find( Query.Value );
	if ( !Record )
	{
		return false;
	}
	OutReady = Record->State == EQueryState::Completed;
	return true;
}

bool CDX12TimestampStore::WaitGpuTimestamp( FGpuTimestampHandle Query, uint32 TimeoutMs )
{
	if ( TimeoutMs > 60000 || !Query.IsValid() || !CollectCompleted() )
	{
		return false;
	}
	const FQueryRecord* Initial = LiveQueries.Find( Query.Value );
	if ( !Initial )
	{
		return false;
	}
	if ( Initial->State == EQueryState::Completed )
	{
		return true;
	}
	if ( Initial->State != EQueryState::Submitted || TimeoutMs == 0 )
	{
		return false;
	}
	const uint64 TargetFence = Initial->FenceValue;
	if ( !Notifications.Contains( TargetFence ) && FAILED( Fence->SetEventOnCompletion( TargetFence, FenceEvent ) ) )
	{
		bHealthy = false;
		return false;
	}
	if ( !Notifications.Contains( TargetFence ) )
	{
		Notifications.Add( TargetFence );
	}
	const uint64 Started = GetTickCount64();
	for ( ;; )
	{
		const uint64 Elapsed = GetTickCount64() - Started;
		if ( Elapsed >= TimeoutMs )
		{
			if ( !CollectCompleted() )
			{
				return false;
			}
			const FQueryRecord* AfterTimeout = LiveQueries.Find( Query.Value );
			return AfterTimeout && AfterTimeout->State == EQueryState::Completed;
		}
		const DWORD WaitResult = WaitForSingleObject( FenceEvent, static_cast<DWORD>( TimeoutMs - Elapsed ) );
		if ( WaitResult != WAIT_OBJECT_0 )
		{
			return false;
		}
		if ( !CollectCompleted() )
		{
			return false;
		}
		const FQueryRecord* Current = LiveQueries.Find( Query.Value );
		if ( !Current )
		{
			return false;
		}
		if ( Current->State == EQueryState::Completed )
		{
			return true;
		}
	}
}

bool CDX12TimestampStore::GetGpuTimestampResult( FGpuTimestampHandle Query, FGpuTimestampResult& Out )
{
	if ( !Query.IsValid() || !CollectCompleted() )
	{
		return false;
	}
	const FQueryRecord* Record = LiveQueries.Find( Query.Value );
	if ( !Record || Record->State != EQueryState::Completed )
	{
		return false;
	}
	Out = Record->CachedResult;
	return true;
}

bool CDX12TimestampStore::IsCompatibleCommandList( ID3D12GraphicsCommandList* Commands ) const
{
	if ( !Commands || !Device )
	{
		return false;
	}
	Microsoft::WRL::ComPtr<ID3D12Device> CmdDevice;
	return SUCCEEDED( Commands->GetDevice( IID_PPV_ARGS( &CmdDevice ) ) ) && CmdDevice.Get() == Device.Get();
}

uint64 CDX12TimestampStore::BeginBatch()
{
	if ( !CollectCompleted() )
	{
		return 0;
	}
	++NextBatchId;
	if ( NextBatchId == 0 )
	{
		++NextBatchId;
	}
	return NextBatchId;
}

bool CDX12TimestampStore::BeginRecording( ID3D12GraphicsCommandList* Commands, uint64 BatchId, FGpuTimestampHandle Query )
{
	if ( BatchId == 0 || !Query.IsValid() || !CollectCompleted() || !IsCompatibleCommandList( Commands ) )
	{
		return false;
	}
	FQueryRecord* Record = LiveQueries.Find( Query.Value );
	if ( !Record || ( Record->State != EQueryState::Idle && Record->State != EQueryState::Completed ) )
	{
		return false;
	}
	Record->State = EQueryState::Recording;
	Record->BatchId = BatchId;
	Record->FenceValue = 0;
	Record->CachedResult = {};
	RecordBeginAtSlot( Commands, Record->Slot * 2 );
	return true;
}

bool CDX12TimestampStore::EndRecording( ID3D12GraphicsCommandList* Commands, uint64 BatchId, FGpuTimestampHandle Query )
{
	if ( BatchId == 0 || !Query.IsValid() || !CollectCompleted() || !IsCompatibleCommandList( Commands ) )
	{
		return false;
	}
	FQueryRecord* Record = LiveQueries.Find( Query.Value );
	if ( !Record || Record->State != EQueryState::Recording || Record->BatchId != BatchId )
	{
		return false;
	}
	RecordEndAndResolveAtSlot( Commands, Record->Slot * 2 );
	Record->State = EQueryState::PendingSubmit;
	return true;
}

bool CDX12TimestampStore::HasActiveRecording( uint64 BatchId ) const
{
	if ( BatchId == 0 )
	{
		return false;
	}
	for ( const auto& Pair : LiveQueries )
	{
		if ( Pair.Value.BatchId == BatchId && Pair.Value.State == EQueryState::Recording )
		{
			return true;
		}
	}
	return false;
}

void CDX12TimestampStore::ResetBatchInternal( uint64 BatchId, bool bIncludePendingSubmit )
{
	if ( BatchId == 0 )
	{
		return;
	}
	for ( auto& Pair : LiveQueries )
	{
		FQueryRecord& Record = Pair.Value;
		if ( Record.BatchId == BatchId && ( Record.State == EQueryState::Recording || ( bIncludePendingSubmit && Record.State == EQueryState::PendingSubmit ) ) )
		{
			Record.State = EQueryState::Idle;
			Record.BatchId = 0;
			Record.FenceValue = 0;
			Record.CachedResult = {};
		}
	}
	if ( bIncludePendingSubmit )
	{
		for ( int32 Index = RetiredSlots.Num() - 1; Index >= 0; --Index )
		{
			if ( RetiredSlots[Index].BatchId == BatchId )
			{
				FreeSlots.Add( RetiredSlots[Index].Slot );
				RetiredSlots.RemoveAt( Index );
			}
		}
	}
}

void CDX12TimestampStore::AbortUnclosedInBatch( uint64 BatchId )
{
	ResetBatchInternal( BatchId, false );
}

void CDX12TimestampStore::AbortBatch( uint64 BatchId )
{
	ResetBatchInternal( BatchId, true );
}

bool CDX12TimestampStore::CommitBatch( uint64 BatchId )
{
	if ( BatchId == 0 )
	{
		return true;
	}
	bool bAnyWork = false;
	for ( const auto& Pair : LiveQueries )
	{
		if ( Pair.Value.BatchId == BatchId && Pair.Value.State == EQueryState::PendingSubmit )
		{
			bAnyWork = true;
			break;
		}
	}
	if ( !bAnyWork )
	{
		for ( const FRetiredSlot& Retired : RetiredSlots )
		{
			if ( Retired.BatchId == BatchId )
			{
				bAnyWork = true;
				break;
			}
		}
	}
	if ( !bAnyWork )
	{
		return true;
	}
	if ( !CollectCompleted() || NextFenceValue >= TNumericLimits<uint64>::Max() - 1 )
	{
		bHealthy = false;
		return false;
	}
	const uint64 SubmittedFence = ++NextFenceValue;
	if ( FAILED( Queue->Signal( Fence.Get(), SubmittedFence ) ) )
	{
		bHealthy = false;
		return false;
	}
	for ( auto& Pair : LiveQueries )
	{
		FQueryRecord& Record = Pair.Value;
		if ( Record.BatchId == BatchId && Record.State == EQueryState::PendingSubmit )
		{
			Record.State = EQueryState::Submitted;
			Record.BatchId = 0;
			Record.FenceValue = SubmittedFence;
		}
	}
	for ( FRetiredSlot& Retired : RetiredSlots )
	{
		if ( Retired.BatchId == BatchId )
		{
			Retired.BatchId = 0;
			Retired.FenceValue = SubmittedFence;
		}
	}
	return true;
}

bool CDX12TimestampStore::TryPrepareComputeSlot( FGpuTimestampHandle Query, uint32& OutBeginSlot )
{
	if ( !Query.IsValid() || !CollectCompleted() )
	{
		return false;
	}
	const FQueryRecord* Record = LiveQueries.Find( Query.Value );
	if ( !Record || ( Record->State != EQueryState::Idle && Record->State != EQueryState::Completed ) )
	{
		return false;
	}
	OutBeginSlot = Record->Slot * 2;
	return true;
}

void CDX12TimestampStore::RecordBeginAtSlot( ID3D12GraphicsCommandList* Commands, uint32 BeginSlot ) const
{
	if ( Commands && QueryHeap && BeginSlot + 1 < MaxActiveQueries * 2 )
	{
		Commands->EndQuery( QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, BeginSlot );
	}
}

void CDX12TimestampStore::RecordEndAndResolveAtSlot( ID3D12GraphicsCommandList* Commands, uint32 BeginSlot ) const
{
	if ( Commands && QueryHeap && ReadbackBuffer && BeginSlot + 1 < MaxActiveQueries * 2 )
	{
		Commands->EndQuery( QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, BeginSlot + 1 );
		Commands->ResolveQueryData( QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, BeginSlot, 2, ReadbackBuffer.Get(), static_cast<UINT64>( BeginSlot ) * sizeof( uint64 ) );
	}
}

bool CDX12TimestampStore::CommitComputeSlot( FGpuTimestampHandle Query, uint32 BeginSlot )
{
	if ( !bHealthy || !Queue || !Fence || !Query.IsValid() || NextFenceValue >= TNumericLimits<uint64>::Max() - 1 )
	{
		bHealthy = false;
		return false;
	}
	FQueryRecord* Record = LiveQueries.Find( Query.Value );
	if ( !Record || Record->Slot * 2 != BeginSlot )
	{
		return false;
	}
	const uint64 SubmittedFence = ++NextFenceValue;
	if ( FAILED( Queue->Signal( Fence.Get(), SubmittedFence ) ) )
	{
		bHealthy = false;
		return false;
	}
	Record->State = EQueryState::Submitted;
	Record->BatchId = 0;
	Record->FenceValue = SubmittedFence;
	Record->CachedResult = {};
	return true;
}
