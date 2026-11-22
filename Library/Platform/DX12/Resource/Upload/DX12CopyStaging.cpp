// SPDX-License-Identifier: MIT
#include "DX12CopyStaging.h"
#include "Library/Core/Memory/Memory.h"

void CDX12CopyStaging::Connect( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue )
{
	Device = InDevice;
	Queue = InQueue;
}

void CDX12CopyStaging::Disconnect()
{
	FreeUploads.Reset();
	FreeRecordings.Reset();
	UsedUploads.Reset();
	UsedRecordings.Reset();
	OpenUpload = FUploadBuffer();
	OpenRecording = FRecordingSet();
	bOpen = false;
	FreeUploadBytes = 0;
	UsedUploadBytes = 0;
	Device = nullptr;
	Queue = nullptr;
}

bool CDX12CopyStaging::AcquireUpload( uint64 Size, FUploadBuffer &Out )
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

bool CDX12CopyStaging::AcquireRecording( FRecordingSet &Out )
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

void CDX12CopyStaging::ReturnUpload( FUploadBuffer &Upload )
{
	if ( Upload.Resource && FreeUploads.Num() < MaxFreeUploads && FreeUploadBytes + static_cast<int64>( Upload.Size ) <= MaxFreeUploadBytes )
	{
		FreeUploadBytes += static_cast<int64>( Upload.Size );
		FreeUploads.Add( MoveTemp( Upload ) );
	}
	Upload = FUploadBuffer();
}

bool CDX12CopyStaging::Open( uint64 Size, FOpened &Out )
{
	if ( !Device || !Queue || bOpen || Size == 0 || UsedRecordings.Num() >= MaxInFlight || UsedUploadBytes >= MaxInFlightBytes )
	{
		return false;
	}
	FUploadBuffer Upload; // 画素やバイト列を書く転送領域。
	if ( !AcquireUpload( Size, Upload ) )
	{
		return false;
	}
	FRecordingSet Recording; // コピーを記録する領域と命令列。
	if ( !AcquireRecording( Recording ) )
	{
		ReturnUpload( Upload );
		return false;
	}
	OpenUpload = MoveTemp( Upload );
	OpenRecording = MoveTemp( Recording );
	bOpen = true;
	Out.Upload = OpenUpload.Resource.Get();
	Out.Mapped = OpenUpload.Mapped;
	Out.Commands = OpenRecording.Commands.Get();
	return true;
}

bool CDX12CopyStaging::Submit()
{
	if ( !bOpen )
	{
		return false;
	}
	if ( FAILED( OpenRecording.Commands->Close() ) )
	{
		Cancel();
		return false;
	}
	ID3D12CommandList *Lists[] = { OpenRecording.Commands.Get() }; // 完了は待たず、描画の記録先とは別に提出する。
	Queue->ExecuteCommandLists( 1, Lists );
	UsedUploadBytes += static_cast<int64>( OpenUpload.Size );
	UsedUploads.Add( MoveTemp( OpenUpload ) );
	UsedRecordings.Add( MoveTemp( OpenRecording ) );
	OpenUpload = FUploadBuffer();
	OpenRecording = FRecordingSet();
	bOpen = false;
	return true;
}

void CDX12CopyStaging::Cancel()
{
	if ( !bOpen )
	{
		return;
	}
	ReturnUpload( OpenUpload ); // 命令列は、閉じていないものも、捨てる（Resetできる領域として戻さない）。
	OpenRecording = FRecordingSet();
	bOpen = false;
}

void CDX12CopyStaging::CollectAfterGPUIdle()
{
	for ( FUploadBuffer &Upload : UsedUploads )
	{
		ReturnUpload( Upload );
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
