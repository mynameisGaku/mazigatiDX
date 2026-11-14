// SPDX-License-Identifier: MIT
#include "DX12TextureReadback.h"
#include "Library/Core/Memory/Memory.h"

bool CDX12TextureReadback::InitializeAfterGPUIdle( ID3D12Device* Device, ID3D12Resource* Texture )
{
	if ( !Device || !Texture )
	{
		return false;
	}
	const D3D12_RESOURCE_DESC Source = Texture->GetDesc(); // 単一面のRGBA8だけを取り扱う。
	if ( Source.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || Source.Format != DXGI_FORMAT_R8G8B8A8_UNORM || Source.SampleDesc.Count != 1 || Source.DepthOrArraySize != 1 || Source.MipLevels != 1 || Source.Width == 0 || Source.Width > 16384 || Source.Height == 0 || Source.Height > 16384 || Source.Width * Source.Height * 4 > 256ull * 1024 * 1024 )
	{
		return false;
	}
	if ( Buffer && Source.Width == static_cast<uint64>( Width ) && Source.Height == static_cast<uint32>( Height ) )
	{
		return true;
	}
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT Layout = {}; // GPU固有の行間隔を求める。
	UINT64 Bytes = 0; // 行の余白を含むコピー先の必要量。
	Device->GetCopyableFootprints( &Source, 0, 1, 0, &Layout, nullptr, nullptr, &Bytes );
	D3D12_HEAP_PROPERTIES Heap = {}; // CPUから読む領域。
	Heap.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC Desc = {}; // 画像をそのままMapせず、バッファへコピーする。
	Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	Desc.Width = Bytes;
	Desc.Height = 1;
	Desc.DepthOrArraySize = 1;
	Desc.MipLevels = 1;
	Desc.SampleDesc.Count = 1;
	Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	Microsoft::WRL::ComPtr<ID3D12Resource> Created; // 成功してから古い領域を置き換える。
	if ( Bytes == 0 || FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &Created ) ) ) )
	{
		return false;
	}
	Buffer = MoveTemp( Created );
	Footprint = Layout;
	BufferBytes = Bytes;
	Width = static_cast<int32>( Source.Width );
	Height = static_cast<int32>( Source.Height );
	return true;
}

bool CDX12TextureReadback::RecordCopy( ID3D12GraphicsCommandList* Commands, ID3D12Resource* Texture, D3D12_RESOURCE_STATES State )
{
	if ( !Buffer || !Commands || !Texture )
	{
		return false;
	}
	const D3D12_RESOURCE_DESC Desc = Texture->GetDesc(); // 生成時と異なる画像を誤ってコピーしない。
	if ( Desc.Width != static_cast<uint64>( Width ) || Desc.Height != static_cast<uint32>( Height ) || Desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM || Desc.SampleDesc.Count != 1 )
	{
		return false;
	}
	D3D12_RESOURCE_BARRIER Barrier = {}; // コピー前後で元の状態を保つ。
	Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Barrier.Transition.pResource = Texture;
	Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	Barrier.Transition.StateBefore = State;
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	if ( State != D3D12_RESOURCE_STATE_COPY_SOURCE )
	{
		Commands->ResourceBarrier( 1, &Barrier );
	}
	D3D12_TEXTURE_COPY_LOCATION Source = {}, Target = {}; // 画像から行の余白を持つバッファへコピーする。
	Source.pResource = Texture;
	Source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	Target.pResource = Buffer.Get();
	Target.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	Target.PlacedFootprint = Footprint;
	Commands->CopyTextureRegion( &Target, 0, 0, 0, &Source, nullptr );
	if ( State != D3D12_RESOURCE_STATE_COPY_SOURCE )
	{
		Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
		Barrier.Transition.StateAfter = State;
		Commands->ResourceBarrier( 1, &Barrier );
	}
	return true;
}

bool CDX12TextureReadback::ReadAfterGPUIdle( TArray<FColor32>& Out, bool bOpaque ) const
{
	if ( !Buffer || Width <= 0 || Height <= 0 )
	{
		return false;
	}
	void* Address = nullptr; // 完了済みのコピー結果だけを読む。
	const D3D12_RANGE Read = { 0, static_cast<SIZE_T>( BufferBytes ) }; // CPUが読む範囲。
	if ( FAILED( Buffer->Map( 0, &Read, &Address ) ) )
	{
		return false;
	}
	TArray<FColor32> Pixels; // 最後まで読めてから呼出側の出力と置き換える。
	Pixels.SetNum( Width * Height );
	for ( int32 Y = 0; Y < Height; ++Y )
	{
		const uint8* Row = static_cast<const uint8*>( Address ) + Footprint.Offset + static_cast<uint64>( Y ) * Footprint.Footprint.RowPitch; // 行の余白を飛ばす。
		FMemory::Memcpy( Pixels.GetData() + Y * Width, Row, static_cast<usize>( Width ) * sizeof( FColor32 ) );
	}
	const D3D12_RANGE NoWrite = { 0, 0 }; // CPUからは変更しない。
	Buffer->Unmap( 0, &NoWrite );
	if ( bOpaque )
	{
		for ( FColor32& Pixel : Pixels )
		{
			Pixel.a = 255;
		}
	}
	Out = MoveTemp( Pixels );
	return true;
}

void CDX12TextureReadback::ResetAfterGPUIdle()
{
	Buffer.Reset();
	Footprint = {};
	BufferBytes = 0;
	Width = Height = 0;
}
