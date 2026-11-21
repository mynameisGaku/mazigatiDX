// SPDX-License-Identifier: MIT
#include "DX12SyncSubmitter.h"

CDX12SyncSubmitter::~CDX12SyncSubmitter()
{
	ResetAfterGPUIdle();
}

bool CDX12SyncSubmitter::Initialize( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue )
{
	if ( Device || !InDevice || !InQueue )
	{
		return false;
	}
	Device = InDevice;
	Queue = InQueue;
	if ( FAILED( Device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &Allocator ) ) ) || FAILED( Device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.Get(), nullptr, IID_PPV_ARGS( &Commands ) ) ) || FAILED( Commands->Close() ) || !Completion.Initialize( Device.Get(), Queue.Get() ) )
	{
		ResetAfterGPUIdle();
		return false;
	}
	bHealthy = true;
	return true;
}

void CDX12SyncSubmitter::CollectAfterGPUIdle()
{
	KeepAlive[0].Reset();
	KeepAlive[1].Reset();
	bHealthy = Device && SUCCEEDED( Device->GetDeviceRemovedReason() );
}

void CDX12SyncSubmitter::ResetAfterGPUIdle()
{
	KeepAlive[0].Reset();
	KeepAlive[1].Reset();
	Commands.Reset();
	Allocator.Reset();
	Completion.ResetAfterGPUIdle();
	Queue.Reset();
	Device.Reset();
	bHealthy = false;
}

ID3D12GraphicsCommandList *CDX12SyncSubmitter::Begin()
{
	if ( !bHealthy || !Device || !Queue || !Allocator || !Commands )
	{
		return nullptr;
	}
	if ( FAILED( Allocator->Reset() ) || FAILED( Commands->Reset( Allocator.Get(), nullptr ) ) )
	{
		bHealthy = false;
		return nullptr;
	}
	return Commands.Get();
}

bool CDX12SyncSubmitter::Submit( ID3D12Resource *KeepAliveFirst, ID3D12Resource *KeepAliveSecond )
{
	if ( !Commands || !Queue )
	{
		return false;
	}
	if ( FAILED( Commands->Close() ) )
	{
		bHealthy = false;
		return false;
	}
	KeepAlive[0] = KeepAliveFirst;
	KeepAlive[1] = KeepAliveSecond;
	ID3D12CommandList *Lists[] = { Commands.Get() }; // 描画の記録先とは別に提出する。
	Queue->ExecuteCommandLists( 1, Lists );
	bHealthy = Completion.Wait( 10000 );
	if ( !bHealthy )
	{
		return false; // 完了が分からないので、GPUが参照しうる資源は保持したまま。
	}
	KeepAlive[0].Reset();
	KeepAlive[1].Reset();
	return true;
}

void CDX12SyncSubmitter::Abort()
{
	if ( Commands )
	{
		bHealthy = SUCCEEDED( Commands->Close() );
	}
}
