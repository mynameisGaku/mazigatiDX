// SPDX-License-Identifier: MIT
#include "DX12TextureCreator.h"
#include "DX12HalfFloat.h"
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Render/TextureMips.h"

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

	// 二次元の既定ヒープの画像の記述（寸法・形式・用途の旗）。
	D3D12_RESOURCE_DESC MakeTexture2DDesc( int32 Width, int32 Height, UINT MipLevels, DXGI_FORMAT Format, D3D12_RESOURCE_FLAGS Flags )
	{
		D3D12_RESOURCE_DESC Desc = {};
		Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		Desc.Width = Width;
		Desc.Height = Height;
		Desc.DepthOrArraySize = Desc.SampleDesc.Count = 1;
		Desc.MipLevels = static_cast<UINT16>( MipLevels );
		Desc.Format = Format;
		Desc.Flags = Flags;
		return Desc;
	}

	// 既定ヒープの画像資源を、指定の状態で作る。
	bool CreateDefaultHeapResource( ID3D12Device *Device, const D3D12_RESOURCE_DESC &Desc, D3D12_RESOURCE_STATES State, const D3D12_CLEAR_VALUE *ClearValue, Microsoft::WRL::ComPtr<ID3D12Resource> &Out )
	{
		D3D12_HEAP_PROPERTIES Heap = {};
		Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		return SUCCEEDED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Desc, State, ClearValue, IID_PPV_ARGS( &Out ) ) );
	}
} // namespace

void CDX12TextureCreator::Connect( ID3D12Device *InDevice, CDX12SyncSubmitter *InSubmitter, CDX12DescriptorPool *InPool )
{
	Device = InDevice;
	Submitter = InSubmitter;
	Pool = InPool;
}

void CDX12TextureCreator::Disconnect()
{
	Device = nullptr;
	Submitter = nullptr;
	Pool = nullptr;
}

template <typename TFill>
bool CDX12TextureCreator::UploadTexture( const D3D12_RESOURCE_DESC &Desc, UINT NumLevels, TFill &&Fill, FDX12TextureRecord &Created )
{
	if ( !CreateDefaultHeapResource( Device, Desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, Created.Resource ) )
	{
		return false;
	}
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT Layouts[16] = {}; // GPUが必要とする行の境界と幅（段ごと。16384 辺でも 15 段まで）。
	UINT64 TotalBytes = 0; // paddingを含む転送領域の大きさ。
	Device->GetCopyableFootprints( &Desc, 0, NumLevels, 0, Layouts, nullptr, nullptr, &TotalBytes );
	D3D12_HEAP_PROPERTIES Heap = {}; // CPUから書き込む転送領域の置き場。
	Heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC Buffer = {}; // CPUから書き込む転送領域。
	Buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	Buffer.Width = TotalBytes;
	Buffer.Height = Buffer.DepthOrArraySize = Buffer.MipLevels = Buffer.SampleDesc.Count = 1;
	Buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	Microsoft::WRL::ComPtr<ID3D12Resource> UploadBuffer; // 転送の完了まで所有する。
	void *Mapped = nullptr; // CPUが書く先。
	const D3D12_RANGE NoRead = { 0, 0 }; // CPUからは読まない。
	if ( FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Buffer, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &UploadBuffer ) ) ) || FAILED( UploadBuffer->Map( 0, &NoRead, &Mapped ) ) )
	{
		return false;
	}
	Fill( static_cast<uint8 *>( Mapped ), Layouts );
	UploadBuffer->Unmap( 0, nullptr );
	ID3D12GraphicsCommandList *Commands = Submitter->Begin(); // 描画の記録先とは別の命令列。
	if ( !Commands )
	{
		return false;
	}
	for ( UINT Level = 0; Level < NumLevels; ++Level )
	{
		D3D12_TEXTURE_COPY_LOCATION Source = {}; // paddingを含む転送元（段ごと）。
		Source.pResource = UploadBuffer.Get();
		Source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		Source.PlacedFootprint = Layouts[Level];
		D3D12_TEXTURE_COPY_LOCATION Target = {}; // 画像の同じ段へ送る。
		Target.pResource = Created.Resource.Get();
		Target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		Target.SubresourceIndex = Level;
		Commands->CopyTextureRegion( &Target, 0, 0, 0, &Source, nullptr );
	}
	RecordTransition( Commands, Created.Resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, ShaderResourceState ); // 以後はshaderから読む。
	return Submitter->Submit( Created.Resource.Get(), UploadBuffer.Get() );
}

