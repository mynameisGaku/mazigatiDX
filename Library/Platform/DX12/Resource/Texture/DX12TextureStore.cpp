// SPDX-License-Identifier: MIT
#include "DX12TextureStore.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Render/RenderResourceSerial.h"
#include "Library/Core/Types/Numeric.h"
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
	bRenderTargetCapabilitiesAvailable = NDX12TextureFormat::GetRenderTargetCapabilities( Device.Get(), RenderTargetCapabilities );
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
	ActiveColorTarget = ActiveDepthTarget = 0;
	bHealthy = false;
	Capabilities = FComputeTextureCapabilities();
	RenderTargetCapabilities = FRenderTargetCapabilities();
	bCapabilitiesAvailable = false;
	bRenderTargetCapabilitiesAvailable = false;
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
	Created.Kind = bWritable ? ETextureKind::ComputeRGBA8 : ETextureKind::StandardRGBA8;
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

bool CDX12TextureStore::GetRenderTargetCapabilities( FRenderTargetCapabilities &Out ) const
{
	if ( !bHealthy || !Device || !bRenderTargetCapabilitiesAvailable || FAILED( Device->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	Out = RenderTargetCapabilities;
	return true;
}

FTextureResourceHandle CDX12TextureStore::CreateRenderTargetRGBA8( int32 Width, int32 Height, FColor32 ClearColor )
{
	if ( !bHealthy || !Device || !Pool || FAILED( Device->GetDeviceRemovedReason() ) || Width <= 0 || Height <= 0 || Width > 16384 || Height > 16384 || static_cast<int64>( Width ) * Height > 64 * 1024 * 1024 || Textures.Num() + Retired.Num() >= MaxResources || !bRenderTargetCapabilitiesAvailable || !RenderTargetCapabilities.bRGBA8RenderTarget )
	{
		return FTextureResourceHandle();
	}
	FTextureRecord Created; // 初期消去とSRV作成が完了するまで登録しない。
	D3D12_HEAP_PROPERTIES Heap = {}; // GPUが描き込み、後続パスが読む領域。
	Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC Desc = {}; // RGBA8の色描画先。
	Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	Desc.Width = Width;
	Desc.Height = Height;
	Desc.DepthOrArraySize = Desc.MipLevels = Desc.SampleDesc.Count = 1;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	D3D12_CLEAR_VALUE ClearValue = {}; // 初期消去色と一致させる最適化値。
	ClearValue.Format = Desc.Format;
	ClearValue.Color[0] = ClearColor.r / 255.0f;
	ClearValue.Color[1] = ClearColor.g / 255.0f;
	ClearValue.Color[2] = ClearColor.b / 255.0f;
	ClearValue.Color[3] = ClearColor.a / 255.0f;
	D3D12_DESCRIPTOR_HEAP_DESC RtvHeapDesc = {}; // この描画先専用のCPU RTV。
	RtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	RtvHeapDesc.NumDescriptors = 1;
	if ( FAILED( Device->CreateDescriptorHeap( &RtvHeapDesc, IID_PPV_ARGS( &Created.TargetHeap ) ) ) || FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &ClearValue, IID_PPV_ARGS( &Created.Resource ) ) ) )
	{
		return FTextureResourceHandle();
	}
	const D3D12_CPU_DESCRIPTOR_HANDLE Rtv = Created.TargetHeap->GetCPUDescriptorHandleForHeapStart(); // 初期消去と描画パスで使うRTV。
	Device->CreateRenderTargetView( Created.Resource.Get(), nullptr, Rtv );
	if ( FAILED( Allocator->Reset() ) || FAILED( Commands->Reset( Allocator.Get(), nullptr ) ) )
	{
		bHealthy = false;
		return FTextureResourceHandle();
	}
	Commands->ClearRenderTargetView( Rtv, ClearValue.Color, 0, nullptr );
	D3D12_RESOURCE_BARRIER Barrier = {}; // 描画パス外では常にSRVとして読める状態に揃える。
	Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Barrier.Transition.pResource = Created.Resource.Get();
	Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	Commands->ResourceBarrier( 1, &Barrier );
	if ( FAILED( Commands->Close() ) )
	{
		bHealthy = false;
		return FTextureResourceHandle();
	}
	PendingTexture = Created.Resource;
	ID3D12CommandList *Lists[] = { Commands.Get() }; // 描画と同じDIRECTキューへ初期消去を提出する。
	Queue->ExecuteCommandLists( 1, Lists );
	bHealthy = Completion.Wait( 10000 );
	if ( !bHealthy )
	{
		return FTextureResourceHandle();
	}
	PendingTexture.Reset();
	if ( !Pool->Allocate( Created.Descriptor ) )
	{
		return FTextureResourceHandle();
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC View = {}; // 通常描画のGetTextureResourceViewで使うSRV。
	View.Format = Desc.Format;
	View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	View.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	View.Texture2D.MipLevels = 1;
	Device->CreateShaderResourceView( Created.Resource.Get(), &View, Created.Descriptor.Cpu );
	Created.Width = Width;
	Created.Height = Height;
	Created.InitialClearColor = ClearColor;
	Created.Kind = ETextureKind::RenderTargetRGBA8;
	const FTextureResourceHandle Handle( NRenderResourceSerial::Allocate() ); // 既存の画像資源と重複しない通し番号。
	if ( !Handle.IsValid() )
	{
		Destroy( Created );
		return FTextureResourceHandle();
	}
	Textures.Add( Handle.Value, MoveTemp( Created ) );
	return Handle;
}

FTextureResourceHandle CDX12TextureStore::CreateDepthTextureD32( int32 Width, int32 Height, float32 ClearDepth )
{
	if ( !bHealthy || !Device || FAILED( Device->GetDeviceRemovedReason() ) || Width <= 0 || Height <= 0 || Width > 16384 || Height > 16384 || static_cast<int64>( Width ) * Height > 64 * 1024 * 1024 || !NMath::IsFinite( ClearDepth ) || ClearDepth < 0.0f || ClearDepth > 1.0f || Textures.Num() + Retired.Num() >= MaxResources || !bRenderTargetCapabilitiesAvailable || !RenderTargetCapabilities.bReadableDepthD32 || !bCapabilitiesAvailable || !Capabilities.bR32FloatRead )
	{
		return FTextureResourceHandle();
	}
	FTextureRecord Created; // 初期消去と遷移が完了するまで登録しない。
	D3D12_HEAP_PROPERTIES Heap = {}; // GPUが深度を書き、computeが読む領域。
	Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC Desc = {}; // DSV(D32_FLOAT)とSRV(R32_FLOAT)を共用するtypeless深度。
	Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	Desc.Width = Width;
	Desc.Height = Height;
	Desc.DepthOrArraySize = Desc.MipLevels = Desc.SampleDesc.Count = 1;
	Desc.Format = DXGI_FORMAT_R32_TYPELESS;
	Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	D3D12_CLEAR_VALUE ClearValue = {}; // 初期深度と一致させる最適化値。
	ClearValue.Format = DXGI_FORMAT_D32_FLOAT;
	ClearValue.DepthStencil.Depth = ClearDepth;
	D3D12_DESCRIPTOR_HEAP_DESC DsvHeapDesc = {}; // この深度資源専用のCPU DSV。
	DsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	DsvHeapDesc.NumDescriptors = 1;
	if ( FAILED( Device->CreateDescriptorHeap( &DsvHeapDesc, IID_PPV_ARGS( &Created.TargetHeap ) ) ) || FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &ClearValue, IID_PPV_ARGS( &Created.Resource ) ) ) )
	{
		return FTextureResourceHandle();
	}
	const D3D12_CPU_DESCRIPTOR_HANDLE Dsv = Created.TargetHeap->GetCPUDescriptorHandleForHeapStart(); // 初期消去と描画パスで使うDSV。
	D3D12_DEPTH_STENCIL_VIEW_DESC DsvDesc = {}; // R32_TYPELESS上にD32_FLOATのDSVを作る。
	DsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	DsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	Device->CreateDepthStencilView( Created.Resource.Get(), &DsvDesc, Dsv );
	if ( FAILED( Allocator->Reset() ) || FAILED( Commands->Reset( Allocator.Get(), nullptr ) ) )
	{
		bHealthy = false;
		return FTextureResourceHandle();
	}
	Commands->ClearDepthStencilView( Dsv, D3D12_CLEAR_FLAG_DEPTH, ClearDepth, 0, 0, nullptr );
	D3D12_RESOURCE_BARRIER Barrier = {}; // 描画パス外では常にSRVとして読める状態に揃える。
	Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Barrier.Transition.pResource = Created.Resource.Get();
	Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	Commands->ResourceBarrier( 1, &Barrier );
	if ( FAILED( Commands->Close() ) )
	{
		bHealthy = false;
		return FTextureResourceHandle();
	}
	PendingTexture = Created.Resource;
	ID3D12CommandList *Lists[] = { Commands.Get() }; // 同じDIRECTキューへ初期深度消去を提出する。
	Queue->ExecuteCommandLists( 1, Lists );
	bHealthy = Completion.Wait( 10000 );
	if ( !bHealthy )
	{
		return FTextureResourceHandle();
	}
	PendingTexture.Reset();
	Created.Width = Width;
	Created.Height = Height;
	Created.InitialClearDepth = ClearDepth;
	Created.Kind = ETextureKind::DepthD32;
	const FTextureResourceHandle Handle( NRenderResourceSerial::Allocate() ); // 既存の画像資源と重複しない通し番号。
	if ( !Handle.IsValid() )
	{
		return FTextureResourceHandle();
	}
	Textures.Add( Handle.Value, MoveTemp( Created ) );
	return Handle;
}

