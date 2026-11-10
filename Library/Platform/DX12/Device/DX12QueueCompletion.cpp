// SPDX-License-Identifier: MIT
#include "DX12QueueCompletion.h"

CDX12QueueCompletion::~CDX12QueueCompletion()
{
	ResetAfterGPUIdle();
}

bool CDX12QueueCompletion::Initialize( ID3D12Device* InDevice, ID3D12CommandQueue* InQueue )
{
	if ( Device || !InDevice || !InQueue || InQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT )
	{
		return false;
	}
	Microsoft::WRL::ComPtr<ID3D12Device> Actual; // 別のGPUを組み合わせない。
	if ( FAILED( InQueue->GetDevice( IID_PPV_ARGS( &Actual ) ) ) || Actual.Get() != InDevice || FAILED( InDevice->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &Fence ) ) ) )
	{
		return false;
	}
	FenceEvent = CreateEventW( nullptr, FALSE, FALSE, nullptr );
	if ( !FenceEvent )
	{
		Fence.Reset();
		return false;
	}
	Device = InDevice;
	Queue = InQueue;
	return true;
}

bool CDX12QueueCompletion::IsInitialized() const
{
	return Device && Queue && Fence && FenceEvent;
}

void CDX12QueueCompletion::ResetAfterGPUIdle()
{
	if ( FenceEvent )
	{
		CloseHandle( FenceEvent );
		FenceEvent = nullptr;
	}
	Fence.Reset();
	Queue.Reset();
	Device.Reset();
	FenceValue = NotifiedValue = 0;
}

bool CDX12QueueCompletion::Wait( uint32 TimeoutMs )
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
