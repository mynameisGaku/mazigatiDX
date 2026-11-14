// SPDX-License-Identifier: MIT
#include "DX12UploadArena.h"
#include "Library/Core/Types/Numeric.h"

namespace
{
	// 2のべき乗整列を切り上げる。呼び出し前にAlignment有効性を検査済みとする。
	uint64 AlignUpPowerOfTwo( uint64 Value, uint64 Alignment )
	{
		const uint64 Mask = Alignment - 1ull; // ビット演算用の2のべき乗マスク。
		if ( Value > TNumericLimits<uint64>::Max() - Mask )
		{
			return TNumericLimits<uint64>::Max();
		}
		return ( Value + Mask ) & ~Mask;
	}
} // namespace

CDX12UploadArena::~CDX12UploadArena()
{
	ResetAfterGPUIdle();
}

bool CDX12UploadArena::Initialize( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue )
{
	if ( Device || !InDevice || !InQueue || InQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT || FAILED( InDevice->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	Microsoft::WRL::ComPtr<ID3D12Device> QueueDevice; // 異なるデバイスのキューを混在させない。
	if ( FAILED( InQueue->GetDevice( IID_PPV_ARGS( &QueueDevice ) ) ) || QueueDevice.Get() != InDevice || FAILED( InDevice->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &Fence ) ) ) )
	{
		Fence.Reset();
		return false;
	}
	Device = InDevice;
	Queue = InQueue;
	bHealthy = true;
	return true;
}

bool CDX12UploadArena::UsesDeviceAndQueue( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue ) const
{
	return bHealthy && Device && Queue && Device.Get() == InDevice && Queue.Get() == InQueue;
}

void CDX12UploadArena::ReleasePageResource( FPage &Page )
{
	if ( Page.Resource && Page.Mapped )
	{
		// ページ全体にCPU書き込みがあり得ることをGPU検査ツールにも伝える。
		Page.Resource->Unmap( 0, nullptr );
	}
	Page.Mapped = nullptr;
	Page.GpuBase = 0;
	Page.Resource.Reset();
}

void CDX12UploadArena::CollectCompleted()
{
	if ( !Device || !Fence )
	{
		return;
	}
	const uint64 Completed = Fence->GetCompletedValue(); // UINT64_MAXはデバイス喪失を表す。
	if ( Completed == TNumericLimits<uint64>::Max() || FAILED( Device->GetDeviceRemovedReason() ) )
	{
		bHealthy = false;
		return;
	}
	for ( FPage &Page : Pages )
	{
		if ( Page.OpenTicketCount == 0 && Page.Offset > 0 && ( !Page.bCommittedWork || Page.RetirementFence <= Completed ) )
		{
			Page.Offset = 0;
			Page.RetirementFence = 0;
			Page.bCommittedWork = false;
			++RecycleCount;
		}
	}
}

void CDX12UploadArena::ResetAfterGPUIdle()
{
	for ( FPage &Page : Pages )
	{
		ReleasePageResource( Page );
	}
	Pages.Reset();
	OpenTickets.Reset();
	Fence.Reset();
	Queue.Reset();
	Device.Reset();
	FenceValue = 0;
	TotalBytes = 0;
	bHealthy = false;
}

int32 CDX12UploadArena::FindPageIndex( uint64 PageId ) const
{
	for ( int32 i = 0; i < Pages.Num(); ++i )
	{
		if ( Pages[i].Id == PageId )
		{
			return i;
		}
	}
	return -1;
}

int32 CDX12UploadArena::FindTicketIndex( uint64 TicketId ) const
{
	for ( int32 i = 0; i < OpenTickets.Num(); ++i )
	{
		if ( OpenTickets[i].Id == TicketId )
		{
			return i;
		}
	}
	return -1;
}

void CDX12UploadArena::TouchPage( FTicket &Ticket, FPage &Page )
{
	for ( const uint64 ExistingId : Ticket.TouchedPageIds )
	{
		if ( ExistingId == Page.Id )
		{
			return;
		}
	}
	Ticket.TouchedPageIds.Add( Page.Id );
	++Page.OpenTicketCount;
}

