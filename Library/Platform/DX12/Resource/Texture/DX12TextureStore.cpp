// SPDX-License-Identifier: MIT
#include "DX12TextureStore.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Render/RenderResourceSerial.h"
#include "DX12TextureFormat.h"

CDX12TextureStore::~CDX12TextureStore()
{
	ResetAfterGPUIdle();
}

bool CDX12TextureStore::Initialize( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue, CDX12DescriptorPool &InPool )
{
	if ( Device || !InDevice || !InQueue || !InPool.GetHeap() || InQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT )
	{
		return false;
	}
	Microsoft::WRL::ComPtr<ID3D12Device> QueueDevice, HeapDevice; // 混在したデバイスへ資源を登録しない。
	if ( FAILED( InQueue->GetDevice( IID_PPV_ARGS( &QueueDevice ) ) ) || FAILED( InPool.GetHeap()->GetDevice( IID_PPV_ARGS( &HeapDevice ) ) ) || QueueDevice.Get() != InDevice || HeapDevice.Get() != InDevice )
	{
		return false;
	}
	Device = InDevice;
	Queue = InQueue;
	Pool = &InPool;
	if ( FAILED( Device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &Allocator ) ) ) || FAILED( Device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.Get(), nullptr, IID_PPV_ARGS( &Commands ) ) ) || FAILED( Commands->Close() ) || !Completion.Initialize( Device.Get(), Queue.Get() ) )
	{
		ResetAfterGPUIdle();
		return false;
	}
	bCapabilitiesAvailable = NDX12TextureFormat::GetComputeCapabilities( Device.Get(), Capabilities );
	bHealthy = true;
	return true;
}

void CDX12TextureStore::Destroy( FTextureRecord &Texture )
{
	if ( Pool && Texture.Descriptor.Serial != 0 )
	{
		Pool->Release( Texture.Descriptor );
	}
	Texture = FTextureRecord();
}

void CDX12TextureStore::CollectAfterGPUIdle()
{
	for ( auto &Texture : Retired )
	{
		Destroy( Texture );
	}
	Retired.Reset();
	PendingUpload.Reset();
	PendingTexture.Reset();
	bHealthy = Device && SUCCEEDED( Device->GetDeviceRemovedReason() );
}

void CDX12TextureStore::ResetAfterGPUIdle()
{
	CollectAfterGPUIdle();
	for ( auto &Pair : Textures )
	{
		Destroy( Pair.Value );
	}
	Textures.Reset();
	Commands.Reset();
	Allocator.Reset();
	Completion.ResetAfterGPUIdle();
	Queue.Reset();
	Device.Reset();
	Pool = nullptr;
	bHealthy = false;
	Capabilities = FComputeTextureCapabilities();
	bCapabilitiesAvailable = false;
}

bool CDX12TextureStore::Upload( int32 Width, int32 Height, const FColor32 *Pixels, bool bWritable, FTextureRecord &Out )
{
	if ( !bHealthy || !Device || !Pool || FAILED( Device->GetDeviceRemovedReason() ) || !Pixels || Width <= 0 || Height <= 0 || Width > 16384 || Height > 16384 || static_cast<int64>( Width ) * Height > 64 * 1024 * 1024 || Textures.Num() + Retired.Num() >= MaxResources || ( bWritable && ( !bCapabilitiesAvailable || !Capabilities.bRGBA8ReadWrite ) ) )
	{
		return false;
	}
	FTextureRecord Created; // 転送完了まで外へ公開しない画像。
	D3D12_HEAP_PROPERTIES Heap = {}; // GPUが読む画像の置き場。
	Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC Desc = {}; // RGBA8の二次元画像。
	Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	Desc.Width = Width;
	Desc.Height = Height;
	Desc.DepthOrArraySize = Desc.MipLevels = Desc.SampleDesc.Count = 1;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.Flags = bWritable ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
	if ( FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &Created.Resource ) ) ) )
	{
		return false;
	}
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT Layout = {}; // GPUが必要とする行の境界と幅。
	UINT64 TotalBytes = 0; // paddingを含む転送領域の大きさ。
	Device->GetCopyableFootprints( &Desc, 0, 1, 0, &Layout, nullptr, nullptr, &TotalBytes );
	D3D12_RESOURCE_DESC Buffer = {}; // CPUから書き込む転送領域。
	Buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	Buffer.Width = TotalBytes;
	Buffer.Height = Buffer.DepthOrArraySize = Buffer.MipLevels = Buffer.SampleDesc.Count = 1;
	Buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	Heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	Microsoft::WRL::ComPtr<ID3D12Resource> UploadBuffer; // 転送の完了まで所有する。
	void *Mapped = nullptr; // CPUが書く先。
	const D3D12_RANGE NoRead = { 0, 0 }; // CPUからは読まない。
	if ( FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Buffer, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &UploadBuffer ) ) ) || FAILED( UploadBuffer->Map( 0, &NoRead, &Mapped ) ) )
	{
		return false;
	}
	for ( int32 Row = 0; Row < Height; ++Row )
	{
		FMemory::Memcpy( static_cast<uint8 *>( Mapped ) + Layout.Offset + static_cast<usize>( Row ) * Layout.Footprint.RowPitch, Pixels + static_cast<usize>( Row ) * Width, static_cast<usize>( Width ) * sizeof( FColor32 ) );
	}
	UploadBuffer->Unmap( 0, nullptr );
	if ( FAILED( Allocator->Reset() ) || FAILED( Commands->Reset( Allocator.Get(), nullptr ) ) )
	{
		bHealthy = false;
		return false;
	}
	D3D12_TEXTURE_COPY_LOCATION Source = {}; // paddingを含む転送元。
	Source.pResource = UploadBuffer.Get();
	Source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	Source.PlacedFootprint = Layout;
	D3D12_TEXTURE_COPY_LOCATION Target = {}; // 画像の最初のmipへ送る。
	Target.pResource = Created.Resource.Get();
	Target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	Commands->CopyTextureRegion( &Target, 0, 0, 0, &Source, nullptr );
	D3D12_RESOURCE_BARRIER Barrier = {}; // 以後はshaderから読む。
	Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Barrier.Transition.pResource = Created.Resource.Get();
	Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	Commands->ResourceBarrier( 1, &Barrier );
	if ( FAILED( Commands->Close() ) )
	{
		bHealthy = false;
		return false;
	}
	PendingTexture = Created.Resource;
	PendingUpload = UploadBuffer;
	ID3D12CommandList *Lists[] = { Commands.Get() }; // 描画の記録先とは別に提出する。
	Queue->ExecuteCommandLists( 1, Lists );
	bHealthy = Completion.Wait( 10000 );
	if ( !bHealthy )
	{
		return false;
	}
	PendingTexture.Reset();
	PendingUpload.Reset();
	if ( !Pool->Allocate( Created.Descriptor ) )
	{
		return false;
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC View = {}; // 画像の記述子もこの実装が所有する。
	View.Format = Desc.Format;
	View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	View.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	View.Texture2D.MipLevels = 1;
	Device->CreateShaderResourceView( Created.Resource.Get(), &View, Created.Descriptor.Cpu );
	Created.Width = Width;
	Created.Height = Height;
	Created.bWritable = bWritable;
	Out = MoveTemp( Created );
	return true;
}

