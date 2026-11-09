// SPDX-License-Identifier: MIT
#include "DX12DescriptorPool.h"
#include "Library/Core/Types/Numeric.h"

bool FDX12DescriptorPool::Initialize( ID3D12Device* Device, int32 Capacity )
{
	if ( Heap || !Device || Capacity <= 0 || Capacity > 1048576 )
	{
		return false;
	}
	D3D12_DESCRIPTOR_HEAP_DESC Desc = {}; // 画像とUI表示で共有できるheap。
	Desc.NumDescriptors = Capacity;
	Desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	Desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if ( FAILED( Device->CreateDescriptorHeap( &Desc, IID_PPV_ARGS( &Heap ) ) ) )
	{
		return false;
	}
	Stride = Device->GetDescriptorHandleIncrementSize( Desc.Type );
	Serials.SetNum( Capacity );
	for ( int32 Index = Capacity - 1; Index >= 0; --Index )
	{
		Serials[Index] = 0;
		Free.Add( Index );
	}
	return true;
}

void FDX12DescriptorPool::Reset()
{
	Free.Reset();
	Serials.Reset();
	Heap.Reset();
	Stride = 0;
}

bool FDX12DescriptorPool::Allocate( FAllocation& Out )
{
	if ( !Heap || Free.IsEmpty() || NextSerial == TNumericLimits<uint64>::Max() )
	{
		return false;
	}
	FAllocation Result; // 成功した区画だけを出力する。
	Result.Owner = this;
	Result.Index = Free.Pop();
	Result.Serial = NextSerial++;
	Serials[Result.Index] = Result.Serial;
	Result.Cpu = Heap->GetCPUDescriptorHandleForHeapStart();
	Result.Gpu = Heap->GetGPUDescriptorHandleForHeapStart();
	Result.Cpu.ptr += static_cast<SIZE_T>( Result.Index ) * Stride;
	Result.Gpu.ptr += static_cast<UINT64>( Result.Index ) * Stride;
	Out = Result;
	return true;
}

bool FDX12DescriptorPool::Release( const FAllocation& Allocation )
{
	if ( !Heap || Allocation.Owner != this || Allocation.Index < 0 || Allocation.Index >= Serials.Num() || Allocation.Serial == 0 || Serials[Allocation.Index] != Allocation.Serial )
	{
		return false;
	}
	Serials[Allocation.Index] = 0;
	Free.Add( Allocation.Index );
	return true;
}

ID3D12DescriptorHeap* FDX12DescriptorPool::GetHeap() const
{
	return Heap.Get();
}