bool CDX12TextureCreator::AttachShaderResourceView( FDX12TextureRecord &Created, DXGI_FORMAT Format, UINT MipLevels )
{
	if ( !Pool || !Pool->Allocate( Created.Descriptor ) )
	{
		return false;
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC View = {}; // 画像の記述子もこの実装が所有する。
	View.Format = Format;
	View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	View.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	View.Texture2D.MipLevels = MipLevels;
	Device->CreateShaderResourceView( Created.Resource.Get(), &View, Created.Descriptor.Cpu );
	return true;
}

bool CDX12TextureCreator::CreateRGBA8( int32 Width, int32 Height, const FColor32 *Pixels, bool bWritable, bool bMips, FDX12TextureRecord &Out )
{
	if ( !Device || !Submitter || !Pool )
	{
		return false;
	}
	FDX12TextureRecord Created; // 転送完了まで外へ公開しない画像。
	// ミップマップ: CPU で、半分ずつに縮めた段を作る（作れない大きさ・UAV 書き込み可の画像・1 x 1 は、段なし）
	TArray<NTextureMips::FLevel> MipLevels;
	if ( bMips && !bWritable )
	{
		NTextureMips::GenerateChain( Width, Height, Pixels, MipLevels );
	}
	const UINT NumLevels = static_cast<UINT>( 1 + MipLevels.Num() );
	const D3D12_RESOURCE_DESC Desc = MakeTexture2DDesc( Width, Height, NumLevels, DXGI_FORMAT_R8G8B8A8_UNORM, bWritable ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE );
	auto Fill = [&]( uint8 *Mapped, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT *Layouts )
	{
		for ( UINT Level = 0; Level < NumLevels; ++Level )
		{
			const int32 LevelWidth = Level == 0 ? Width : MipLevels[Level - 1].Width;
			const int32 LevelHeight = Level == 0 ? Height : MipLevels[Level - 1].Height;
			const FColor32 *LevelPixels = Level == 0 ? Pixels : MipLevels[Level - 1].Pixels.GetData();
			for ( int32 Row = 0; Row < LevelHeight; ++Row )
			{
				FMemory::Memcpy( Mapped + Layouts[Level].Offset + static_cast<usize>( Row ) * Layouts[Level].Footprint.RowPitch, LevelPixels + static_cast<usize>( Row ) * LevelWidth, static_cast<usize>( LevelWidth ) * sizeof( FColor32 ) );
			}
		}
	};
	if ( !UploadTexture( Desc, NumLevels, Fill, Created ) || !AttachShaderResourceView( Created, Desc.Format, NumLevels ) )
	{
		return false;
	}
	Created.Width = Width;
	Created.Height = Height;
	Created.MipLevels = static_cast<int32>( NumLevels );
	Created.Kind = bWritable ? EDX12TextureKind::ComputeRGBA8 : EDX12TextureKind::StandardRGBA8;
	Out = MoveTemp( Created );
	return true;
}

bool CDX12TextureCreator::CreateRGBA16F( int32 Width, int32 Height, const FColor *Pixels, FDX12TextureRecord &Out )
{
	if ( !Device || !Submitter || !Pool )
	{
		return false;
	}
	FDX12TextureRecord Created; // 転送完了まで外へ公開しないRGBA16F画像。
	const D3D12_RESOURCE_DESC Desc = MakeTexture2DDesc( Width, Height, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );
	auto Fill = [&]( uint8 *Mapped, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT *Layouts )
	{
		for ( int32 Row = 0; Row < Height; ++Row )
		{
			uint16 *DstHalf = reinterpret_cast<uint16 *>( Mapped + Layouts[0].Offset + static_cast<usize>( Row ) * Layouts[0].Footprint.RowPitch );
			const FColor *SrcRow = Pixels + static_cast<usize>( Row ) * Width;
			for ( int32 Col = 0; Col < Width; ++Col )
			{
				DstHalf[Col * 4 + 0] = NDX12HalfFloat::FromFloat32( SrcRow[Col].r );
				DstHalf[Col * 4 + 1] = NDX12HalfFloat::FromFloat32( SrcRow[Col].g );
				DstHalf[Col * 4 + 2] = NDX12HalfFloat::FromFloat32( SrcRow[Col].b );
				DstHalf[Col * 4 + 3] = NDX12HalfFloat::FromFloat32( SrcRow[Col].a );
			}
		}
	};
	if ( !UploadTexture( Desc, 1, Fill, Created ) || !AttachShaderResourceView( Created, Desc.Format, 1 ) )
	{
		return false;
	}
	Created.Width = Width;
	Created.Height = Height;
	Created.Kind = EDX12TextureKind::ComputeRGBA16F;
	Out = MoveTemp( Created );
	return true;
}

bool CDX12TextureCreator::CreateColorRenderTarget( int32 Width, int32 Height, DXGI_FORMAT Format, const float32 ClearValues[4], EDX12TextureKind Kind, FDX12TextureRecord &Out )
{
	if ( !Device || !Submitter || !Pool )
	{
		return false;
	}
	FDX12TextureRecord Created; // 初期消去とSRV作成が完了するまで登録しない。
	const D3D12_RESOURCE_DESC Desc = MakeTexture2DDesc( Width, Height, 1, Format, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET );
	D3D12_CLEAR_VALUE ClearValue = {}; // 初期消去色と一致させる最適化値。
	ClearValue.Format = Desc.Format;
	ClearValue.Color[0] = ClearValues[0];
	ClearValue.Color[1] = ClearValues[1];
	ClearValue.Color[2] = ClearValues[2];
	ClearValue.Color[3] = ClearValues[3];
	D3D12_DESCRIPTOR_HEAP_DESC RtvHeapDesc = {}; // この描画先専用のCPU RTV。
	RtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	RtvHeapDesc.NumDescriptors = 1;
	if ( FAILED( Device->CreateDescriptorHeap( &RtvHeapDesc, IID_PPV_ARGS( &Created.TargetHeap ) ) ) || !CreateDefaultHeapResource( Device, Desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &ClearValue, Created.Resource ) )
	{
		return false;
	}
	const D3D12_CPU_DESCRIPTOR_HANDLE Rtv = Created.TargetHeap->GetCPUDescriptorHandleForHeapStart(); // 初期消去と描画パスで使うRTV。
	Device->CreateRenderTargetView( Created.Resource.Get(), nullptr, Rtv );
	ID3D12GraphicsCommandList *Commands = Submitter->Begin();
	if ( !Commands )
	{
		return false;
	}
	Commands->ClearRenderTargetView( Rtv, ClearValue.Color, 0, nullptr );
	RecordTransition( Commands, Created.Resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, ShaderResourceState ); // 描画パス外では常にSRVとして読める状態に揃える。
	if ( !Submitter->Submit( Created.Resource.Get() ) || !AttachShaderResourceView( Created, Desc.Format, 1 ) )
	{
		return false;
	}
	Created.Width = Width;
	Created.Height = Height;
	Created.Kind = Kind;
	Out = MoveTemp( Created );
	return true;
}

bool CDX12TextureCreator::CreateDepthD32( int32 Width, int32 Height, float32 ClearDepth, FDX12TextureRecord &Out )
{
	if ( !Device || !Submitter || !Pool )
	{
		return false;
	}
	FDX12TextureRecord Created; // 初期消去と遷移が完了するまで登録しない。
	const D3D12_RESOURCE_DESC Desc = MakeTexture2DDesc( Width, Height, 1, DXGI_FORMAT_R32_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL ); // DSV(D32_FLOAT)とSRV(R32_FLOAT)を共用するtypeless深度。
	D3D12_CLEAR_VALUE ClearValue = {}; // 初期深度と一致させる最適化値。
	ClearValue.Format = DXGI_FORMAT_D32_FLOAT;
	ClearValue.DepthStencil.Depth = ClearDepth;
	D3D12_DESCRIPTOR_HEAP_DESC DsvHeapDesc = {}; // この深度資源専用のCPU DSV。
	DsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	DsvHeapDesc.NumDescriptors = 1;
	if ( FAILED( Device->CreateDescriptorHeap( &DsvHeapDesc, IID_PPV_ARGS( &Created.TargetHeap ) ) ) || !CreateDefaultHeapResource( Device, Desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &ClearValue, Created.Resource ) )
	{
		return false;
	}
	const D3D12_CPU_DESCRIPTOR_HANDLE Dsv = Created.TargetHeap->GetCPUDescriptorHandleForHeapStart(); // 初期消去と描画パスで使うDSV。
	D3D12_DEPTH_STENCIL_VIEW_DESC DsvDesc = {}; // R32_TYPELESS上にD32_FLOATのDSVを作る。
	DsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	DsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	Device->CreateDepthStencilView( Created.Resource.Get(), &DsvDesc, Dsv );
	ID3D12GraphicsCommandList *Commands = Submitter->Begin();
	if ( !Commands )
	{
		return false;
	}
	Commands->ClearDepthStencilView( Dsv, D3D12_CLEAR_FLAG_DEPTH, ClearDepth, 0, 0, nullptr );
	RecordTransition( Commands, Created.Resource.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, ShaderResourceState ); // 描画パス外では常にSRVとして読める状態に揃える。
	if ( !Submitter->Submit( Created.Resource.Get() ) || !AttachShaderResourceView( Created, DXGI_FORMAT_R32_FLOAT, 1 ) ) // カスタムグラフィックスおよびcompute読み取り用R32_FLOAT SRV。
	{
		return false;
	}
	Created.Width = Width;
	Created.Height = Height;
	Created.InitialClearDepth = ClearDepth;
	Created.Kind = EDX12TextureKind::DepthD32;
	Out = MoveTemp( Created );
	return true;
}
