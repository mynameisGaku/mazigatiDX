// SPDX-License-Identifier: MIT
#include "DX12TextureStore.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Render/RenderResourceSerial.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Math/Math.h"
#include "DX12TextureFormat.h"

namespace
{
	// IEEE 754 単精度を半精度（binary16、round-to-nearest-even）へ変換する。
	uint16 Float32ToHalf( float32 Value )
	{
		uint32 Bits = 0;
		FMemory::Memcpy( &Bits, &Value, sizeof( Bits ) );
		const uint32 Sign = ( Bits >> 16 ) & 0x8000u;
		const int32 Exp = static_cast<int32>( ( Bits >> 23 ) & 0xFFu ) - 127 + 15;
		uint32 Mant = Bits & 0x007FFFFFu;
		if ( ( Bits & 0x7FFFFFFFu ) == 0 )
		{
			return static_cast<uint16>( Sign );
		}
		if ( Exp >= 31 )
		{
			return static_cast<uint16>( Sign | 0x7C00u );
		}
		if ( Exp <= 0 )
		{
			if ( Exp < -10 )
			{
				return static_cast<uint16>( Sign );
			}
			Mant |= 0x00800000u;
			const uint32 Shift = static_cast<uint32>( 14 - Exp );
			uint32 HalfMant = Mant >> Shift;
			const uint32 Remainder = Mant & ( ( 1u << Shift ) - 1u );
			const uint32 Halfway = 1u << ( Shift - 1u );
			if ( Remainder > Halfway || ( Remainder == Halfway && ( HalfMant & 1u ) != 0 ) )
			{
				++HalfMant;
			}
			return static_cast<uint16>( Sign | HalfMant );
		}
		uint32 Half = Sign | ( static_cast<uint32>( Exp ) << 10 ) | ( Mant >> 13 );
		const uint32 RoundBits = Mant & 0x1FFFu;
		if ( RoundBits > 0x1000u || ( RoundBits == 0x1000u && ( Half & 1u ) != 0 ) )
		{
			++Half;
		}
		return static_cast<uint16>( Half );
	}

	// IEEE 754 半精度（binary16）を単精度へ変換する。
	float32 HalfToFloat32( uint16 H )
	{
		const uint32 Sign = static_cast<uint32>( H & 0x8000u ) << 16;
		const uint32 Exp = ( H >> 10 ) & 0x1Fu;
		const uint32 Mant = H & 0x03FFu;
		uint32 Bits = 0;
		if ( Exp == 0 )
		{
			if ( Mant == 0 )
			{
				Bits = Sign;
			}
			else
			{
				uint32 M = Mant;
				int32 E = 127 - 14;
				while ( ( M & 0x0400u ) == 0 )
				{
					M <<= 1;
					--E;
				}
				M &= 0x03FFu;
				Bits = Sign | ( static_cast<uint32>( E ) << 23 ) | ( M << 13 );
			}
		}
		else if ( Exp == 31 )
		{
			Bits = Sign | 0x7F800000u | ( Mant << 13 );
		}
		else
		{
			Bits = Sign | ( ( Exp + ( 127 - 15 ) ) << 23 ) | ( Mant << 13 );
		}
		float32 Out = 0.0f;
		FMemory::Memcpy( &Out, &Bits, sizeof( Out ) );
		return Out;
	}

	// RGBA16F入力の4成分がすべて有限値か検査する。
	bool IsFiniteColor( const FColor &C )
	{
		return NMath::IsFinite( C.r ) && NMath::IsFinite( C.g ) && NMath::IsFinite( C.b ) && NMath::IsFinite( C.a );
	}
} // namespace

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
	EndRenderTargetPass();
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

