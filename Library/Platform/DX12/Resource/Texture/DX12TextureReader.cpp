// SPDX-License-Identifier: MIT
#include "DX12TextureReader.h"
#include "Library/Core/Memory/Memory.h"

void CDX12TextureReader::Connect( ID3D12Device *InDevice, CDX12SyncSubmitter *InSubmitter )
{
	Device = InDevice;
	Submitter = InSubmitter;
}

void CDX12TextureReader::Disconnect()
{
	Device = nullptr;
	Submitter = nullptr;
}

bool CDX12TextureReader::ReadRows( const FDX12TextureRecord &Record, DXGI_FORMAT ExpectedFormat, UINT BytesPerPixel, TArray<uint8> &OutRows, UINT &OutRowBytes )
{
	if ( !Device || !Submitter || !Submitter->IsHealthy() || !Record.Resource || Record.Width <= 0 || Record.Height <= 0 || FAILED( Device->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	const D3D12_RESOURCE_DESC Desc = Record.Resource->GetDesc();
	if ( Desc.Format != ExpectedFormat || Desc.Width != static_cast<UINT64>( Record.Width ) || Desc.Height != static_cast<UINT>( Record.Height ) )
	{
		return false;
	}
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT Layout = {};
	UINT64 TotalBytes = 0;
	Device->GetCopyableFootprints( &Desc, 0, 1, 0, &Layout, nullptr, nullptr, &TotalBytes );
	if ( TotalBytes == 0 || Layout.Footprint.RowPitch < static_cast<UINT>( Record.Width ) * BytesPerPixel )
	{
		return false;
	}
	D3D12_HEAP_PROPERTIES Heap = {};
	Heap.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC Buffer = {};
	Buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	Buffer.Width = TotalBytes;
	Buffer.Height = Buffer.DepthOrArraySize = Buffer.MipLevels = Buffer.SampleDesc.Count = 1;
	Buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	Microsoft::WRL::ComPtr<ID3D12Resource> ReadbackBuffer;
	if ( FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &ReadbackBuffer ) ) ) )
	{
		return false;
	}
	constexpr D3D12_RESOURCE_STATES ShaderResource = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; // 定常の読み取りの状態。
	ID3D12GraphicsCommandList *Commands = Submitter->Begin();
	if ( !Commands )
	{
		return false;
	}
	D3D12_RESOURCE_BARRIER Barrier = {};
	Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Barrier.Transition.pResource = Record.Resource.Get();
	Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	Barrier.Transition.StateBefore = ShaderResource;
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	Commands->ResourceBarrier( 1, &Barrier );
	D3D12_TEXTURE_COPY_LOCATION DstLoc = {};
	DstLoc.pResource = ReadbackBuffer.Get();
	DstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	DstLoc.PlacedFootprint = Layout;
	D3D12_TEXTURE_COPY_LOCATION SrcLoc = {};
	SrcLoc.pResource = Record.Resource.Get();
	SrcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	Commands->CopyTextureRegion( &DstLoc, 0, 0, 0, &SrcLoc, nullptr );
	Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
	Barrier.Transition.StateAfter = ShaderResource;
	Commands->ResourceBarrier( 1, &Barrier );
	if ( !Submitter->Submit( Record.Resource.Get(), ReadbackBuffer.Get() ) )
	{
		return false;
	}
	void *Mapped = nullptr;
	const D3D12_RANGE ReadRange = { 0, static_cast<SIZE_T>( TotalBytes ) };
	const D3D12_RANGE NoWrite = { 0, 0 };
	if ( FAILED( ReadbackBuffer->Map( 0, &ReadRange, &Mapped ) ) || !Mapped )
	{
		return false;
	}
	const UINT RowBytes = static_cast<UINT>( Record.Width ) * BytesPerPixel;
	TArray<uint8> Rows;
	Rows.SetNum( static_cast<int32>( static_cast<usize>( RowBytes ) * static_cast<usize>( Record.Height ) ) );
	for ( int32 Row = 0; Row < Record.Height; ++Row )
	{
		const uint8 *SrcRow = static_cast<const uint8 *>( Mapped ) + Layout.Offset + static_cast<usize>( Row ) * Layout.Footprint.RowPitch;
		uint8 *DstRow = Rows.GetData() + static_cast<usize>( Row ) * RowBytes;
		FMemory::Memcpy( DstRow, SrcRow, RowBytes );
	}
	ReadbackBuffer->Unmap( 0, &NoWrite );
	OutRows = MoveTemp( Rows );
	OutRowBytes = RowBytes;
	return true;
}
