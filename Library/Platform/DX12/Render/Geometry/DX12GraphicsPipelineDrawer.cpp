// SPDX-License-Identifier: MIT
#include "DX12GraphicsPipelineDrawer.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Platform/DX12/Pipeline/Graphics/DX12GraphicsPipelineStore.h"
#include "Library/Platform/DX12/Render/Geometry/DX12Topology.h"
#include "Library/Platform/DX12/Resource/Buffer/DX12BufferBarriers.h"
#include "Library/Platform/DX12/Resource/Buffer/DX12BufferStore.h"
#include "Library/Platform/DX12/Resource/Texture/DX12TextureStore.h"
#include "Library/Platform/DX12/Resource/Upload/DX12UploadArena.h"

namespace
{
	constexpr int32 MaxGraphicsDrawsPerFrame = 1024; // 1 フレームのカスタム描画の上限。
	constexpr uint64 MaxUploadBytesPerFrame = 64ull * 1024ull * 1024ull; // 1 フレームに転送領域へ載せられる量の上限。

	// 定数 1 つを、256 の境界へ揃えた大きさ。
	uint64 AlignedConstantSize( const FGraphicsConstantBytes &Bytes )
	{
		return ( static_cast<uint64>( Bytes.Size ) + 255ull ) / 256ull * 256ull;
	}
} // namespace

void CDX12GraphicsPipelineDrawer::Initialize( ID3D12Device *InDevice, ID3D12CommandQueue *InBufferQueue, CDX12GraphicsPipelineStore *InGraphicsPipelines, CDX12BufferStore *InBuffers, CDX12TextureStore *InTextureStore, CDX12UploadArena *InUploadArena )
{
	Device = InDevice;
	BufferQueue = InBufferQueue;
	GraphicsPipelines = InGraphicsPipelines;
	Buffers = InBuffers;
	TextureStore = InTextureStore;
	UploadArena = InUploadArena;
}

bool CDX12GraphicsPipelineDrawer::IsForbiddenBinding( const FDX12GraphicsDrawTarget &Target, UINT64 Binding )
{
	if ( Binding == 0 )
	{
		return false;
	}
	for ( uint32 i = 0; i < Target.NumForbiddenBindings; ++i )
	{
		if ( Target.ForbiddenBindings[i] == Binding )
		{
			return true;
		}
	}
	return false;
}

bool CDX12GraphicsPipelineDrawer::IsValidSrvHandle( const FDX12GraphicsDrawTarget &Target, UINT64 Binding ) const
{
	if ( !Target.SrvHeap || Binding == 0 )
	{
		return false;
	}
	const UINT64 Base = Target.SrvHeap->GetGPUDescriptorHandleForHeapStart().ptr; // 同じ画像heapの範囲だけを使う。
	const UINT Stride = Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ); // 画像記述子の幅。
	return Binding >= Base && ( Binding - Base ) % Stride == 0 && ( Binding - Base ) / Stride < Target.SrvHeap->GetDesc().NumDescriptors;
}

bool CDX12GraphicsPipelineDrawer::AcquirePipeline( FGraphicsPipelineHandle Handle, const FDX12GraphicsDrawTarget &Target, FDX12GraphicsPipelineBinding &OutPipeline ) const
{
	if ( !GraphicsPipelines->AcquirePipeline( Handle, OutPipeline ) || !OutPipeline.Pipeline || !OutPipeline.Root || OutPipeline.NumRenderTargets != Target.NumColorViews || ( OutPipeline.DepthFormat == EGraphicsDepthFormat::D32Float && !Target.bHasDepth ) )
	{
		return false;
	}
	for ( uint32 i = 0; i < Target.NumColorViews; ++i )
	{
		if ( OutPipeline.RenderTargetFormats[i] != Target.ColorFormats[i] )
		{
			return false;
		}
	}
	return true;
}