bool CDX12TextureStore::UploadRGBA16F( int32 Width, int32 Height, const FColor *Pixels, FTextureRecord &Out )
{
	if ( !bHealthy || !Device || !Pool || FAILED( Device->GetDeviceRemovedReason() ) || !Pixels || Width <= 0 || Height <= 0 || Width > 16384 || Height > 16384 || static_cast<int64>( Width ) * Height > 64 * 1024 * 1024 || Textures.Num() + Retired.Num() >= MaxResources || !bCapabilitiesAvailable || !Capabilities.bRGBA16FReadWrite )
	{
		return false;
	}
	const usize PixelCount = static_cast<usize>( Width ) * static_cast<usize>( Height );
	for ( usize i = 0; i < PixelCount; ++i )
	{
		if ( !IsFiniteColor( Pixels[i] ) )
		{
			return false;
		}
	}
	FTextureRecord Created; // 転送完了まで外へ公開しないRGBA16F画像。
	D3D12_HEAP_PROPERTIES Heap = {};
	Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC Desc = {};
	Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	Desc.Width = Width;
	Desc.Height = Height;
	Desc.DepthOrArraySize = Desc.MipLevels = Desc.SampleDesc.Count = 1;
	Desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	if ( FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &Created.Resource ) ) ) )
	{
		return false;
	}
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT Layout = {};
	UINT64 TotalBytes = 0;
	Device->GetCopyableFootprints( &Desc, 0, 1, 0, &Layout, nullptr, nullptr, &TotalBytes );
	D3D12_RESOURCE_DESC Buffer = {};
	Buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	Buffer.Width = TotalBytes;
	Buffer.Height = Buffer.DepthOrArraySize = Buffer.MipLevels = Buffer.SampleDesc.Count = 1;
	Buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	Heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	Microsoft::WRL::ComPtr<ID3D12Resource> UploadBuffer;
	void *Mapped = nullptr;
	const D3D12_RANGE NoRead = { 0, 0 };
	if ( FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Buffer, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &UploadBuffer ) ) ) || FAILED( UploadBuffer->Map( 0, &NoRead, &Mapped ) ) )
	{
		return false;
	}
	for ( int32 Row = 0; Row < Height; ++Row )
	{
		uint16 *DstHalf = reinterpret_cast<uint16 *>( static_cast<uint8 *>( Mapped ) + Layout.Offset + static_cast<usize>( Row ) * Layout.Footprint.RowPitch );
		const FColor *SrcRow = Pixels + static_cast<usize>( Row ) * Width;
		for ( int32 Col = 0; Col < Width; ++Col )
		{
			DstHalf[Col * 4 + 0] = Float32ToHalf( SrcRow[Col].r );
			DstHalf[Col * 4 + 1] = Float32ToHalf( SrcRow[Col].g );
			DstHalf[Col * 4 + 2] = Float32ToHalf( SrcRow[Col].b );
			DstHalf[Col * 4 + 3] = Float32ToHalf( SrcRow[Col].a );
		}
	}
	UploadBuffer->Unmap( 0, nullptr );
	if ( FAILED( Allocator->Reset() ) || FAILED( Commands->Reset( Allocator.Get(), nullptr ) ) )
	{
		bHealthy = false;
		return false;
	}
	D3D12_TEXTURE_COPY_LOCATION Source = {};
	Source.pResource = UploadBuffer.Get();
	Source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	Source.PlacedFootprint = Layout;
	D3D12_TEXTURE_COPY_LOCATION Target = {};
	Target.pResource = Created.Resource.Get();
	Target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	Commands->CopyTextureRegion( &Target, 0, 0, 0, &Source, nullptr );
	D3D12_RESOURCE_BARRIER Barrier = {};
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
	ID3D12CommandList *Lists[] = { Commands.Get() };
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
	D3D12_SHADER_RESOURCE_VIEW_DESC View = {};
	View.Format = Desc.Format;
	View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	View.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	View.Texture2D.MipLevels = 1;
	Device->CreateShaderResourceView( Created.Resource.Get(), &View, Created.Descriptor.Cpu );
	Created.Width = Width;
	Created.Height = Height;
	Created.Kind = ETextureKind::ComputeRGBA16F;
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

