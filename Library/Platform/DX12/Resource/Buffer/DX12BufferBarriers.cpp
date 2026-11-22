// SPDX-License-Identifier: MIT
#include "DX12BufferBarriers.h"

void NDX12BufferBarriers::AccumulateReadState( TArray<D3D12_RESOURCE_BARRIER> &Barriers, ID3D12Resource *Resource, D3D12_RESOURCE_STATES ReadState )
{
	for ( D3D12_RESOURCE_BARRIER &Entry : Barriers )
	{
		if ( Entry.Transition.pResource == Resource )
		{
			Entry.Transition.StateAfter |= ReadState;
			return;
		}
	}
	D3D12_RESOURCE_BARRIER Added = {}; // 提出間で待機状態になっているCOMMONから読み取り用途へ切り替える。
	Added.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Added.Transition.pResource = Resource;
	Added.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	Added.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	Added.Transition.StateAfter = ReadState;
	Barriers.Add( Added );
}