bool CDX12TextureStore::UsesDeviceAndQueue( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue ) const
{
	return Device && Queue && Device.Get() == InDevice && Queue.Get() == InQueue;
}

bool CDX12TextureStore::AcquireComputeTexture( FTextureResourceHandle Texture, EComputeTextureAccess Access, FDX12TextureBinding &Out, EComputeTextureFormat Format ) const
{
	const FTextureRecord *Found = Textures.Find( Texture.Value ); // 現在の版だけを新しい提出へ渡す。
	if ( !bHealthy || !Device || FAILED( Device->GetDeviceRemovedReason() ) || !Found || !bCapabilitiesAvailable || ( ActiveColorTarget != 0 && Texture.Value == ActiveColorTarget ) || ( ActiveDepthTarget != 0 && Texture.Value == ActiveDepthTarget ) || ( Access != EComputeTextureAccess::Read && Access != EComputeTextureAccess::ReadWrite ) )
	{
		return false;
	}
	if ( Format == EComputeTextureFormat::RGBA8 )
	{
		if ( Found->Kind == ETextureKind::DepthD32 || !Capabilities.bRGBA8Read || ( Access == EComputeTextureAccess::ReadWrite && ( !Capabilities.bRGBA8ReadWrite || Found->Kind != ETextureKind::ComputeRGBA8 ) ) )
		{
			return false;
		}
	}
	else if ( Format == EComputeTextureFormat::R32Float )
	{
		if ( Found->Kind != ETextureKind::DepthD32 || Access != EComputeTextureAccess::Read || !Capabilities.bR32FloatRead )
		{
			return false;
		}
	}
	else
	{
		return false;
	}
	FDX12TextureBinding Binding; // 呼び出し元へ渡した後は登録解除・更新から独立して保持する。
	Binding.Resource = Found->Resource;
	Binding.TargetHeap = Found->TargetHeap;
	Binding.SrvGpuDescriptor = Found->Descriptor.Gpu.ptr;
	Binding.Width = Found->Width;
	Binding.Height = Found->Height;
	Binding.InitialClearColor = Found->InitialClearColor;
	Binding.InitialClearDepth = Found->InitialClearDepth;
	Binding.Format = Format;
	Out = MoveTemp( Binding );
	return true;
}

