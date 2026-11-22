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
}

void CDX12TextureUpdater::Disconnect()
{
	for ( FDX12TextureRecord &Version : FreeVersions )
	{
		Destroy( Version );
	}
	FreeVersions.Reset();
	FreeUploads.Reset();
	FreeRecordings.Reset();
	UsedUploads.Reset();
	UsedRecordings.Reset();
	FreeVersionBytes = 0;
	FreeUploadBytes = 0;
	UsedUploadBytes = 0;
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

bool CDX12TextureUpdater::AcquireUpload( uint64 Size, FUploadBuffer &Out )
{
	for ( int32 Index = 0; Index < FreeUploads.Num(); ++Index )
	{
		if ( FreeUploads[Index].Size >= Size && FreeUploads[Index].Size <= Size * 2 )
		{
			Out = MoveTemp( FreeUploads[Index] );
			FreeUploads.RemoveAt( Index );
			FreeUploadBytes -= static_cast<int64>( Out.Size );
			return true;
		}
	}
	D3D12_HEAP_PROPERTIES Heap = {}; // CPUから書き込む転送領域の置き場。
	Heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC Buffer = {}; // CPUから書き込む転送領域。
	Buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	Buffer.Width = Size;
	Buffer.Height = Buffer.DepthOrArraySize = Buffer.MipLevels = Buffer.SampleDesc.Count = 1;
	Buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	FUploadBuffer Created; // 作った転送領域（開いたままマップする）。
	const D3D12_RANGE NoRead = { 0, 0 }; // CPUからは読まない。
	void *Mapped = nullptr; // CPUが書く先。
	if ( FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Buffer, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &Created.Resource ) ) ) || FAILED( Created.Resource->Map( 0, &NoRead, &Mapped ) ) )
	{
		return false;
	}
	Created.Mapped = static_cast<uint8 *>( Mapped );
	Created.Size = Size;
	Out = MoveTemp( Created );
	return true;
}

bool CDX12TextureUpdater::AcquireRecording( FRecordingSet &Out )
{
	while ( !FreeRecordings.IsEmpty() )
	{
		FRecordingSet Free = FreeRecordings.Pop();
		if ( SUCCEEDED( Free.Allocator->Reset() ) && SUCCEEDED( Free.Commands->Reset( Free.Allocator.Get(), nullptr ) ) )
		{
			Out = MoveTemp( Free );
			return true;
		}
	}
	FRecordingSet Created; // 新しく作る。作った命令列は、記録できる状態。
	if ( FAILED( Device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &Created.Allocator ) ) ) || FAILED( Device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, Created.Allocator.Get(), nullptr, IID_PPV_ARGS( &Created.Commands ) ) ) )
	{
		return false;
	}
	Out = MoveTemp( Created );
	return true;
}

bool CDX12TextureUpdater::Update( int32 Width, int32 Height, const FColor32 *Pixels, FDX12TextureRecord &Out )
{
	if ( !Device || !Queue || !Creator || !Pixels || Width <= 0 || Height <= 0 || UsedRecordings.Num() >= MaxInFlight || UsedUploadBytes >= MaxInFlightBytes )
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
	FUploadBuffer Upload; // 画素を書く転送領域。
	if ( !AcquireUpload( TotalBytes, Upload ) )
	{
		GiveBack( Version, bRecycled );
		return false;
	}
	FRecordingSet Recording; // コピーを記録する領域と命令列。
	if ( !AcquireRecording( Recording ) )
	{
		FreeUploadBytes += static_cast<int64>( Upload.Size );
		FreeUploads.Add( MoveTemp( Upload ) );
		GiveBack( Version, bRecycled );
		return false;
	}
	for ( int32 Row = 0; Row < Height; ++Row )
	{
		FMemory::Memcpy( Upload.Mapped + Layout.Offset + static_cast<usize>( Row ) * Layout.Footprint.RowPitch, Pixels + static_cast<usize>( Row ) * Width, static_cast<usize>( Width ) * sizeof( FColor32 ) );
	}
	ID3D12GraphicsCommandList *Commands = Recording.Commands.Get(); // このコピーだけを記録する命令列。
	if ( bRecycled )
	{
		RecordTransition( Commands, Version.Resource.Get(), ShaderResourceState, D3D12_RESOURCE_STATE_COPY_DEST );
	}
	D3D12_TEXTURE_COPY_LOCATION Source = {}; // paddingを含む転送元。
	Source.pResource = Upload.Resource.Get();
	Source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	Source.PlacedFootprint = Layout;
	D3D12_TEXTURE_COPY_LOCATION Target = {}; // 画像の最初の段へ送る。
	Target.pResource = Version.Resource.Get();
	Target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	Target.SubresourceIndex = 0;
	Commands->CopyTextureRegion( &Target, 0, 0, 0, &Source, nullptr );
	RecordTransition( Commands, Version.Resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, ShaderResourceState ); // 以後はshaderから読む。
	if ( FAILED( Commands->Close() ) )
	{
		FreeUploadBytes += static_cast<int64>( Upload.Size );
		FreeUploads.Add( MoveTemp( Upload ) );
		GiveBack( Version, bRecycled );
		return false;
	}
	ID3D12CommandList *Lists[] = { Commands }; // 完了は待たず、描画の記録先とは別に提出する。
	Queue->ExecuteCommandLists( 1, Lists );
	UsedUploadBytes += static_cast<int64>( Upload.Size );
	UsedUploads.Add( MoveTemp( Upload ) );
	UsedRecordings.Add( MoveTemp( Recording ) );
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
	for ( FUploadBuffer &Upload : UsedUploads )
	{
		if ( FreeUploads.Num() < MaxFreeUploads && FreeUploadBytes + static_cast<int64>( Upload.Size ) <= MaxFreeUploadBytes )
		{
			FreeUploadBytes += static_cast<int64>( Upload.Size );
			FreeUploads.Add( MoveTemp( Upload ) );
		}
	}
	UsedUploads.Reset();
	UsedUploadBytes = 0;
	for ( FRecordingSet &Recording : UsedRecordings )
	{
		if ( FreeRecordings.Num() < MaxFreeRecordings )
		{
			FreeRecordings.Add( MoveTemp( Recording ) );
		}
	}
	UsedRecordings.Reset();
}