bool CDX12GraphicsPipelineDrawer::AcquireSlotTextures( TArrayView<const FTextureResourceHandle> Textures, const FDX12GraphicsPipelineBinding &Pipeline, const FDX12GraphicsDrawTarget &Target, FDX12GraphicsDrawLedger &Ledger, TArray<FDX12TextureBinding> &OutTextures ) const
{
	if ( Pipeline.TextureSlots.IsEmpty() )
	{
		return true;
	}
	if ( !TextureStore || !Target.SrvHeap )
	{
		return false;
	}
	OutTextures.SetNum( Pipeline.TextureSlots.Num() );
	for ( int32 i = 0; i < Pipeline.TextureSlots.Num(); ++i )
	{
		const FTextureResourceHandle TexHandle = Textures[i];
		if ( !TextureStore->AcquireGraphicsTexture( TexHandle, Pipeline.TextureSlots[i].Format, OutTextures[i] ) )
		{
			if ( TextureStore->IsActiveTarget( TexHandle ) )
			{
				*Ledger.bHazardRejected = true;
			}
			return false;
		}
		if ( !IsValidSrvHandle( Target, OutTextures[i].SrvGpuDescriptor ) )
		{
			return false;
		}
		if ( IsForbiddenBinding( Target, OutTextures[i].SrvGpuDescriptor ) )
		{
			*Ledger.bHazardRejected = true;
			return false;
		}
	}
	return true;
}

bool CDX12GraphicsPipelineDrawer::UploadConstants( TArrayView<const FGraphicsConstantBytes> Constants, const FDX12GraphicsDrawLedger &Ledger, TArray<uint64> &OutGpuAddresses ) const
{
	for ( const FGraphicsConstantBytes &Bytes : Constants )
	{
		const uint64 Size = AlignedConstantSize( Bytes );
		CDX12UploadArena::FAllocation Alloc = {};
		if ( !UploadArena->Allocate( Ledger.UploadTicket, Size, 256, Alloc ) )
		{
			return false;
		}
		FMemory::Memzero( Alloc.CpuAddress, static_cast<usize>( Size ) ); // 余りは0。
		FMemory::Memcpy( Alloc.CpuAddress, Bytes.Data, static_cast<usize>( Bytes.Size ) );
		OutGpuAddresses.Add( Alloc.GpuAddress );
	}
	return true;
}