bool CDX12UploadArena::AllocatePage( uint64 MinBytes, int32 &OutPageIndex )
{
	if ( !bHealthy || !Device || MinBytes == 0 || MinBytes > MaxSingleAllocationBytes || FAILED( Device->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	const uint64 AlignedSpecial = AlignUpPowerOfTwo( MinBytes, 65536ull ); // 64KiB粒度で一時大サイズページを確保する。
	const uint64 PageSize = MinBytes <= DefaultPageBytes ? DefaultPageBytes : AlignedSpecial;
	if ( PageSize == TNumericLimits<uint64>::Max() || PageSize > MaxTotalBytes )
	{
		return false;
	}
	// 上限超過前に、すでにGPU完了済みで空になった他ページだけを安全に解放して総容量内に収める。
	for ( int32 i = Pages.Num() - 1; i >= 0 && TotalBytes > MaxTotalBytes - PageSize; --i )
	{
		if ( Pages[i].OpenTicketCount == 0 && Pages[i].Offset == 0 )
		{
			TotalBytes -= Pages[i].Size;
			ReleasePageResource( Pages[i] );
			Pages.RemoveAt( i );
		}
	}
	if ( PageSize > MaxTotalBytes - TotalBytes )
	{
		return false;
	}
	D3D12_HEAP_PROPERTIES Heap = {}; // CPUから書き込みGPUが読むアップロードヒープ。
	Heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC Desc = {}; // 永続マップして線形利用する1次元バッファ。
	Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	Desc.Width = PageSize;
	Desc.Height = Desc.DepthOrArraySize = Desc.MipLevels = Desc.SampleDesc.Count = 1;
	Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	Microsoft::WRL::ComPtr<ID3D12Resource> Resource; // マップ確認後に登録する資源。
	void *MappedAddress = nullptr; // write-combinedメモリ。
	const D3D12_RANGE NoRead = { 0, 0 }; // CPU側からは一切読み戻さない契約。
	if ( FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &Resource ) ) ) || FAILED( Resource->Map( 0, &NoRead, &MappedAddress ) ) )
	{
		bHealthy = SUCCEEDED( Device->GetDeviceRemovedReason() ) && bHealthy;
		return false;
	}
	FPage Page;
	Page.Id = NextPageId++;
	Page.Resource = MoveTemp( Resource );
	Page.Mapped = static_cast<uint8 *>( MappedAddress );
	Page.GpuBase = Page.Resource->GetGPUVirtualAddress();
	Page.Size = PageSize;
	TotalBytes += PageSize;
	++CreateResourceCount;
	OutPageIndex = Pages.Num();
	Pages.Add( MoveTemp( Page ) );
	return true;
}

uint64 CDX12UploadArena::BeginTicket()
{
	if ( !bHealthy || !Device || !Queue || !Fence || FAILED( Device->GetDeviceRemovedReason() ) || OpenTickets.Num() >= 256 )
	{
		return 0;
	}
	FTicket Ticket;
	Ticket.Id = NextTicketId++;
	if ( Ticket.Id == 0 )
	{
		Ticket.Id = NextTicketId++;
	}
	OpenTickets.Add( MoveTemp( Ticket ) );
	return OpenTickets.Last().Id;
}