bool CDX12TextureStore::BeginRenderTargetPass( FTextureResourceHandle ColorTarget, FTextureResourceHandle DepthTarget, FDX12TextureBinding &OutColor, FDX12TextureBinding &OutDepth )
{
	if ( !bHealthy || !Device || FAILED( Device->GetDeviceRemovedReason() ) || ActiveColorTarget != 0 || ActiveDepthTarget != 0 || !ColorTarget.IsValid() )
	{
		return false;
	}
	const FTextureRecord *Color = Textures.Find( ColorTarget.Value ); // 書き込み先の色描画先。
	if ( !Color || Color->Kind != ETextureKind::RenderTargetRGBA8 || !Color->Resource || !Color->TargetHeap )
	{
		return false;
	}
	FDX12TextureBinding DepthBinding; // 任意の読み取れる深度。
	if ( DepthTarget.IsValid() )
	{
		if ( DepthTarget.Value == ColorTarget.Value )
		{
			return false;
		}
		const FTextureRecord *Depth = Textures.Find( DepthTarget.Value ); // 書き込み先の深度。
		if ( !Depth || Depth->Kind != ETextureKind::DepthD32 || !Depth->Resource || !Depth->TargetHeap || Depth->Width != Color->Width || Depth->Height != Color->Height )
		{
			return false;
		}
		DepthBinding.Resource = Depth->Resource;
		DepthBinding.TargetHeap = Depth->TargetHeap;
		DepthBinding.Width = Depth->Width;
		DepthBinding.Height = Depth->Height;
		DepthBinding.InitialClearDepth = Depth->InitialClearDepth;
		DepthBinding.Format = EComputeTextureFormat::R32Float;
	}
	FDX12TextureBinding ColorBinding; // GPU完了まで保持する色描画先の実体。
	ColorBinding.Resource = Color->Resource;
	ColorBinding.TargetHeap = Color->TargetHeap;
	ColorBinding.SrvGpuDescriptor = Color->Descriptor.Gpu.ptr;
	ColorBinding.Width = Color->Width;
	ColorBinding.Height = Color->Height;
	ColorBinding.InitialClearColor = Color->InitialClearColor;
	ColorBinding.Format = EComputeTextureFormat::RGBA8;
	ActiveColorTarget = ColorTarget.Value;
	ActiveDepthTarget = DepthTarget.IsValid() ? DepthTarget.Value : 0;
	OutColor = MoveTemp( ColorBinding );
	OutDepth = MoveTemp( DepthBinding );
	return true;
}