bool CDX12GraphicsPipelineDrawer::RecordTransient( const FGraphicsPipelineTransientDraw &Draw, const FDX12GraphicsDrawTarget &Target, FDX12GraphicsDrawLedger &Ledger ) const
{
	if ( !Target.Commands || Target.NumColorViews == 0 || !GraphicsPipelines || !UploadArena || Ledger.UploadTicket == 0 || *Ledger.NumGraphicsDraws >= MaxGraphicsDrawsPerFrame || !Draw.Vertices )
	{
		return false;
	}
	FDX12GraphicsPipelineBinding Pipeline; // PSOとスロット宣言を取得する。
	if ( !AcquirePipeline( Draw.Pipeline, Target, Pipeline ) )
	{
		return false;
	}
	const bool bIndexed = Draw.Indices != nullptr; // index付き描画の有無。
	const uint32 Group = Pipeline.Topology == EGeometryTopology::Points ? 1u : Pipeline.Topology == EGeometryTopology::Lines ? 2u : 3u;
	if ( Draw.VertexCount == 0 || Draw.VertexCount > NGraphicsPipelineLimits::MaxTransientVertices || ( bIndexed ? Draw.IndexCount == 0 || Draw.IndexCount > NGraphicsPipelineLimits::MaxTransientIndices || ( Draw.IndexCount % Group ) != 0 : ( Draw.IndexCount != 0 || ( Draw.VertexCount % Group ) != 0 ) ) )
	{
		return false;
	}
	if ( Draw.Constants.Num() != Pipeline.BufferSlots.Num() || ( Draw.Constants.Num() > 0 && !Draw.Constants.GetData() ) || Draw.Textures.Num() != Pipeline.TextureSlots.Num() || ( Draw.Textures.Num() > 0 && !Draw.Textures.GetData() ) )
	{
		return false;
	}
	for ( int32 i = 0; i < Draw.Constants.Num(); ++i )
	{
		const FGraphicsConstantBytes &Bytes = Draw.Constants[i]; // 一時の転送では、定数だけを受け取る（読み取りバッファは、登録したものを使う）。
		if ( Pipeline.BufferSlots[i].Layout != EGraphicsBufferLayout::Constant || !Bytes.Data || Bytes.Size == 0 || Bytes.Size > 65536 || Bytes.Size < Pipeline.MinimumBytes[i] )
		{
			return false;
		}
	}
	TArray<FDX12TextureBinding> SlotTextures; // DescriptorTableへ渡す画像・読み取り深度。
	if ( !AcquireSlotTextures( Draw.Textures, Pipeline, Target, Ledger, SlotTextures ) )
	{
		return false;
	}
	if ( !Target.bHasArea )
	{
		return true;
	}
	int32 NewBufferPinCount = 0;
	uint64 AddedBufferBytes = 0;
	const TArrayView<const FDX12BufferBinding> NoBuffers; // 一時の転送は、バッファの登録を使わない。
	if ( !Ledger.Pins->CanCommitBuffers( NoBuffers, NewBufferPinCount, AddedBufferBytes ) || !Ledger.Pins->CommitGraphicsAssets( TArrayView<const FDX12TextureBinding>( SlotTextures.GetData(), SlotTextures.Num() ), Pipeline ) )
	{
		return false;
	}
	// 頂点・添字・定数を、フレームの転送領域へ載せる（定数は、256 の境界へ揃える）。
	const uint64 VertexBytes = static_cast<uint64>( Draw.VertexCount ) * Pipeline.VertexStride;
	const uint64 IndexBytes = bIndexed ? static_cast<uint64>( Draw.IndexCount ) * 4ull : 0ull;
	uint64 TotalBytes = VertexBytes + IndexBytes;
	for ( const FGraphicsConstantBytes &Bytes : Draw.Constants )
	{
		TotalBytes += AlignedConstantSize( Bytes ) + 256ull;
	}
	if ( *Ledger.UploadBytes + TotalBytes > MaxUploadBytesPerFrame )
	{
		return false;
	}
	CDX12UploadArena::FAllocation VertexAlloc = {}, IndexAlloc = {};
	if ( !UploadArena->Allocate( Ledger.UploadTicket, VertexBytes, 16, VertexAlloc ) || ( bIndexed && !UploadArena->Allocate( Ledger.UploadTicket, IndexBytes, 4, IndexAlloc ) ) )
	{
		return false;
	}
	FMemory::Memcpy( VertexAlloc.CpuAddress, Draw.Vertices, static_cast<usize>( VertexBytes ) );
	if ( bIndexed )
	{
		FMemory::Memcpy( IndexAlloc.CpuAddress, Draw.Indices, static_cast<usize>( IndexBytes ) );
	}
	TArray<uint64> ConstantAddresses;
	if ( !UploadConstants( Draw.Constants, Ledger, ConstantAddresses ) )
	{
		return false;
	}
	*Ledger.UploadBytes += TotalBytes;
	++*Ledger.NumGraphicsDraws;

	ID3D12GraphicsCommandList *Commands = Target.Commands;
	if ( Target.SrvHeap )
	{
		ID3D12DescriptorHeap *Heaps[] = { Target.SrvHeap };
		Commands->SetDescriptorHeaps( 1, Heaps );
	}
	Commands->SetGraphicsRootSignature( Pipeline.Root.Get() );
	Commands->SetPipelineState( Pipeline.Pipeline.Get() );
	for ( int32 i = 0; i < ConstantAddresses.Num(); ++i )
	{
		Commands->SetGraphicsRootConstantBufferView( static_cast<UINT>( i ), ConstantAddresses[i] );
	}
	for ( int32 i = 0; i < SlotTextures.Num(); ++i )
	{
		Commands->SetGraphicsRootDescriptorTable( static_cast<UINT>( ConstantAddresses.Num() + i ), D3D12_GPU_DESCRIPTOR_HANDLE{ SlotTextures[i].SrvGpuDescriptor } );
	}
	const D3D12_CPU_DESCRIPTOR_HANDLE Dsv = Target.DepthView;
	Commands->OMSetRenderTargets( Target.NumColorViews, Target.ColorViews, FALSE, Pipeline.DepthFormat == EGraphicsDepthFormat::D32Float ? &Dsv : nullptr );
	Commands->RSSetViewports( 1, &Target.Viewport );
	Commands->RSSetScissorRects( 1, &Target.Scissor );
	Commands->IASetPrimitiveTopology( NDX12Topology::ToD3D( Pipeline.Topology ) );
	const D3D12_VERTEX_BUFFER_VIEW Vbv = { VertexAlloc.GpuAddress, static_cast<UINT>( VertexBytes ), Pipeline.VertexStride };
	Commands->IASetVertexBuffers( 0, 1, &Vbv );
	if ( bIndexed )
	{
		const D3D12_INDEX_BUFFER_VIEW Ibv = { IndexAlloc.GpuAddress, static_cast<UINT>( IndexBytes ), DXGI_FORMAT_R32_UINT };
		Commands->IASetIndexBuffer( &Ibv );
		Commands->DrawIndexedInstanced( Draw.IndexCount, 1, 0, 0, 0 );
	}
	else
	{
		Commands->IASetIndexBuffer( nullptr );
		Commands->DrawInstanced( Draw.VertexCount, 1, 0, 0 );
	}
	Commands->OMSetRenderTargets( Target.NumColorViews, Target.ColorViews, FALSE, nullptr );
	return true;
}