bool CDX12UploadArena::Allocate( uint64 TicketId, uint64 Size, uint64 Alignment, FAllocation &Out )
{
	if ( !bHealthy || !Device || TicketId == 0 || Size == 0 || Size > MaxSingleAllocationBytes || Alignment == 0 || Alignment > 65536ull || ( Alignment & ( Alignment - 1ull ) ) != 0 || FAILED( Device->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	const int32 TicketIndex = FindTicketIndex( TicketId );
	if ( TicketIndex < 0 )
	{
		return false;
	}
	auto TrySuballocateFrom = [&]( FPage &Page, uint64 &OutAligned ) -> bool
	{
		const uint64 Aligned = AlignUpPowerOfTwo( Page.Offset, Alignment );
		if ( Aligned > Page.Size || Size > Page.Size - Aligned )
		{
			return false;
		}
		OutAligned = Aligned;
		return true;
	};

	int32 ChosenPageIndex = -1;
	uint64 ChosenOffset = 0;
	// 既存ページでそのまま空き末尾に入るものを先に探す。
	for ( int32 i = 0; i < Pages.Num(); ++i )
	{
		if ( TrySuballocateFrom( Pages[i], ChosenOffset ) )
		{
			ChosenPageIndex = i;
			break;
		}
	}
	// 空き不足なら最新完了フェンスで回収できるページを戻してから再試行する。
	if ( ChosenPageIndex < 0 )
	{
		CollectCompleted();
		if ( !bHealthy )
		{
			return false;
		}
		for ( int32 i = 0; i < Pages.Num(); ++i )
		{
			if ( TrySuballocateFrom( Pages[i], ChosenOffset ) )
			{
				ChosenPageIndex = i;
				break;
			}
		}
	}
	// 新規ページの先頭は0で整列済みのため、最大要求に余分な整列容量を足さない。
	if ( ChosenPageIndex < 0 )
	{
		if ( !AllocatePage( Size, ChosenPageIndex ) || !TrySuballocateFrom( Pages[ChosenPageIndex], ChosenOffset ) )
		{
			return false;
		}
	}

	// TouchPageが内部状態を増やしたあとではTicketIndexは変わらないが、分かりやすく再参照する。
	FTicket &Ticket = OpenTickets[TicketIndex];
	FPage &Chosen = Pages[ChosenPageIndex];
	TouchPage( Ticket, Chosen );
	Chosen.Offset = ChosenOffset + Size;

	Out.Resource = Chosen.Resource.Get();
	Out.Offset = ChosenOffset;
	Out.Size = Size;
	Out.CpuAddress = Chosen.Mapped + ChosenOffset;
	Out.GpuAddress = Chosen.GpuBase + ChosenOffset;
	return true;
}

bool CDX12UploadArena::CommitTicket( uint64 TicketId )
{
	const int32 TicketIndex = FindTicketIndex( TicketId );
	if ( TicketId == 0 || TicketIndex < 0 )
	{
		return false;
	}
	FTicket Ticket = MoveTemp( OpenTickets[TicketIndex] );
	OpenTickets.RemoveAt( TicketIndex );
	if ( Ticket.TouchedPageIds.IsEmpty() )
	{
		return bHealthy && Device && SUCCEEDED( Device->GetDeviceRemovedReason() );
	}
	bool bSignaled = false;
	if ( bHealthy && Device && Queue && Fence && FenceValue < TNumericLimits<uint64>::Max() && SUCCEEDED( Device->GetDeviceRemovedReason() ) )
	{
		const uint64 NextFence = FenceValue + 1ull;
		if ( SUCCEEDED( Queue->Signal( Fence.Get(), NextFence ) ) )
		{
			FenceValue = NextFence;
			bSignaled = true;
		}
		else
		{
			bHealthy = false;
		}
	}
	else
	{
		bHealthy = false;
	}
	for ( const uint64 PageId : Ticket.TouchedPageIds )
	{
		const int32 PageIndex = FindPageIndex( PageId );
		if ( PageIndex >= 0 )
		{
			FPage &Page = Pages[PageIndex];
			if ( Page.OpenTicketCount > 0 )
			{
				--Page.OpenTicketCount;
			}
			Page.bCommittedWork = true;
			if ( bSignaled && FenceValue > Page.RetirementFence )
			{
				Page.RetirementFence = FenceValue;
			}
			else if ( !bSignaled )
			{
				// シグナル失敗・デバイス喪失時は未完了扱いの最大値を残し再利用させない。
				Page.RetirementFence = TNumericLimits<uint64>::Max();
			}
		}
	}
	return bSignaled;
}

void CDX12UploadArena::AbortTicket( uint64 TicketId )
{
	const int32 TicketIndex = FindTicketIndex( TicketId );
	if ( TicketId == 0 || TicketIndex < 0 )
	{
		return;
	}
	FTicket Ticket = MoveTemp( OpenTickets[TicketIndex] );
	OpenTickets.RemoveAt( TicketIndex );
	for ( const uint64 PageId : Ticket.TouchedPageIds )
	{
		const int32 PageIndex = FindPageIndex( PageId );
		if ( PageIndex >= 0 )
		{
			FPage &Page = Pages[PageIndex];
			if ( Page.OpenTicketCount > 0 )
			{
				--Page.OpenTicketCount;
			}
			if ( Page.OpenTicketCount == 0 && !Page.bCommittedWork && Page.Offset > 0 )
			{
				Page.Offset = 0;
				Page.RetirementFence = 0;
				++RecycleCount;
			}
		}
	}
}

CDX12UploadArena::FArenaStats CDX12UploadArena::GetStats() const
{
	FArenaStats Stats;
	Stats.TotalBytes = TotalBytes;
	Stats.CreateResourceCount = CreateResourceCount;
	Stats.RecycleCount = RecycleCount;
	Stats.PageCount = Pages.Num();
	Stats.ActiveTicketCount = OpenTickets.Num();
	return Stats;
}