FTextureResourceHandle CDX12TextureStore::CreateComputeTextureRGBA16F( int32 Width, int32 Height, const FColor *Pixels )
{
	FTextureRecord Created; // 完成したRGBA16F compute画像だけを登録する。
	if ( !UploadRGBA16F( Width, Height, Pixels, Created ) )
	{
		return FTextureResourceHandle();
	}
	const FTextureResourceHandle Handle( NRenderResourceSerial::Allocate() );
	if ( !Handle.IsValid() )
	{
		Destroy( Created );
		return FTextureResourceHandle();
	}
	Textures.Add( Handle.Value, MoveTemp( Created ) );
	return Handle;
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

FTextureResourceHandle CDX12TextureStore::CreateFloatColorRenderTarget( int32 Width, int32 Height, DXGI_FORMAT Format, const float32 ClearValues[4], ETextureKind Kind )
{
	if ( !bHealthy || !Device || !Pool || FAILED( Device->GetDeviceRemovedReason() ) || Width <= 0 || Height <= 0 || Width > 16384 || Height > 16384 || static_cast<int64>( Width ) * Height > 64 * 1024 * 1024 || Textures.Num() + Retired.Num() >= MaxResources )
	{
		return FTextureResourceHandle();
	}
	FTextureRecord Created; // 初期消去とSRV作成が完了するまで登録しない。
	D3D12_HEAP_PROPERTIES Heap = {};
	Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC Desc = {};
	Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	Desc.Width = Width;
	Desc.Height = Height;
	Desc.DepthOrArraySize = Desc.MipLevels = Desc.SampleDesc.Count = 1;
	Desc.Format = Format;
	Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	D3D12_CLEAR_VALUE ClearValue = {};
	ClearValue.Format = Desc.Format;
	ClearValue.Color[0] = ClearValues[0];
	ClearValue.Color[1] = ClearValues[1];
	ClearValue.Color[2] = ClearValues[2];
	ClearValue.Color[3] = ClearValues[3];
	D3D12_DESCRIPTOR_HEAP_DESC RtvHeapDesc = {};
	RtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	RtvHeapDesc.NumDescriptors = 1;
	if ( FAILED( Device->CreateDescriptorHeap( &RtvHeapDesc, IID_PPV_ARGS( &Created.TargetHeap ) ) ) || FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &ClearValue, IID_PPV_ARGS( &Created.Resource ) ) ) )
	{
		return FTextureResourceHandle();
	}
	const D3D12_CPU_DESCRIPTOR_HANDLE Rtv = Created.TargetHeap->GetCPUDescriptorHandleForHeapStart();
	Device->CreateRenderTargetView( Created.Resource.Get(), nullptr, Rtv );
	if ( FAILED( Allocator->Reset() ) || FAILED( Commands->Reset( Allocator.Get(), nullptr ) ) )
	{
		bHealthy = false;
		return FTextureResourceHandle();
	}
	Commands->ClearRenderTargetView( Rtv, ClearValue.Color, 0, nullptr );
	D3D12_RESOURCE_BARRIER Barrier = {};
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
	ID3D12CommandList *Lists[] = { Commands.Get() };
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
	D3D12_SHADER_RESOURCE_VIEW_DESC View = {};
	View.Format = Desc.Format;
	View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	View.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	View.Texture2D.MipLevels = 1;
	Device->CreateShaderResourceView( Created.Resource.Get(), &View, Created.Descriptor.Cpu );
	Created.Width = Width;
	Created.Height = Height;
	Created.InitialClearColorF = FColor( ClearValues[0], ClearValues[1], ClearValues[2], ClearValues[3] );
	Created.Kind = Kind;
	const FTextureResourceHandle Handle( NRenderResourceSerial::Allocate() );
	if ( !Handle.IsValid() )
	{
		Destroy( Created );
		return FTextureResourceHandle();
	}
	Textures.Add( Handle.Value, MoveTemp( Created ) );
	return Handle;
}

FTextureResourceHandle CDX12TextureStore::CreateRenderTargetRGBA16F( int32 Width, int32 Height, FColor ClearColor )
{
	if ( !IsFiniteColor( ClearColor ) || !bRenderTargetCapabilitiesAvailable || !RenderTargetCapabilities.bRGBA16FRenderTarget )
	{
		return FTextureResourceHandle();
	}
	const float32 ClearValues[4] = { ClearColor.r, ClearColor.g, ClearColor.b, ClearColor.a };
	return CreateFloatColorRenderTarget( Width, Height, DXGI_FORMAT_R16G16B16A16_FLOAT, ClearValues, ETextureKind::RenderTargetRGBA16F );
}