bool CDX12GraphicsPipelineDrawer::Record( const FGraphicsPipelineDraw &Draw, const FDX12GraphicsDrawTarget &Target, FDX12GraphicsDrawLedger &Ledger ) const
{
	if ( !Target.Commands || Target.NumColorViews == 0 || !GraphicsPipelines || !Buffers || !Buffers->UsesDeviceAndQueue( Device, BufferQueue ) || *Ledger.NumGraphicsDraws >= MaxGraphicsDrawsPerFrame )
	{
		return false;
	}
	FDX12GraphicsPipelineBinding Pipeline; // PSOとスロット宣言を取得する。
	if ( !AcquirePipeline( Draw.Pipeline, Target, Pipeline ) )
	{
		return false;
	}
	const bool bIndexed = Draw.IndexBuffer.IsValid(); // index付き描画の有無。
	const uint32 Group = Pipeline.Topology == EGeometryTopology::Points ? 1u : Pipeline.Topology == EGeometryTopology::Lines ? 2u : 3u;
	if ( Draw.VertexCount == 0 || Draw.VertexCount > NGraphicsPipelineLimits::MaxRegisteredVertices || ( Draw.VertexOffset % 4 ) != 0 || Draw.IndexFormat > EGeometryIndexFormat::UInt32 || ( bIndexed ? Draw.IndexCount == 0 || Draw.IndexCount > NGraphicsPipelineLimits::MaxRegisteredIndices || ( Draw.IndexCount % Group ) != 0 : ( Draw.IndexCount != 0 || Draw.IndexOffset != 0 || ( Draw.VertexCount % Group ) != 0 ) ) )
	{
		return false;
	}
	const int32 NumConstants = Draw.Constants.Num(); // 先頭の枠は、登録せずに渡す定数（描くたびに変わる値）。
	if ( NumConstants > Pipeline.BufferSlots.Num() || Draw.Buffers.Num() != Pipeline.BufferSlots.Num() - NumConstants || ( Draw.Buffers.Num() > 0 && !Draw.Buffers.GetData() ) || ( NumConstants > 0 && !Draw.Constants.GetData() ) || Draw.Textures.Num() != Pipeline.TextureSlots.Num() || ( Draw.Textures.Num() > 0 && !Draw.Textures.GetData() ) )
	{
		return false;
	}
	for ( int32 i = 0; i < NumConstants; ++i )
	{
		const FGraphicsConstantBytes &Bytes = Draw.Constants[i];
		if ( Pipeline.BufferSlots[i].Layout != EGraphicsBufferLayout::Constant || !Bytes.Data || Bytes.Size == 0 || Bytes.Size > 65536 || Bytes.Size < Pipeline.MinimumBytes[i] )
		{
			return false;
		}
	}
	FDX12BufferBinding Vertices, Indices; // 全範囲が有効な場合だけ描画を記録する。
	if ( !Buffers->AcquireGeometryBuffer( Draw.VertexBuffer, Vertices ) || ( bIndexed && !Buffers->AcquireGeometryBuffer( Draw.IndexBuffer, Indices ) ) )
	{
		return false;
	}
	const uint64 VertexBytes = static_cast<uint64>( Draw.VertexCount ) * Pipeline.VertexStride; // IAへ渡す頂点範囲。
	const uint64 IndexStride = Draw.IndexFormat == EGeometryIndexFormat::UInt16 ? 2ull : 4ull; // index要素幅。
	const uint64 IndexBytes = bIndexed ? static_cast<uint64>( Draw.IndexCount ) * IndexStride : 0ull; // index参照範囲。
	if ( Draw.VertexOffset > Vertices.Size || VertexBytes > Vertices.Size - Draw.VertexOffset || ( bIndexed && ( ( Draw.IndexOffset % IndexStride ) != 0 || Draw.IndexOffset > Indices.Size || IndexBytes > Indices.Size - Draw.IndexOffset ) ) )
	{
		return false;
	}
	TArray<FDX12BufferBinding> SlotBuffers; // Root CBV／Root SRVとして結合するバッファ（定数を渡した枠の続き）。
	SlotBuffers.SetNum( Draw.Buffers.Num() );
	for ( int32 i = 0; i < Draw.Buffers.Num(); ++i )
	{
		const FGraphicsBufferSlot &Slot = Pipeline.BufferSlots[NumConstants + i];
		if ( !Buffers->AcquireComputeBuffer( Draw.Buffers[i], ERenderBufferShaderAccess::Read, SlotBuffers[i] ) )
		{
			return false;
		}
		const uint64 Size = SlotBuffers[i].Size;
		if ( Slot.Layout == EGraphicsBufferLayout::Constant )
		{
			if ( Size == 0 || ( Size % 256 ) != 0 || Size > 65536 || Size < Pipeline.MinimumBytes[NumConstants + i] )
			{
				return false;
			}
		}
		else if ( Slot.Layout == EGraphicsBufferLayout::Raw )
		{
			if ( Size < 4 || ( Size % 4 ) != 0 )
			{
				return false;
			}
		}
		else if ( Slot.Layout == EGraphicsBufferLayout::Structured )
		{
			if ( Slot.ElementStride < 4 || Size < Slot.ElementStride || ( Size % Slot.ElementStride ) != 0 )
			{
				return false;
			}
		}
	}
	TArray<FDX12TextureBinding> SlotTextures; // DescriptorTableへ渡す画像・読み取り深度。
	if ( !AcquireSlotTextures( Draw.Textures, Pipeline, Target, Ledger, SlotTextures ) )
	{
		return false;
	}
	if ( !Target.bHasArea )
	{
		return true;
	}
	TArray<FDX12BufferBinding> BufferCandidates; // VB・IB・全バッファスロットを一度にピン留め検証する。
	BufferCandidates.Reserve( 2 + SlotBuffers.Num() );
	BufferCandidates.Add( Vertices );
	if ( bIndexed )
	{
		BufferCandidates.Add( Indices );
	}
	for ( const FDX12BufferBinding &Binding : SlotBuffers )
	{
		BufferCandidates.Add( Binding );
	}
	int32 NewBufferPinCount = 0;
	uint64 AddedBufferBytes = 0;
	if ( !Ledger.Pins->CanCommitBuffers( TArrayView<const FDX12BufferBinding>( BufferCandidates.GetData(), BufferCandidates.Num() ), NewBufferPinCount, AddedBufferBytes ) || !Ledger.Pins->CommitGraphicsAssets( TArrayView<const FDX12TextureBinding>( SlotTextures.GetData(), SlotTextures.Num() ), Pipeline ) )
	{
		return false;
	}
	TArray<uint64> ConstantAddresses; // 登録せずに渡された定数は、フレームの転送領域へ載せる（256の境界へ揃える）。
	if ( NumConstants > 0 )
	{
		if ( !UploadArena || Ledger.UploadTicket == 0 )
		{
			return false;
		}
		uint64 ConstantBytes = 0;
		for ( const FGraphicsConstantBytes &Bytes : Draw.Constants )
		{
			ConstantBytes += AlignedConstantSize( Bytes ) + 256ull;
		}
		if ( *Ledger.UploadBytes + ConstantBytes > MaxUploadBytesPerFrame )
		{
			return false;
		}
		if ( !UploadConstants( Draw.Constants, Ledger, ConstantAddresses ) )
		{
			return false;
		}
		*Ledger.UploadBytes += ConstantBytes;
	}
	Ledger.Pins->CommitBuffers( TArrayView<const FDX12BufferBinding>( BufferCandidates.GetData(), BufferCandidates.Num() ) );
	++*Ledger.NumGraphicsDraws;

	TArray<D3D12_RESOURCE_BARRIER> Barriers; // 同一バッファが多用途で重複しても1回の遷移へまとめる。
	NDX12BufferBarriers::AccumulateReadState( Barriers, Vertices.Resource.Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER );
	if ( bIndexed )
	{
		NDX12BufferBarriers::AccumulateReadState( Barriers, Indices.Resource.Get(), D3D12_RESOURCE_STATE_INDEX_BUFFER );
	}
	for ( int32 i = 0; i < SlotBuffers.Num(); ++i )
	{
		const D3D12_RESOURCE_STATES RoleState = Pipeline.BufferSlots[NumConstants + i].Layout == EGraphicsBufferLayout::Constant ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER : ( D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		NDX12BufferBarriers::AccumulateReadState( Barriers, SlotBuffers[i].Resource.Get(), RoleState );
	}
	ID3D12GraphicsCommandList *Commands = Target.Commands;
	Commands->ResourceBarrier( static_cast<UINT>( Barriers.Num() ), Barriers.GetData() );

	if ( Target.SrvHeap )
	{
		ID3D12DescriptorHeap *Heaps[] = { Target.SrvHeap };
		Commands->SetDescriptorHeaps( 1, Heaps );
	}
	Commands->SetGraphicsRootSignature( Pipeline.Root.Get() );
	Commands->SetPipelineState( Pipeline.Pipeline.Get() );
	for ( int32 i = 0; i < ConstantAddresses.Num(); ++i )
	{
		Commands->SetGraphicsRootConstantBufferView( static_cast<UINT>( i ), ConstantAddresses[i] );
	}
	for ( int32 i = 0; i < SlotBuffers.Num(); ++i )
	{
		const UINT RootIndex = static_cast<UINT>( NumConstants + i );
		const D3D12_GPU_VIRTUAL_ADDRESS GpuAddress = SlotBuffers[i].Resource->GetGPUVirtualAddress();
		if ( Pipeline.BufferSlots[NumConstants + i].Layout == EGraphicsBufferLayout::Constant )
		{
			Commands->SetGraphicsRootConstantBufferView( RootIndex, GpuAddress );
		}
		else
		{
			Commands->SetGraphicsRootShaderResourceView( RootIndex, GpuAddress );
		}
	}
	for ( int32 i = 0; i < SlotTextures.Num(); ++i )
	{
		const UINT RootIndex = static_cast<UINT>( NumConstants + SlotBuffers.Num() + i );
		Commands->SetGraphicsRootDescriptorTable( RootIndex, D3D12_GPU_DESCRIPTOR_HANDLE{ SlotTextures[i].SrvGpuDescriptor } );
	}
	const D3D12_CPU_DESCRIPTOR_HANDLE Dsv = Target.DepthView;
	Commands->OMSetRenderTargets( Target.NumColorViews, Target.ColorViews, FALSE, Pipeline.DepthFormat == EGraphicsDepthFormat::D32Float ? &Dsv : nullptr );
	Commands->RSSetViewports( 1, &Target.Viewport );
	Commands->RSSetScissorRects( 1, &Target.Scissor );
	Commands->IASetPrimitiveTopology( NDX12Topology::ToD3D( Pipeline.Topology ) );
	const D3D12_VERTEX_BUFFER_VIEW Vbv = { Vertices.Resource->GetGPUVirtualAddress() + Draw.VertexOffset, static_cast<UINT>( VertexBytes ), Pipeline.VertexStride };
	Commands->IASetVertexBuffers( 0, 1, &Vbv );
	if ( bIndexed )
	{
		const D3D12_INDEX_BUFFER_VIEW Ibv = { Indices.Resource->GetGPUVirtualAddress() + Draw.IndexOffset, static_cast<UINT>( IndexBytes ), Draw.IndexFormat == EGeometryIndexFormat::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT };
		Commands->IASetIndexBuffer( &Ibv );
		Commands->DrawIndexedInstanced( Draw.IndexCount, 1, 0, 0, 0 );
	}
	else
	{
		Commands->IASetIndexBuffer( nullptr );
		Commands->DrawInstanced( Draw.VertexCount, 1, 0, 0 );
	}
	Commands->OMSetRenderTargets( Target.NumColorViews, Target.ColorViews, FALSE, nullptr );
	for ( D3D12_RESOURCE_BARRIER &Barrier : Barriers )
	{
		Barrier.Transition.StateBefore = Barrier.Transition.StateAfter;
		Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
	}
	Commands->ResourceBarrier( static_cast<UINT>( Barriers.Num() ), Barriers.GetData() );
	return true;
}