FTextureResourceHandle CDX12TextureStore::CreateTextureRGBA8( int32 Width, int32 Height, const FColor32 *Pixels )
{
	return Create( Width, Height, Pixels, false );
}

bool CDX12TextureStore::GetComputeTextureCapabilities( FComputeTextureCapabilities &Out ) const
{
	if ( !bHealthy || !Device || !bCapabilitiesAvailable || FAILED( Device->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	Out = Capabilities;
	return true;
}

FTextureResourceHandle CDX12TextureStore::CreateComputeTextureRGBA8( int32 Width, int32 Height, const FColor32 *Pixels )
{
	return Create( Width, Height, Pixels, true );
}

bool CDX12TextureStore::UsesDeviceAndQueue( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue ) const
{
	return Device && Queue && Device.Get() == InDevice && Queue.Get() == InQueue;
}

bool CDX12TextureStore::AcquireComputeTexture( FTextureResourceHandle Texture, EComputeTextureAccess Access, FDX12TextureBinding &Out ) const
{
	const FTextureRecord *Found = Textures.Find( Texture.Value ); // 現在の版だけを新しい提出へ渡す。
	if ( !bHealthy || !Device || FAILED( Device->GetDeviceRemovedReason() ) || !Found || !bCapabilitiesAvailable || !Capabilities.bRGBA8Read || ( Access != EComputeTextureAccess::Read && Access != EComputeTextureAccess::ReadWrite ) || ( Access == EComputeTextureAccess::ReadWrite && ( !Capabilities.bRGBA8ReadWrite || !Found->bWritable ) ) )
	{
		return false;
	}
	FDX12TextureBinding Binding; // 呼び出し元へ渡した後は登録解除・更新から独立して保持する。
	Binding.Resource = Found->Resource;
	Binding.Width = Found->Width;
	Binding.Height = Found->Height;
	Out = MoveTemp( Binding );
	return true;
}

FTextureResourceHandle CDX12TextureStore::Create( int32 Width, int32 Height, const FColor32 *Pixels, bool bWritable )
{
	FTextureRecord Created; // 完成した画像だけを登録する。
	if ( !Upload( Width, Height, Pixels, bWritable, Created ) )
	{
		return FTextureResourceHandle();
	}
	const FTextureResourceHandle Handle( NRenderResourceSerial::Allocate() ); // 別の所有先・再初期化後とも重ならない。
	if ( !Handle.IsValid() )
	{
		Destroy( Created );
		return FTextureResourceHandle();
	}
	Textures.Add( Handle.Value, MoveTemp( Created ) );
	return Handle;
}

bool CDX12TextureStore::UpdateTextureRGBA8( FTextureResourceHandle Texture, int32 Width, int32 Height, const FColor32 *Pixels )
{
	auto *Current = Textures.Find( Texture.Value ); // 有効な同寸法の画像だけを更新する。
	if ( !Current || Current->Width != Width || Current->Height != Height )
	{
		return false;
	}
	FTextureRecord Created; // 失敗時は現在の内容とSRVを保つ。
	if ( !Upload( Width, Height, Pixels, Current->bWritable, Created ) )
	{
		return false;
	}
	Retired.Add( MoveTemp( *Current ) );
	*Current = MoveTemp( Created );
	return true;
}

void CDX12TextureStore::ReleaseTexture( FTextureResourceHandle Texture )
{
	if ( auto *Current = Textures.Find( Texture.Value ) )
	{
		Retired.Add( MoveTemp( *Current ) );
		Textures.Remove( Texture.Value );
	}
}

bool CDX12TextureStore::GetTextureResourceView( FTextureResourceHandle Texture, FTextureDrawView &Out ) const
{
	const auto *Found = Textures.Find( Texture.Value ); // 解放済みの番号はもう見えない。
	if ( !Found )
	{
		return false;
	}
	Out = { Found->Descriptor.Gpu.ptr, Found->Width, Found->Height };
	return true;
}