FTextureResourceHandle CDX12TextureStore::CreateRenderTargetR32Float( int32 Width, int32 Height, float32 ClearValue )
{
	if ( !NMath::IsFinite( ClearValue ) || !bRenderTargetCapabilitiesAvailable || !RenderTargetCapabilities.bR32FloatRenderTarget )
	{
		return FTextureResourceHandle();
	}
	const float32 ClearValues[4] = { ClearValue, ClearValue, ClearValue, ClearValue };
	return CreateFloatColorRenderTarget( Width, Height, DXGI_FORMAT_R32_FLOAT, ClearValues, ETextureKind::RenderTargetR32Float );
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
	if ( !Pool || !Pool->Allocate( Created.Descriptor ) )
	{
		return FTextureResourceHandle();
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC Srv = {}; // カスタムグラフィックスおよびcompute読み取り用R32_FLOAT SRV。
	Srv.Format = DXGI_FORMAT_R32_FLOAT;
	Srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	Srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	Srv.Texture2D.MipLevels = 1;
	Device->CreateShaderResourceView( Created.Resource.Get(), &Srv, Created.Descriptor.Cpu );
	Created.Width = Width;
	Created.Height = Height;
	Created.InitialClearDepth = ClearDepth;
	Created.Kind = ETextureKind::DepthD32;
	const FTextureResourceHandle Handle( NRenderResourceSerial::Allocate() ); // 既存の画像資源と重複しない通し番号。
	if ( !Handle.IsValid() )
	{
		Destroy( Created );
		return FTextureResourceHandle();
	}
	Textures.Add( Handle.Value, MoveTemp( Created ) );
	return Handle;
}

bool CDX12TextureStore::UsesDeviceAndQueue( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue ) const
{
	return Device && Queue && Device.Get() == InDevice && Queue.Get() == InQueue;
}

bool CDX12TextureStore::IsActiveTarget( uint64 TextureValue ) const
{
	if ( TextureValue == 0 )
	{
		return false;
	}
	if ( ActiveDepthTarget != 0 && TextureValue == ActiveDepthTarget )
	{
		return true;
	}
	for ( uint32 i = 0; i < ActiveColorTargetCount; ++i )
	{
		if ( ActiveColorTargets[i] == TextureValue )
		{
			return true;
		}
	}
	return false;
}

bool CDX12TextureStore::AcquireComputeTexture( FTextureResourceHandle Texture, EComputeTextureAccess Access, FDX12TextureBinding &Out, EComputeTextureFormat Format ) const
{
	const FTextureRecord *Found = Textures.Find( Texture.Value ); // 現在の版だけを新しい提出へ渡す。
	if ( !bHealthy || !Device || FAILED( Device->GetDeviceRemovedReason() ) || !Found || !bCapabilitiesAvailable || IsActiveTarget( Texture.Value ) || ( Access != EComputeTextureAccess::Read && Access != EComputeTextureAccess::ReadWrite ) )
	{
		return false;
	}
	if ( Format == EComputeTextureFormat::RGBA8 )
	{
		const bool bIsRGBA8 = Found->Kind == ETextureKind::StandardRGBA8 || Found->Kind == ETextureKind::ComputeRGBA8 || Found->Kind == ETextureKind::RenderTargetRGBA8;
		if ( !bIsRGBA8 || !Capabilities.bRGBA8Read || ( Access == EComputeTextureAccess::ReadWrite && ( !Capabilities.bRGBA8ReadWrite || Found->Kind != ETextureKind::ComputeRGBA8 ) ) )
		{
			return false;
		}
	}
	else if ( Format == EComputeTextureFormat::RGBA16F )
	{
		const bool bIsRGBA16F = Found->Kind == ETextureKind::ComputeRGBA16F || Found->Kind == ETextureKind::RenderTargetRGBA16F;
		if ( !bIsRGBA16F || !Capabilities.bRGBA16FRead || ( Access == EComputeTextureAccess::ReadWrite && ( !Capabilities.bRGBA16FReadWrite || Found->Kind != ETextureKind::ComputeRGBA16F ) ) )
		{
			return false;
		}
	}
	else if ( Format == EComputeTextureFormat::R32Float )
	{
		const bool bIsR32Float = Found->Kind == ETextureKind::DepthD32 || Found->Kind == ETextureKind::RenderTargetR32Float;
		if ( !bIsR32Float || Access != EComputeTextureAccess::Read || !Capabilities.bR32FloatRead )
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
	Binding.InitialClearColorF = Found->InitialClearColorF;
	Binding.InitialClearDepth = Found->InitialClearDepth;
	Binding.Format = Format;
	Out = MoveTemp( Binding );
	return true;
}

bool CDX12TextureStore::AcquireGraphicsTexture( FTextureResourceHandle Texture, EComputeTextureFormat Format, FDX12TextureBinding &Out ) const
{
	const FTextureRecord *Found = Textures.Find( Texture.Value ); // パス書き込み中のターゲットや解放済み番号は読ませない。
	if ( !bHealthy || !Device || FAILED( Device->GetDeviceRemovedReason() ) || !Found || IsActiveTarget( Texture.Value ) || Found->Descriptor.Serial == 0 || Found->Descriptor.Gpu.ptr == 0 )
	{
		return false;
	}
	if ( Format == EComputeTextureFormat::RGBA8 )
	{
		const bool bIsRGBA8 = Found->Kind == ETextureKind::StandardRGBA8 || Found->Kind == ETextureKind::ComputeRGBA8 || Found->Kind == ETextureKind::RenderTargetRGBA8;
		if ( !bIsRGBA8 )
		{
			return false;
		}
	}
	else if ( Format == EComputeTextureFormat::RGBA16F )
	{
		const bool bIsRGBA16F = Found->Kind == ETextureKind::ComputeRGBA16F || Found->Kind == ETextureKind::RenderTargetRGBA16F;
		if ( !bIsRGBA16F )
		{
			return false;
		}
	}
	else if ( Format == EComputeTextureFormat::R32Float )
	{
		const bool bIsR32Float = Found->Kind == ETextureKind::DepthD32 || Found->Kind == ETextureKind::RenderTargetR32Float;
		if ( !bIsR32Float || !bCapabilitiesAvailable || !Capabilities.bR32FloatRead )
		{
			return false;
		}
	}
	else
	{
		return false;
	}
	FDX12TextureBinding Binding; // 描画終了のGPU完了まで保持する参照。
	Binding.Resource = Found->Resource;
	Binding.TargetHeap = Found->TargetHeap;
	Binding.SrvGpuDescriptor = Found->Descriptor.Gpu.ptr;
	Binding.Width = Found->Width;
	Binding.Height = Found->Height;
	Binding.InitialClearColor = Found->InitialClearColor;
	Binding.InitialClearColorF = Found->InitialClearColorF;
	Binding.InitialClearDepth = Found->InitialClearDepth;
	Binding.Format = Format;
	Out = MoveTemp( Binding );
	return true;
}

bool CDX12TextureStore::BeginRenderTargetPass( TArrayView<const FTextureResourceHandle> ColorTargets, FTextureResourceHandle DepthTarget, TArray<FDX12TextureBinding> &OutColors, FDX12TextureBinding &OutDepth )
{
	const int32 TargetCount = ColorTargets.Num(); // 同時束縛する色描画先の枚数。
	if ( !bHealthy || !Device || FAILED( Device->GetDeviceRemovedReason() ) || ActiveColorTargetCount != 0 || ActiveDepthTarget != 0 || !ColorTargets.GetData() || TargetCount <= 0 || TargetCount > static_cast<int32>( FRenderTargetPassDesc::MaxColorTargets ) || !bRenderTargetCapabilitiesAvailable || static_cast<uint32>( TargetCount ) > RenderTargetCapabilities.MaxColorTargets )
	{
		return false;
	}
	TArray<FDX12TextureBinding> ColorBindings; // 全ターゲットを検証できるまで確定しない。
	ColorBindings.SetNum( TargetCount );
	int32 RefWidth = 0, RefHeight = 0; // すべての色・深度ターゲットで揃える画素寸法。
	for ( int32 i = 0; i < TargetCount; ++i )
	{
		const FTextureResourceHandle Handle = ColorTargets[i]; // i番目の色描画先。
		if ( !Handle.IsValid() || ( DepthTarget.IsValid() && Handle.Value == DepthTarget.Value ) )
		{
			return false;
		}
		for ( int32 Earlier = 0; Earlier < i; ++Earlier )
		{
			if ( ColorTargets[Earlier].Value == Handle.Value )
			{
				return false;
			}
		}
		const FTextureRecord *Color = Textures.Find( Handle.Value ); // 所有するRGBA8・RGBA16F・R32Floatカラー描画先か検査する（DepthD32は不可）。
		const bool bIsRGBA8 = Color && Color->Kind == ETextureKind::RenderTargetRGBA8 && RenderTargetCapabilities.bRGBA8RenderTarget;
		const bool bIsRGBA16F = Color && Color->Kind == ETextureKind::RenderTargetRGBA16F && RenderTargetCapabilities.bRGBA16FRenderTarget;
		const bool bIsR32Float = Color && Color->Kind == ETextureKind::RenderTargetR32Float && RenderTargetCapabilities.bR32FloatRenderTarget;
		if ( !Color || ( !bIsRGBA8 && !bIsRGBA16F && !bIsR32Float ) || !Color->Resource || !Color->TargetHeap )
		{
			return false;
		}
		if ( i == 0 )
		{
			RefWidth = Color->Width;
			RefHeight = Color->Height;
		}
		else if ( Color->Width != RefWidth || Color->Height != RefHeight )
		{
			return false;
		}
		ColorBindings[i].Resource = Color->Resource;
		ColorBindings[i].TargetHeap = Color->TargetHeap;
		ColorBindings[i].SrvGpuDescriptor = Color->Descriptor.Gpu.ptr;
		ColorBindings[i].Width = Color->Width;
		ColorBindings[i].Height = Color->Height;
		ColorBindings[i].InitialClearColor = Color->InitialClearColor;
		ColorBindings[i].InitialClearColorF = Color->InitialClearColorF;
		ColorBindings[i].Format = bIsRGBA16F ? EComputeTextureFormat::RGBA16F : ( bIsR32Float ? EComputeTextureFormat::R32Float : EComputeTextureFormat::RGBA8 );
	}
	FDX12TextureBinding DepthBinding; // 任意の読み取れる深度。
	if ( DepthTarget.IsValid() )
	{
		const FTextureRecord *Depth = Textures.Find( DepthTarget.Value ); // 書き込み先の深度。
		if ( !Depth || Depth->Kind != ETextureKind::DepthD32 || !Depth->Resource || !Depth->TargetHeap || Depth->Width != RefWidth || Depth->Height != RefHeight )
		{
			return false;
		}
		DepthBinding.Resource = Depth->Resource;
		DepthBinding.TargetHeap = Depth->TargetHeap;
		DepthBinding.SrvGpuDescriptor = Depth->Descriptor.Gpu.ptr;
		DepthBinding.Width = Depth->Width;
		DepthBinding.Height = Depth->Height;
		DepthBinding.InitialClearDepth = Depth->InitialClearDepth;
		DepthBinding.Format = EComputeTextureFormat::R32Float;
	}
	ActiveColorTargetCount = static_cast<uint32>( TargetCount );
	for ( int32 i = 0; i < TargetCount; ++i )
	{
		ActiveColorTargets[i] = ColorTargets[i].Value;
	}
	ActiveDepthTarget = DepthTarget.IsValid() ? DepthTarget.Value : 0;
	OutColors = MoveTemp( ColorBindings );
	OutDepth = MoveTemp( DepthBinding );
	return true;
}

bool CDX12TextureStore::BeginRenderTargetPass( FTextureResourceHandle ColorTarget, FTextureResourceHandle DepthTarget, FDX12TextureBinding &OutColor, FDX12TextureBinding &OutDepth )
{
	TArray<FDX12TextureBinding> Colors; // 既存の単一ターゲット版はMRT版へ委譲する。
	const FTextureResourceHandle Single[1] = { ColorTarget };
	if ( !BeginRenderTargetPass( TArrayView<const FTextureResourceHandle>( Single, 1 ), DepthTarget, Colors, OutDepth ) )
	{
		return false;
	}
	OutColor = MoveTemp( Colors[0] );
	return true;
}

void CDX12TextureStore::EndRenderTargetPass()
{
	for ( uint32 i = 0; i < ActiveColorTargetCount; ++i )
	{
		ActiveColorTargets[i] = 0;
	}
	ActiveColorTargetCount = 0;
	ActiveDepthTarget = 0;
}

bool CDX12TextureStore::IsRenderTargetPassActive() const
{
	return ActiveColorTargetCount != 0 || ActiveDepthTarget != 0;
}

bool CDX12TextureStore::IsActiveTarget( FTextureResourceHandle Texture ) const
{
	return Texture.IsValid() && IsActiveTarget( Texture.Value );
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
	if ( !Found || Found->Kind == ETextureKind::DepthD32 || Found->Descriptor.Serial == 0 || IsActiveTarget( Texture.Value ) )
	{
		return false;
	}
	Out = { Found->Descriptor.Gpu.ptr, Found->Width, Found->Height };
	return true;
}

bool CDX12TextureStore::CopyTexture( FTextureResourceHandle Source, FTextureResourceHandle Destination )
{
	if ( !bHealthy || !Device || !Queue || !Allocator || !Commands || FAILED( Device->GetDeviceRemovedReason() ) || !Source.IsValid() || !Destination.IsValid() || Source.Value == Destination.Value || IsActiveTarget( Source.Value ) || IsActiveTarget( Destination.Value ) )
	{
		return false;
	}
	const FTextureRecord *Src = Textures.Find( Source.Value );
	const FTextureRecord *Dst = Textures.Find( Destination.Value );
	if ( !Src || !Dst || !Src->Resource || !Dst->Resource || Src->Width != Dst->Width || Src->Height != Dst->Height )
	{
		return false;
	}
	const bool bSrcRGBA8 = Src->Kind == ETextureKind::StandardRGBA8 || Src->Kind == ETextureKind::ComputeRGBA8 || Src->Kind == ETextureKind::RenderTargetRGBA8;
	const bool bDstRGBA8 = Dst->Kind == ETextureKind::StandardRGBA8 || Dst->Kind == ETextureKind::ComputeRGBA8 || Dst->Kind == ETextureKind::RenderTargetRGBA8;
	const bool bSrcRGBA16F = Src->Kind == ETextureKind::ComputeRGBA16F || Src->Kind == ETextureKind::RenderTargetRGBA16F;
	const bool bDstRGBA16F = Dst->Kind == ETextureKind::ComputeRGBA16F || Dst->Kind == ETextureKind::RenderTargetRGBA16F;
	const bool bSrcR32Float = Src->Kind == ETextureKind::RenderTargetR32Float;
	const bool bDstR32Float = Dst->Kind == ETextureKind::RenderTargetR32Float;
	if ( !( ( bSrcRGBA8 && bDstRGBA8 ) || ( bSrcRGBA16F && bDstRGBA16F ) || ( bSrcR32Float && bDstR32Float ) ) )
	{
		return false;
	}
	if ( FAILED( Allocator->Reset() ) || FAILED( Commands->Reset( Allocator.Get(), nullptr ) ) )
	{
		bHealthy = false;
		return false;
	}
	D3D12_RESOURCE_BARRIER EnterBarriers[2] = {}; // 定常のSRV状態からコピー状態へ移す。
	EnterBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	EnterBarriers[0].Transition.pResource = Src->Resource.Get();
	EnterBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	EnterBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	EnterBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	EnterBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	EnterBarriers[1].Transition.pResource = Dst->Resource.Get();
	EnterBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	EnterBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	EnterBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
	Commands->ResourceBarrier( 2, EnterBarriers );

	D3D12_TEXTURE_COPY_LOCATION DstLoc = {};
	DstLoc.pResource = Dst->Resource.Get();
	DstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	D3D12_TEXTURE_COPY_LOCATION SrcLoc = {};
	SrcLoc.pResource = Src->Resource.Get();
	SrcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	Commands->CopyTextureRegion( &DstLoc, 0, 0, 0, &SrcLoc, nullptr );

	D3D12_RESOURCE_BARRIER ExitBarriers[2] = {}; // 定常のSRV状態へ戻して後続描画・computeへ渡す。
	ExitBarriers[0] = EnterBarriers[0];
	ExitBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
	ExitBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	ExitBarriers[1] = EnterBarriers[1];
	ExitBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	ExitBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	Commands->ResourceBarrier( 2, ExitBarriers );
	if ( FAILED( Commands->Close() ) )
	{
		bHealthy = false;
		return false;
	}
	PendingTexture = Src->Resource;
	PendingUpload = Dst->Resource;
	ID3D12CommandList *Lists[] = { Commands.Get() };
	Queue->ExecuteCommandLists( 1, Lists );
	bHealthy = Completion.Wait( 10000 );
	if ( !bHealthy )
	{
		return false;
	}
	PendingTexture.Reset();
	PendingUpload.Reset();
	return true;
}

bool CDX12TextureStore::ReadbackTextureBytes( const FTextureRecord &Record, DXGI_FORMAT ExpectedFormat, UINT BytesPerPixel, TArray<uint8> &OutRows, UINT &OutRowBytes )
{
	if ( !bHealthy || !Device || !Queue || !Allocator || !Commands || !Record.Resource || Record.Width <= 0 || Record.Height <= 0 || FAILED( Device->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	D3D12_RESOURCE_DESC Desc = Record.Resource->GetDesc();
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
	if ( FAILED( Allocator->Reset() ) || FAILED( Commands->Reset( Allocator.Get(), nullptr ) ) )
	{
		bHealthy = false;
		return false;
	}
	D3D12_RESOURCE_BARRIER Barrier = {};
	Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Barrier.Transition.pResource = Record.Resource.Get();
	Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
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
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	Commands->ResourceBarrier( 1, &Barrier );
	if ( FAILED( Commands->Close() ) )
	{
		bHealthy = false;
		return false;
	}
	PendingTexture = Record.Resource;
	PendingUpload = ReadbackBuffer;
	ID3D12CommandList *Lists[] = { Commands.Get() };
	Queue->ExecuteCommandLists( 1, Lists );
	bHealthy = Completion.Wait( 10000 );
	if ( !bHealthy )
	{
		return false;
	}
	PendingTexture.Reset();
	PendingUpload.Reset();

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

bool CDX12TextureStore::ReadTextureRGBA8( FTextureResourceHandle Texture, TArray<FColor32> &OutPixels, int32 &OutWidth, int32 &OutHeight )
{
	if ( !bHealthy || !Device || FAILED( Device->GetDeviceRemovedReason() ) || !Texture.IsValid() || IsActiveTarget( Texture.Value ) )
	{
		return false;
	}
	const FTextureRecord *Found = Textures.Find( Texture.Value );
	const bool bIsRGBA8 = Found && ( Found->Kind == ETextureKind::StandardRGBA8 || Found->Kind == ETextureKind::ComputeRGBA8 || Found->Kind == ETextureKind::RenderTargetRGBA8 );
	if ( !bIsRGBA8 )
	{
		return false;
	}
	TArray<uint8> Rows;
	UINT RowBytes = 0;
	if ( !ReadbackTextureBytes( *Found, DXGI_FORMAT_R8G8B8A8_UNORM, 4, Rows, RowBytes ) )
	{
		return false;
	}
	TArray<FColor32> Pixels;
	Pixels.SetNum( Found->Width * Found->Height );
	FMemory::Memcpy( Pixels.GetData(), Rows.GetData(), static_cast<usize>( Pixels.Num() ) * sizeof( FColor32 ) );
	OutPixels = MoveTemp( Pixels );
	OutWidth = Found->Width;
	OutHeight = Found->Height;
	return true;
}

bool CDX12TextureStore::ReadTextureRGBA16F( FTextureResourceHandle Texture, TArray<FColor> &OutPixels, int32 &OutWidth, int32 &OutHeight )
{
	if ( !bHealthy || !Device || FAILED( Device->GetDeviceRemovedReason() ) || !Texture.IsValid() || IsActiveTarget( Texture.Value ) )
	{
		return false;
	}
	const FTextureRecord *Found = Textures.Find( Texture.Value );
	const bool bIsRGBA16F = Found && ( Found->Kind == ETextureKind::ComputeRGBA16F || Found->Kind == ETextureKind::RenderTargetRGBA16F );
	if ( !bIsRGBA16F )
	{
		return false;
	}
	TArray<uint8> Rows;
	UINT RowBytes = 0;
	if ( !ReadbackTextureBytes( *Found, DXGI_FORMAT_R16G16B16A16_FLOAT, 8, Rows, RowBytes ) )
	{
		return false;
	}
	const int32 TotalPixels = Found->Width * Found->Height;
	const uint16 *HalfWords = reinterpret_cast<const uint16 *>( Rows.GetData() );
	TArray<FColor> Pixels;
	Pixels.SetNum( TotalPixels );
	for ( int32 i = 0; i < TotalPixels; ++i )
	{
		Pixels[i].r = HalfToFloat32( HalfWords[i * 4 + 0] );
		Pixels[i].g = HalfToFloat32( HalfWords[i * 4 + 1] );
		Pixels[i].b = HalfToFloat32( HalfWords[i * 4 + 2] );
		Pixels[i].a = HalfToFloat32( HalfWords[i * 4 + 3] );
	}
	OutPixels = MoveTemp( Pixels );
	OutWidth = Found->Width;
	OutHeight = Found->Height;
	return true;
}

bool CDX12TextureStore::ReadTextureR32Float( FTextureResourceHandle Texture, TArray<float32> &OutValues, int32 &OutWidth, int32 &OutHeight )
{
	if ( !bHealthy || !Device || FAILED( Device->GetDeviceRemovedReason() ) || !Texture.IsValid() || IsActiveTarget( Texture.Value ) )
	{
		return false;
	}
	const FTextureRecord *Found = Textures.Find( Texture.Value );
	if ( !Found || Found->Kind != ETextureKind::RenderTargetR32Float )
	{
		return false;
	}
	TArray<uint8> Rows;
	UINT RowBytes = 0;
	if ( !ReadbackTextureBytes( *Found, DXGI_FORMAT_R32_FLOAT, 4, Rows, RowBytes ) )
	{
		return false;
	}
	TArray<float32> Values;
	Values.SetNum( Found->Width * Found->Height );
	FMemory::Memcpy( Values.GetData(), Rows.GetData(), static_cast<usize>( Values.Num() ) * sizeof( float32 ) );
	OutValues = MoveTemp( Values );
	OutWidth = Found->Width;
	OutHeight = Found->Height;
	return true;
}
