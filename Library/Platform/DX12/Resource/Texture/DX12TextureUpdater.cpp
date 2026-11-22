// SPDX-License-Identifier: MIT
#include "DX12TextureUpdater.h"
#include "Library/Core/Memory/Memory.h"

namespace
{
	constexpr D3D12_RESOURCE_STATES ShaderResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; // 描画パスの外での、常の状態。

	// 画像全体の状態を遷移させる命令を記録する。
	void RecordTransition( ID3D12GraphicsCommandList *Commands, ID3D12Resource *Resource, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After )
	{
		D3D12_RESOURCE_BARRIER Barrier = {};
		Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		Barrier.Transition.pResource = Resource;
		Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		Barrier.Transition.StateBefore = Before;
		Barrier.Transition.StateAfter = After;
		Commands->ResourceBarrier( 1, &Barrier );
	}

	// RGBA8画像の大きさ（バイト）。
	int64 TextureBytes( int32 Width, int32 Height )
	{
		return static_cast<int64>( Width ) * Height * 4;
	}
} // namespace

void CDX12TextureUpdater::Connect( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue, CDX12TextureCreator *InCreator, CDX12DescriptorPool *InPool )
{
	Device = InDevice;
	Queue = InQueue;
	Creator = InCreator;
	Pool = InPool;
	Staging.Connect( InDevice, InQueue );
}

void CDX12TextureUpdater::Disconnect()
{
	for ( FDX12TextureRecord &Version : FreeVersions )
	{
		Destroy( Version );
	}
	FreeVersions.Reset();
	Staging.Disconnect();
	FreeVersionBytes = 0;
	Device = nullptr;
	Queue = nullptr;
	Creator = nullptr;
	Pool = nullptr;
}

void CDX12TextureUpdater::Destroy( FDX12TextureRecord &Texture )
{
	if ( Pool && Texture.Descriptor.Serial != 0 )
	{
		Pool->Release( Texture.Descriptor );
	}
	Texture = FDX12TextureRecord();
}

void CDX12TextureUpdater::GiveBack( FDX12TextureRecord &Version, bool bRecycled )
{
	if ( bRecycled )
	{
		FreeVersionBytes += TextureBytes( Version.Width, Version.Height );
		FreeVersions.Add( MoveTemp( Version ) );
	}
	else
	{
		Destroy( Version );
	}
}

bool CDX12TextureUpdater::Update( int32 Width, int32 Height, const FColor32 *Pixels, FDX12TextureRecord &Out )
{
	if ( !Device || !Queue || !Creator || !Pixels || Width <= 0 || Height <= 0 )
	{
		return false;
	}
	FDX12TextureRecord Version; // 新しい版（再利用した旧版か、新しく作った版）。
	bool bRecycled = false; // 再利用した版は読み取りの状態、新しく作った版は転送先の状態にある。
	for ( int32 Index = 0; Index < FreeVersions.Num(); ++Index )
	{
		if ( FreeVersions[Index].Width == Width && FreeVersions[Index].Height == Height )
		{
			Version = MoveTemp( FreeVersions[Index] );
			FreeVersions.RemoveAt( Index );
			FreeVersionBytes -= TextureBytes( Width, Height );
			bRecycled = true;
			break;
		}
	}
	if ( !bRecycled && !Creator->CreateEmptyRGBA8( Width, Height, Version ) )
	{
		return false;
	}
	const D3D12_RESOURCE_DESC Desc = Version.Resource->GetDesc(); // GPUが必要とする行の境界と幅を聞く。
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT Layout = {};
	UINT64 TotalBytes = 0; // paddingを含む転送領域の大きさ。
	Device->GetCopyableFootprints( &Desc, 0, 1, 0, &Layout, nullptr, nullptr, &TotalBytes );
	CDX12CopyStaging::FOpened Opened; // 画素を書く転送領域と、コピーを記録する命令列。
	if ( !Staging.Open( TotalBytes, Opened ) )
	{
		GiveBack( Version, bRecycled );
		return false;
	}
	for ( int32 Row = 0; Row < Height; ++Row )
	{
		FMemory::Memcpy( Opened.Mapped + Layout.Offset + static_cast<usize>( Row ) * Layout.Footprint.RowPitch, Pixels + static_cast<usize>( Row ) * Width, static_cast<usize>( Width ) * sizeof( FColor32 ) );
	}
	ID3D12GraphicsCommandList *Commands = Opened.Commands; // このコピーだけを記録する命令列。
	if ( bRecycled )
	{
		RecordTransition( Commands, Version.Resource.Get(), ShaderResourceState, D3D12_RESOURCE_STATE_COPY_DEST );
	}
	D3D12_TEXTURE_COPY_LOCATION Source = {}; // paddingを含む転送元。
	Source.pResource = Opened.Upload;
	Source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	Source.PlacedFootprint = Layout;
	D3D12_TEXTURE_COPY_LOCATION Target = {}; // 画像の最初の段へ送る。
	Target.pResource = Version.Resource.Get();
	Target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	Target.SubresourceIndex = 0;
	Commands->CopyTextureRegion( &Target, 0, 0, 0, &Source, nullptr );
	RecordTransition( Commands, Version.Resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, ShaderResourceState ); // 以後はshaderから読む。
	if ( !Staging.Submit() ) // 閉じて、完了は待たずに提出する。
	{
		GiveBack( Version, bRecycled );
		return false;
	}
	Out = MoveTemp( Version );
	return true;
}

bool CDX12TextureUpdater::Adopt( FDX12TextureRecord &Retired )
{
	if ( !Device || Retired.Kind != EDX12TextureKind::StandardRGBA8 || Retired.MipLevels != 1 || !Retired.Resource || Retired.Descriptor.Serial == 0 )
	{
		return false;
	}
	const int64 Bytes = TextureBytes( Retired.Width, Retired.Height ); // この版の大きさ。
	if ( Bytes > MaxFreeVersionBytes )
	{
		return false;
	}
	while ( !FreeVersions.IsEmpty() && ( FreeVersions.Num() >= MaxFreeVersions || FreeVersionBytes + Bytes > MaxFreeVersionBytes ) )
	{
		FreeVersionBytes -= TextureBytes( FreeVersions[0].Width, FreeVersions[0].Height ); // 一番古い版から手放す。
		Destroy( FreeVersions[0] );
		FreeVersions.RemoveAt( 0 );
	}
	FreeVersionBytes += Bytes;
	FreeVersions.Add( MoveTemp( Retired ) );
	return true;
}

void CDX12TextureUpdater::CollectAfterGPUIdle()
{
	Staging.CollectAfterGPUIdle();
}