void CDX12TextureStore::EndRenderTargetPass()
{
	ActiveColorTarget = 0;
	ActiveDepthTarget = 0;
}

bool CDX12TextureStore::IsRenderTargetPassActive() const
{
	return ActiveColorTarget != 0 || ActiveDepthTarget != 0;
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
	auto *Current = Textures.Find( Texture.Value ); // 有効な同寸法の通常画像またはcompute画像だけを更新する。
	if ( !Current || ( Current->Kind != ETextureKind::StandardRGBA8 && Current->Kind != ETextureKind::ComputeRGBA8 ) || Current->Width != Width || Current->Height != Height )
	{
		return false;
	}
	FTextureRecord Created; // 失敗時は現在の内容とSRVを保つ。
	if ( !Upload( Width, Height, Pixels, Current->Kind == ETextureKind::ComputeRGBA8, Created ) )
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
	const auto *Found = Textures.Find( Texture.Value ); // 解放済み・深度・現在書き込み中の番号は描画viewとして貸さない。
	if ( !Found || Found->Kind == ETextureKind::DepthD32 || Found->Descriptor.Serial == 0 || ( ActiveColorTarget != 0 && Texture.Value == ActiveColorTarget ) )
	{
		return false;
	}
	Out = { Found->Descriptor.Gpu.ptr, Found->Width, Found->Height };
	return true;
}
