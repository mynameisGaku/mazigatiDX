// SPDX-License-Identifier: MIT
#include "DX12GeometryRenderer.h"
#include "Library/Platform/DX12/Resource/Buffer/DX12BufferStore.h"
#include "Library/Platform/DX12/Resource/Texture/DX12TextureStore.h"
#include "Library/Platform/DX12/Pipeline/Graphics/DX12GraphicsPipelineStore.h"
#include "Library/Platform/DX12/Resource/Upload/DX12UploadArena.h"
#include <cstddef>
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"

namespace
{
	// バリア配列で同一GPUバッファの必要読み取り状態を統合する。
	void AccumulateBufferReadState( TArray<D3D12_RESOURCE_BARRIER> &Barriers, ID3D12Resource *Resource, D3D12_RESOURCE_STATES ReadState )
	{
		for ( D3D12_RESOURCE_BARRIER &Entry : Barriers )
		{
			if ( Entry.Transition.pResource == Resource )
			{
				Entry.Transition.StateAfter |= ReadState;
				return;
			}
		}
		D3D12_RESOURCE_BARRIER Added = {}; // 提出間で待機状態になっているCOMMONから読み取り用途へ切り替える。
		Added.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		Added.Transition.pResource = Resource;
		Added.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		Added.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
		Added.Transition.StateAfter = ReadState;
		Barriers.Add( Added );
	}

	// DrawGraphicsPipeline用に登録済みトポロジー列挙をD3D12へ変換する。
	D3D_PRIMITIVE_TOPOLOGY ToD3DTopology( EGeometryTopology Topology )
	{
		return Topology == EGeometryTopology::Points ? D3D_PRIMITIVE_TOPOLOGY_POINTLIST : Topology == EGeometryTopology::Lines ? D3D_PRIMITIVE_TOPOLOGY_LINELIST
																															  : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	}
} // namespace

CDX12GeometryRenderer::~CDX12GeometryRenderer()
{
	AbortUploadTicket();
}

bool CDX12GeometryRenderer::CreateOwnedDepth( int32 InWidth, int32 InHeight, Microsoft::WRL::ComPtr<ID3D12Resource> &OutDepth, Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> &OutHeap ) const
{
	if ( !Device || InWidth <= 0 || InHeight <= 0 || InWidth > 16384 || InHeight > 16384 )
	{
		return false;
	}
	D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {}; // 通常描画では専用の深度を作る。
	HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	HeapDesc.NumDescriptors = 1;
	D3D12_HEAP_PROPERTIES Heap = {}; // GPUが読み書きする領域。
	Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC Desc = {}; // 深度画像。
	Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	Desc.Width = static_cast<UINT64>( InWidth );
	Desc.Height = static_cast<UINT>( InHeight );
	Desc.DepthOrArraySize = 1;
	Desc.MipLevels = 1;
	Desc.Format = DXGI_FORMAT_D32_FLOAT;
	Desc.SampleDesc.Count = 1;
	Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	D3D12_CLEAR_VALUE Clear = {}; // 未描画部分の深度。
	Clear.Format = Desc.Format;
	Clear.DepthStencil.Depth = 1;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> NewHeap; // 再作成成功後にだけ差し替える仮記述子。
	Microsoft::WRL::ComPtr<ID3D12Resource> NewDepth; // 再作成成功後にだけ差し替える仮深度。
	if ( FAILED( Device->CreateDescriptorHeap( &HeapDesc, IID_PPV_ARGS( &NewHeap ) ) ) || FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &Clear, IID_PPV_ARGS( &NewDepth ) ) ) )
	{
		return false;
	}
	Device->CreateDepthStencilView( NewDepth.Get(), nullptr, NewHeap->GetCPUDescriptorHandleForHeapStart() );
	OutHeap = MoveTemp( NewHeap );
	OutDepth = MoveTemp( NewDepth );
	return true;
}

bool CDX12GeometryRenderer::Initialize( ID3D12Device *InDevice, int32 InWidth, int32 InHeight, ID3D12Resource *InDepth, ID3D12DescriptorHeap *InDepthHeap, CDX12BufferStore *InBuffers, ID3D12CommandQueue *InQueue, CDX12TextureStore *InTextureStore, CDX12GraphicsPipelineStore *InGraphicsPipelines, CDX12UploadArena *InUploadArena )
{
	if ( Device || !InDevice || InWidth <= 0 || InHeight <= 0 || InWidth > 16384 || InHeight > 16384 || ( InDepth == nullptr ) != ( InDepthHeap == nullptr ) )
	{
		return false;
	}
	if ( InDepth )
	{
		Microsoft::WRL::ComPtr<ID3D12Device> DepthDevice, HeapDevice; // 別のGPUや不一致の深度を借りない。
		const D3D12_RESOURCE_DESC Desc = InDepth->GetDesc(); // 撮影・描画先の寸法・形式を確認する。
		if ( FAILED( InDepth->GetDevice( IID_PPV_ARGS( &DepthDevice ) ) ) || FAILED( InDepthHeap->GetDevice( IID_PPV_ARGS( &HeapDevice ) ) ) || DepthDevice.Get() != InDevice || HeapDevice.Get() != InDevice || Desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || Desc.Width != static_cast<UINT64>( InWidth ) || Desc.Height != static_cast<UINT>( InHeight ) || ( Desc.Format != DXGI_FORMAT_D32_FLOAT && Desc.Format != DXGI_FORMAT_R32_TYPELESS ) || Desc.SampleDesc.Count != 1 || InDepthHeap->GetDesc().Type != D3D12_DESCRIPTOR_HEAP_TYPE_DSV || InDepthHeap->GetDesc().NumDescriptors < 1 )
		{
			return false;
		}
	}
	if ( ( InBuffers && !InQueue ) || ( !InBuffers && !InUploadArena && InQueue ) || ( InBuffers && !InBuffers->UsesDeviceAndQueue( InDevice, InQueue ) ) )
	{
		return false;
	}
	if ( ( InTextureStore && ( !InQueue || !InTextureStore->UsesDeviceAndQueue( InDevice, InQueue ) ) ) || ( InGraphicsPipelines && !InGraphicsPipelines->UsesDevice( InDevice ) ) || ( InUploadArena && ( !InQueue || !InUploadArena->UsesDeviceAndQueue( InDevice, InQueue ) ) ) )
	{
		return false;
	}
	Buffers = InBuffers;
	TextureStore = InTextureStore;
	GraphicsPipelines = InGraphicsPipelines;
	UploadArena = InUploadArena;
	UploadTicket = 0;
	BufferQueue = InQueue;
	Device = InDevice;
	Width = InWidth;
	Height = InHeight;
	if ( !Pipelines.Initialize( InDevice ) )
	{
		return false;
	}
	D3D12_DESCRIPTOR_HEAP_DESC SamplerDesc = {}; // 各設定を一度だけ作る。
	SamplerDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
	SamplerDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	SamplerDesc.NumDescriptors = 32;
	if ( FAILED( Device->CreateDescriptorHeap( &SamplerDesc, IID_PPV_ARGS( &SamplerHeap ) ) ) )
	{
		return false;
	}
	SamplerStride = Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER );
	for ( UINT Index = 0; Index < 32; ++Index )
	{
		D3D12_SAMPLER_DESC Sampler = {}; // 参照方法の組み合わせ。
		Sampler.Filter = Index & 16 ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
		Sampler.AddressU = static_cast<D3D12_TEXTURE_ADDRESS_MODE>( ( Index & 3 ) + 1 );
		Sampler.AddressV = static_cast<D3D12_TEXTURE_ADDRESS_MODE>( ( ( Index >> 2 ) & 3 ) + 1 );
		Sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		Sampler.MaxAnisotropy = 1;
		Sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		Sampler.MaxLOD = D3D12_FLOAT32_MAX;
		auto Handle = SamplerHeap->GetCPUDescriptorHandleForHeapStart(); // この設定の保存先。
		Handle.ptr += Index * SamplerStride;
		Device->CreateSampler( &Sampler, Handle );
	}
	if ( InDepth && InDepthHeap )
	{
		Depth = InDepth;
		DsvHeap = InDepthHeap;
		bOwnsDepth = false;
		return true;
	}
	if ( !CreateOwnedDepth( Width, Height, Depth, DsvHeap ) )
	{
		return false;
	}
	bOwnsDepth = true;
	return true;
}

bool CDX12GeometryRenderer::BindTarget( int32 InWidth, int32 InHeight, ID3D12Resource *InDepth, ID3D12DescriptorHeap *InDepthHeap )
{
	if ( !Device || Commands || InWidth <= 0 || InHeight <= 0 || InWidth > 16384 || InHeight > 16384 || !InDepth || !InDepthHeap )
	{
		return false;
	}
	Microsoft::WRL::ComPtr<ID3D12Device> DepthDevice, HeapDevice; // 同じGPUの深度だけを受け付ける。
	const D3D12_RESOURCE_DESC Desc = InDepth->GetDesc(); // 描画先と同じ寸法のD32深度を確認する。
	if ( FAILED( InDepth->GetDevice( IID_PPV_ARGS( &DepthDevice ) ) ) || FAILED( InDepthHeap->GetDevice( IID_PPV_ARGS( &HeapDevice ) ) ) || DepthDevice.Get() != Device.Get() || HeapDevice.Get() != Device.Get() || Desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || Desc.Width != static_cast<UINT64>( InWidth ) || Desc.Height != static_cast<UINT>( InHeight ) || ( Desc.Format != DXGI_FORMAT_D32_FLOAT && Desc.Format != DXGI_FORMAT_R32_TYPELESS ) || Desc.SampleDesc.Count != 1 || InDepthHeap->GetDesc().Type != D3D12_DESCRIPTOR_HEAP_TYPE_DSV || InDepthHeap->GetDesc().NumDescriptors < 1 )
	{
		return false;
	}
	Width = InWidth;
	Height = InHeight;
	Depth = InDepth;
	DsvHeap = InDepthHeap;
	bOwnsDepth = false;
	return true;
}

bool CDX12GeometryRenderer::Resize( int32 InWidth, int32 InHeight )
{
	if ( !Device || Commands || !bOwnsDepth || !Depth || !DsvHeap || InWidth <= 0 || InHeight <= 0 || InWidth > 16384 || InHeight > 16384 || FAILED( Device->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	if ( InWidth == Width && InHeight == Height )
	{
		return true;
	}
	Microsoft::WRL::ComPtr<ID3D12Resource> NewDepth; // 再作成成功後にだけ差し替える仮深度。
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> NewHeap; // 再作成成功後にだけ差し替える仮DSV。
	if ( !CreateOwnedDepth( InWidth, InHeight, NewDepth, NewHeap ) )
	{
		return false;
	}
	Depth = MoveTemp( NewDepth );
	DsvHeap = MoveTemp( NewHeap );
	Width = InWidth;
	Height = InHeight;
	return true;
}

bool CDX12GeometryRenderer::BeginFrame( ID3D12GraphicsCommandList *InCommands, D3D12_CPU_DESCRIPTOR_HANDLE InRtv, ID3D12DescriptorHeap *InSrvHeap, const FRenderClipRect *InClip, bool bInClearDepth, float32 InClearDepth, uint64 InForbiddenTextureBinding )
{
	const D3D12_CPU_DESCRIPTOR_HANDLE Views[1] = { InRtv };
	const uint64 Forbidden[1] = { InForbiddenTextureBinding };
	return BeginFrame( InCommands, TArrayView<const D3D12_CPU_DESCRIPTOR_HANDLE>( Views, 1 ), InSrvHeap, InClip, bInClearDepth, InClearDepth, InForbiddenTextureBinding != 0 ? TArrayView<const uint64>( Forbidden, 1 ) : TArrayView<const uint64>() );
}

bool CDX12GeometryRenderer::BeginFrame( ID3D12GraphicsCommandList *InCommands, TArrayView<const D3D12_CPU_DESCRIPTOR_HANDLE> InColorViews, ID3D12DescriptorHeap *InSrvHeap, const FRenderClipRect *InClip, bool bInClearDepth, float32 InClearDepth, TArrayView<const uint64> InForbiddenTextureBindings, TArrayView<const ERenderTargetColorFormat> InColorFormats )
{
	if ( !Device || !Depth || !DsvHeap || Commands || !InCommands || InCommands->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT || !InColorViews.GetData() || InColorViews.Num() <= 0 || InColorViews.Num() > static_cast<int32>( FRenderTargetPassDesc::MaxColorTargets ) || ( !InColorFormats.IsEmpty() && ( !InColorFormats.GetData() || InColorFormats.Num() != InColorViews.Num() ) ) || InForbiddenTextureBindings.Num() < 0 || InForbiddenTextureBindings.Num() > static_cast<int32>( FRenderTargetPassDesc::MaxColorTargets ) + 1 || ( InForbiddenTextureBindings.Num() > 0 && !InForbiddenTextureBindings.GetData() ) || ( bInClearDepth && ( !NMath::IsFinite( InClearDepth ) || InClearDepth < 0.0f || InClearDepth > 1.0f ) ) )
	{
		return false;
	}
	for ( int32 i = 0; i < InColorFormats.Num(); ++i )
	{
		if ( InColorFormats[i] != ERenderTargetColorFormat::RGBA8 && InColorFormats[i] != ERenderTargetColorFormat::RGBA16F )
		{
			return false;
		}
	}
	for ( const D3D12_CPU_DESCRIPTOR_HANDLE View : InColorViews )
	{
		if ( !View.ptr )
		{
			return false;
		}
	}
	Microsoft::WRL::ComPtr<ID3D12Device> CommandDevice, ImageDevice; // 別のGPUの命令列や画像heapを混ぜない。
	if ( FAILED( InCommands->GetDevice( IID_PPV_ARGS( &CommandDevice ) ) ) || CommandDevice.Get() != Device.Get() )
	{
		return false;
	}
	if ( InSrvHeap && ( FAILED( InSrvHeap->GetDevice( IID_PPV_ARGS( &ImageDevice ) ) ) || ImageDevice.Get() != Device.Get() || InSrvHeap->GetDesc().Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || !( InSrvHeap->GetDesc().Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE ) ) )
	{
		return false;
	}
	uint64 NewUploadTicket = 0; // 新規フレームの頂点転送予約。失敗時は描画状態を汚さない。
	if ( UploadArena )
	{
		if ( !UploadArena->UsesDeviceAndQueue( Device.Get(), BufferQueue.Get() ) )
		{
			return false;
		}
		AbortUploadTicket();
		NewUploadTicket = UploadArena->BeginTicket();
		if ( NewUploadTicket == 0 )
		{
			return false;
		}
	}
	BufferPins.Reset();
	PinnedBytes = 0;
	BufferDrawCount = 0;
	TexturePins.Reset();
	PipelinePins.Reset();
	GraphicsDrawCount = 0;
	Uploads.Reset();
	UploadBytes = 0;
	UploadTicket = NewUploadTicket;
	ColorViewCount = static_cast<uint32>( InColorViews.Num() );
	for ( uint32 i = 0; i < ColorViewCount; ++i )
	{
		Rtvs[i] = InColorViews[static_cast<int32>( i )];
		ColorFormats[i] = InColorFormats.IsEmpty() ? ERenderTargetColorFormat::RGBA8 : InColorFormats[static_cast<int32>( i )];
	}
	ForbiddenBindingCount = 0;
	for ( const uint64 Forbidden : InForbiddenTextureBindings )
	{
		if ( Forbidden != 0 && ForbiddenBindingCount <= FRenderTargetPassDesc::MaxColorTargets )
		{
			ForbiddenTextureBindings[ForbiddenBindingCount++] = Forbidden;
		}
	}
	Commands = InCommands;
	SrvHeap = InSrvHeap;
	Clip = InClip;
	bHazardRejected = false;
	if ( bInClearDepth )
	{
		ClearDepthValue( InClearDepth );
	}
	return true;
}

void CDX12GeometryRenderer::EndFrame()
{
	Commands = nullptr;
	ColorViewCount = 0;
	ForbiddenBindingCount = 0;
	SrvHeap = nullptr;
	Clip = nullptr;
}

uint64 CDX12GeometryRenderer::TakeUploadTicket()
{
	if ( Commands )
	{
		return 0;
	}
	const uint64 Ticket = UploadTicket;
	UploadTicket = 0;
	return Ticket;
}

void CDX12GeometryRenderer::AbortUploadTicket()
{
	if ( UploadArena && UploadTicket != 0 )
	{
		UploadArena->AbortTicket( UploadTicket );
	}
	UploadTicket = 0;
}

bool CDX12GeometryRenderer::DidFrameSucceed() const
{
	return !bHazardRejected;
}

void CDX12GeometryRenderer::MoveFrameResources( TArray<FDX12BufferBinding> &OutBuffers, TArray<Microsoft::WRL::ComPtr<ID3D12Resource>> &OutUploads )
{
	for ( FDX12BufferBinding &Pin : BufferPins )
	{
		OutBuffers.Add( MoveTemp( Pin ) );
	}
	BufferPins.Reset();
	PinnedBytes = 0;
	for ( FUploadRecord &Record : Uploads )
	{
		OutUploads.Add( MoveTemp( Record.Resource ) );
	}
	Uploads.Reset();
	UploadBytes = 0;
}

void CDX12GeometryRenderer::MoveFrameResources( TArray<FDX12BufferBinding> &OutBuffers, TArray<Microsoft::WRL::ComPtr<ID3D12Resource>> &OutUploads, TArray<FDX12TextureBinding> &OutTextures, TArray<FDX12GraphicsPipelineBinding> &OutPipelines )
{
	MoveFrameResources( OutBuffers, OutUploads );
	for ( FDX12TextureBinding &Pin : TexturePins )
	{
		OutTextures.Add( MoveTemp( Pin ) );
	}
	TexturePins.Reset();
	for ( FDX12GraphicsPipelineBinding &Pin : PipelinePins )
	{
		OutPipelines.Add( MoveTemp( Pin ) );
	}
	PipelinePins.Reset();
}

int32 CDX12GeometryRenderer::GetWidth() const
{
	return Width;
}

int32 CDX12GeometryRenderer::GetHeight() const
{
	return Height;
}

bool CDX12GeometryRenderer::DrawGeometry( const FGeometryVertex *Vertices, int32 NumVertices, const FGeometryDrawState &State )
{
	return DrawInternal( Vertices, NumVertices, State, nullptr );
}

bool CDX12GeometryRenderer::DrawTexturedGeometry( const FGeometryVertex *Vertices, int32 NumVertices, const FGeometryDrawState &State, const FTextureDrawView &Texture )
{
	return DrawInternal( Vertices, NumVertices, State, &Texture );
}

bool CDX12GeometryRenderer::IsForbiddenTextureBinding( UINT64 Binding ) const
{
	if ( Binding == 0 )
	{
		return false;
	}
	for ( uint32 i = 0; i < ForbiddenBindingCount; ++i )
	{
		if ( ForbiddenTextureBindings[i] == Binding )
		{
			return true;
		}
	}
	return false;
}

bool CDX12GeometryRenderer::IsValidSrvHandle( UINT64 Binding ) const
{
	if ( !SrvHeap || Binding == 0 )
	{
		return false;
	}
	const UINT64 Base = SrvHeap->GetGPUDescriptorHandleForHeapStart().ptr; // 同じ画像heapの範囲だけを使う。
	const UINT Stride = Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ); // 画像記述子の幅。
	return Binding >= Base && ( Binding - Base ) % Stride == 0 && ( Binding - Base ) / Stride < SrvHeap->GetDesc().NumDescriptors;
}

bool CDX12GeometryRenderer::ResolveViewportAndScissor( D3D12_VIEWPORT &OutViewport, D3D12_RECT &OutScissor ) const
{
	const FRenderClipRect Area = Clip ? *Clip : FRenderClipRect{ 0, 0, Width, Height }; // UI座標ではなく描画先の画素を使う。
	OutScissor = { NMath::Clamp( Area.Left, 0, Width ), NMath::Clamp( Area.Top, 0, Height ), NMath::Clamp( Area.Right, 0, Width ), NMath::Clamp( Area.Bottom, 0, Height ) };
	if ( OutScissor.left >= OutScissor.right || OutScissor.top >= OutScissor.bottom )
	{
		return false;
	}
	OutViewport = { 0, 0, static_cast<float>( Width ), static_cast<float>( Height ), 0, 1 };
	return true;
}

bool CDX12GeometryRenderer::CanCommitBufferPins( TArrayView<const FDX12BufferBinding> Candidates, int32 &OutNewCount, uint64 &OutAddedBytes ) const
{
	OutNewCount = 0;
	OutAddedBytes = 0;
	for ( int32 i = 0; i < Candidates.Num(); ++i )
	{
		ID3D12Resource *Resource = Candidates[i].Resource.Get(); // 重複した資源は参照数と保持量を増やさない。
		bool bSeen = false;
		for ( const FDX12BufferBinding &Existing : BufferPins )
		{
			if ( Existing.Resource.Get() == Resource )
			{
				bSeen = true;
				break;
			}
		}
		for ( int32 Earlier = 0; !bSeen && Earlier < i; ++Earlier )
		{
			if ( Candidates[Earlier].Resource.Get() == Resource )
			{
				bSeen = true;
			}
		}
		if ( !bSeen )
		{
			++OutNewCount;
			if ( Candidates[i].Size > 256ull * 1024 * 1024 - PinnedBytes - OutAddedBytes )
			{
				return false;
			}
			OutAddedBytes += Candidates[i].Size;
		}
	}
	return BufferPins.Num() + OutNewCount <= 256;
}

bool CDX12GeometryRenderer::CommitBufferPins( TArrayView<const FDX12BufferBinding> Candidates )
{
	int32 NewCount = 0;
	uint64 AddedBytes = 0;
	if ( !CanCommitBufferPins( Candidates, NewCount, AddedBytes ) )
	{
		return false;
	}
	for ( const FDX12BufferBinding &Candidate : Candidates )
	{
		bool bPinned = false;
		for ( const FDX12BufferBinding &Existing : BufferPins )
		{
			if ( Existing.Resource.Get() == Candidate.Resource.Get() )
			{
				bPinned = true;
				break;
			}
		}
		if ( !bPinned )
		{
			BufferPins.Add( Candidate );
		}
	}
	PinnedBytes += AddedBytes;
	return true;
}

bool CDX12GeometryRenderer::CommitGraphicsAssetPins( TArrayView<const FDX12TextureBinding> Textures, const FDX12GraphicsPipelineBinding &Pipeline )
{
	int32 NewTextures = 0; // 未保持の新規画像実体数。
	for ( int32 i = 0; i < Textures.Num(); ++i )
	{
		ID3D12Resource *Resource = Textures[i].Resource.Get();
		bool bSeen = false;
		for ( const FDX12TextureBinding &Existing : TexturePins )
		{
			if ( Existing.Resource.Get() == Resource )
			{
				bSeen = true;
				break;
			}
		}
		for ( int32 Earlier = 0; !bSeen && Earlier < i; ++Earlier )
		{
			if ( Textures[Earlier].Resource.Get() == Resource )
			{
				bSeen = true;
			}
		}
		if ( !bSeen )
		{
			++NewTextures;
		}
	}
	bool bPipelinePinned = false; // 同じPSOの再描画では保持数を増やさない。
	for ( const FDX12GraphicsPipelineBinding &Existing : PipelinePins )
	{
		if ( Existing.Pipeline.Get() == Pipeline.Pipeline.Get() )
		{
			bPipelinePinned = true;
			break;
		}
	}
	if ( TexturePins.Num() + NewTextures > 256 || PipelinePins.Num() + ( bPipelinePinned ? 0 : 1 ) > 256 )
	{
		return false;
	}
	for ( const FDX12TextureBinding &Candidate : Textures )
	{
		bool bPinned = false;
		for ( const FDX12TextureBinding &Existing : TexturePins )
		{
			if ( Existing.Resource.Get() == Candidate.Resource.Get() )
			{
				bPinned = true;
				break;
			}
		}
		if ( !bPinned )
		{
			TexturePins.Add( Candidate );
		}
	}
	if ( !bPipelinePinned )
	{
		PipelinePins.Add( Pipeline );
	}
	return true;
}

bool CDX12GeometryRenderer::ValidateDrawState( const FGeometryDrawState &State, const FTextureDrawView *Texture ) const
{
	if ( !Commands || ColorViewCount != 1 || ColorFormats[0] != ERenderTargetColorFormat::RGBA8 || State.Topology > EGeometryTopology::Triangles || State.Compare > EGeometryCompare::Always || State.Culling > EGeometryCulling::FrontFaces || State.AddressU > EGeometryTextureAddress::Border || State.AddressV > EGeometryTextureAddress::Border || ( Texture && ( !SrvHeap || Texture->Binding == 0 || Texture->Width <= 0 || Texture->Height <= 0 ) ) )
	{
		return false;
	}
	if ( Texture )
	{
		if ( IsForbiddenTextureBinding( Texture->Binding ) )
		{
			bHazardRejected = true;
			return false;
		}
		if ( !IsValidSrvHandle( Texture->Binding ) )
		{
			return false;
		}
	}
	return true;
}

bool CDX12GeometryRenderer::DrawInternal( const FGeometryVertex *Vertices, int32 NumVertices, const FGeometryDrawState &State, const FTextureDrawView *Texture )
{
	const int32 Group = State.Topology == EGeometryTopology::Points ? 1 : State.Topology == EGeometryTopology::Lines ? 2
																													 : 3; // リストの最小単位。
	if ( !ValidateDrawState( State, Texture ) || !Vertices || NumVertices <= 0 || NumVertices > 65536 || NumVertices % Group != 0 )
	{
		return false;
	}
	for ( int32 Index = 0; Index < NumVertices; ++Index )
	{
		if ( !NMath::IsFinite( Vertices[Index].U ) || !NMath::IsFinite( Vertices[Index].V ) )
		{
			return false;
		}
		for ( float32 Value : Vertices[Index].Position )
		{
			if ( !NMath::IsFinite( Value ) )
			{
				return false;
			}
		}
	}
	const UINT64 ByteWidth = static_cast<UINT64>( NumVertices ) * sizeof( FGeometryVertex ); // 今回の頂点バイト数。
	if ( UploadBytes + ByteWidth > 64ull * 1024ull * 1024ull )
	{
		return false;
	}
	ID3D12PipelineState *Pipeline = Pipelines.GetOrCreatePipeline( State, Texture != nullptr ); // 今回だけのGPU状態。
	if ( !Pipeline )
	{
		return false;
	}
	D3D12_GPU_VIRTUAL_ADDRESS VertexGpuAddress = 0; // IAへ束縛する頂点バッファGPUアドレス。
	if ( UploadArena && UploadTicket != 0 )
	{
		CDX12UploadArena::FAllocation Alloc = {}; // 永続マップ済みページからのサブ割り当て。
		if ( !UploadArena->Allocate( UploadTicket, ByteWidth, 16, Alloc ) )
		{
			return false;
		}
		FMemory::Memcpy( Alloc.CpuAddress, Vertices, static_cast<usize>( ByteWidth ) );
		VertexGpuAddress = Alloc.GpuAddress;
	}
	else
	{
		D3D12_HEAP_PROPERTIES Heap = {}; // CPUから一度書き、GPUが読む領域。
		Heap.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC Desc = {}; // 頂点を所有コピーするバッファ。
		Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		Desc.Width = ByteWidth;
		Desc.Height = 1;
		Desc.DepthOrArraySize = 1;
		Desc.MipLevels = 1;
		Desc.SampleDesc.Count = 1;
		Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		Microsoft::WRL::ComPtr<ID3D12Resource> Upload; // GPU待ちまで保持する領域。
		void *Mapped = nullptr; // CPUが書く先。
		const D3D12_RANGE NoRead = { 0, 0 }; // CPUは読み戻さない。
		if ( FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &Upload ) ) ) || FAILED( Upload->Map( 0, &NoRead, &Mapped ) ) )
		{
			return false;
		}
		FMemory::Memcpy( Mapped, Vertices, static_cast<usize>( ByteWidth ) );
		Upload->Unmap( 0, nullptr );
		VertexGpuAddress = Upload->GetGPUVirtualAddress();
		Uploads.Add( FUploadRecord{ MoveTemp( Upload ) } );
	}
	FPendingDraw Pending; // 検査済みのGPU参照を、この位置で命令列へ渡す。
	Pending.TextureBinding = Texture ? Texture->Binding : 0;
	Pending.SamplerIndex = static_cast<UINT>( State.AddressU ) | ( static_cast<UINT>( State.AddressV ) << 2 ) | ( State.bLinearFilter ? 16 : 0 );
	Pending.Pipeline = Pipeline;
	Pending.Vertices = { VertexGpuAddress, static_cast<UINT>( ByteWidth ), sizeof( FGeometryVertex ) };
	Pending.NumVertices = static_cast<UINT>( NumVertices );
	Pending.Topology = State.Topology == EGeometryTopology::Points	? D3D_PRIMITIVE_TOPOLOGY_POINTLIST
					   : State.Topology == EGeometryTopology::Lines ? D3D_PRIMITIVE_TOPOLOGY_LINELIST
																	: D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	UploadBytes += ByteWidth;
	DrawPrepared( Pending );
	return true;
}

void CDX12GeometryRenderer::DrawPrepared( const FPendingDraw &Pending )
{
	D3D12_VIEWPORT Viewport = {};
	D3D12_RECT Scissor = {};
	if ( !ResolveViewportAndScissor( Viewport, Scissor ) )
	{
		return;
	}
	const auto Dsv = DsvHeap->GetCPUDescriptorHandleForHeapStart(); // 所有する深度。
	Commands->OMSetRenderTargets( 1, &Rtvs[0], FALSE, &Dsv );
	Commands->RSSetViewports( 1, &Viewport );
	Commands->RSSetScissorRects( 1, &Scissor );
	ID3D12DescriptorHeap *Heaps[] = { SrvHeap, SamplerHeap.Get() }; // 同じroot設定の連続描画でも、参照するheapをすべて保持する。
	Commands->SetDescriptorHeaps( SrvHeap ? 2 : 1, SrvHeap ? Heaps : &Heaps[1] );
	Commands->SetGraphicsRootSignature( Pipelines.GetRootSignature() );
	if ( Pending.TextureBinding != 0 )
	{
		Commands->SetGraphicsRootDescriptorTable( 0, D3D12_GPU_DESCRIPTOR_HANDLE{ Pending.TextureBinding } );
		auto Sampler = SamplerHeap->GetGPUDescriptorHandleForHeapStart(); // この命令に固定した補間。
		Sampler.ptr += Pending.SamplerIndex * SamplerStride;
		Commands->SetGraphicsRootDescriptorTable( 1, Sampler );
	}
	Commands->SetPipelineState( Pending.Pipeline );
	Commands->IASetPrimitiveTopology( Pending.Topology );
	Commands->IASetVertexBuffers( 0, 1, &Pending.Vertices );
	Commands->IASetIndexBuffer( Pending.NumIndices ? &Pending.Indices : nullptr );
	if ( Pending.NumIndices )
	{
		Commands->DrawIndexedInstanced( Pending.NumIndices, 1, 0, 0, 0 );
	}
	else
	{
		Commands->DrawInstanced( Pending.NumVertices, 1, 0, 0 );
	}
	Commands->OMSetRenderTargets( 1, &Rtvs[0], FALSE, nullptr );
}

void CDX12GeometryRenderer::ClearDepth()
{
	ClearDepthValue( 1.0f );
}

bool CDX12GeometryRenderer::ClearDepthValue( float32 DepthValue )
{
	if ( !Commands || ColorViewCount == 0 || !Depth || !DsvHeap || !NMath::IsFinite( DepthValue ) || DepthValue < 0.0f || DepthValue > 1.0f )
	{
		return false;
	}
	Commands->ClearDepthStencilView( DsvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, DepthValue, 0, 0, nullptr );
	return true;
}

bool CDX12GeometryRenderer::ClearColor( FColor32 Color )
{
	const FGeometryVertex Vertices[] = { { { -1, -1, 0, 1 }, Color }, { { -1, 3, 0, 1 }, Color }, { { 3, -1, 0, 1 }, Color } }; // 深度を変えずに全画素を覆う一枚。
	FGeometryDrawState State; // 合成せずRGBAを置き換える。
	State.bTestDepth = State.bWriteDepth = State.bAlphaBlend = false;
	const FRenderClipRect *Previous = Clip; // 色の消去だけは切り取りに依存しない。
	Clip = nullptr;
	const bool bResult = DrawGeometry( Vertices, 3, State ); // 頂点の所有は通常の描画と同じ扱い。
	Clip = Previous;
	return bResult;
}

bool CDX12GeometryRenderer::DrawBufferGeometry( const FGeometryBufferDraw &Draw, const FGeometryDrawState &State, const FTextureDrawView *Texture )
{
	static_assert( sizeof( FGeometryVertex ) == 28 && offsetof( FGeometryVertex, Position ) == 0 && offsetof( FGeometryVertex, Color ) == 16 && offsetof( FGeometryVertex, U ) == 20 && offsetof( FGeometryVertex, V ) == 24, "GPU geometry uses the fixed clip vertex layout" );
	const bool bIndexed = Draw.IndexBuffer.IsValid(); // 頂点番号の有無は番号の有効値で明示する。
	// プリミティブを構成する最小の頂点数。
	const int32 Group = State.Topology == EGeometryTopology::Points ? 1 : State.Topology == EGeometryTopology::Lines ? 2
																													 : 3;
	if ( !ValidateDrawState( State, Texture ) || !Buffers || !Buffers->UsesDeviceAndQueue( Device.Get(), BufferQueue.Get() ) || BufferDrawCount >= 1024 || Draw.VertexCount <= 0 || Draw.VertexCount > 65536 || Draw.VertexOffset % 4 != 0 || Draw.IndexFormat > EGeometryIndexFormat::UInt32 || ( bIndexed ? Draw.IndexCount <= 0 || Draw.IndexCount > 196608 || Draw.IndexCount % Group != 0 : Draw.IndexCount != 0 || Draw.IndexOffset != 0 || Draw.VertexCount % Group != 0 ) )
	{
		return false;
	}
	FDX12BufferBinding Vertices, Indices; // 全範囲を検査し終わるまでGPU命令は記録しない。
	if ( !Buffers->AcquireGeometryBuffer( Draw.VertexBuffer, Vertices ) || ( bIndexed && !Buffers->AcquireGeometryBuffer( Draw.IndexBuffer, Indices ) ) )
	{
		return false;
	}
	const uint64 VertexBytes = static_cast<uint64>( Draw.VertexCount ) * sizeof( FGeometryVertex ); // 頂点viewに公開する範囲。
	const uint64 IndexStride = Draw.IndexFormat == EGeometryIndexFormat::UInt16 ? 2 : 4; // indexの整数幅。
	const uint64 IndexBytes = bIndexed ? static_cast<uint64>( Draw.IndexCount ) * IndexStride : 0; // 未使用なら番号列を参照しない。
	if ( Draw.VertexOffset > Vertices.Size || VertexBytes > Vertices.Size - Draw.VertexOffset || ( bIndexed && ( Draw.IndexOffset % IndexStride != 0 || Draw.IndexOffset > Indices.Size || IndexBytes > Indices.Size - Draw.IndexOffset ) ) )
	{
		return false;
	}
	const FDX12BufferBinding Candidates[2] = { Vertices, Indices };
	ID3D12PipelineState *Pipeline = Pipelines.GetOrCreatePipeline( State, Texture != nullptr ); // すべての検査後に描画状態を用意する。
	if ( !Pipeline || !CommitBufferPins( TArrayView<const FDX12BufferBinding>( Candidates, bIndexed ? 2 : 1 ) ) )
	{
		return false;
	}
	FPendingDraw Pending; // GPUアドレスと状態は呼出中に固定する。バッファ内容はGPUが実行時に読む。
	Pending.Pipeline = Pipeline;
	Pending.Vertices = { Vertices.Resource->GetGPUVirtualAddress() + Draw.VertexOffset, static_cast<UINT>( VertexBytes ), sizeof( FGeometryVertex ) };
	Pending.NumVertices = static_cast<UINT>( Draw.VertexCount );
	Pending.TextureBinding = Texture ? Texture->Binding : 0;
	Pending.SamplerIndex = static_cast<UINT>( State.AddressU ) | ( static_cast<UINT>( State.AddressV ) << 2 ) | ( State.bLinearFilter ? 16 : 0 );
	Pending.Topology = ToD3DTopology( State.Topology );
	if ( bIndexed )
	{
		Pending.Indices = { Indices.Resource->GetGPUVirtualAddress() + Draw.IndexOffset, static_cast<UINT>( IndexBytes ), Draw.IndexFormat == EGeometryIndexFormat::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT };
		Pending.NumIndices = static_cast<UINT>( Draw.IndexCount );
	}
	++BufferDrawCount;
	TArray<D3D12_RESOURCE_BARRIER> Barriers; // 読み取り用途をまとめ、同じ実体へ二重の遷移を記録しない。
	AccumulateBufferReadState( Barriers, Vertices.Resource.Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER );
	if ( bIndexed )
	{
		AccumulateBufferReadState( Barriers, Indices.Resource.Get(), D3D12_RESOURCE_STATE_INDEX_BUFFER );
	}
	Commands->ResourceBarrier( static_cast<UINT>( Barriers.Num() ), Barriers.GetData() );
	DrawPrepared( Pending );
	for ( D3D12_RESOURCE_BARRIER &Barrier : Barriers )
	{
		Barrier.Transition.StateBefore = Barrier.Transition.StateAfter;
		Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
	}
	Commands->ResourceBarrier( static_cast<UINT>( Barriers.Num() ), Barriers.GetData() );
	return true;
}

bool CDX12GeometryRenderer::DrawGraphicsPipeline( const FGraphicsPipelineDraw &Draw )
{
	if ( !Commands || ColorViewCount == 0 || !GraphicsPipelines || !Buffers || !Buffers->UsesDeviceAndQueue( Device.Get(), BufferQueue.Get() ) || GraphicsDrawCount >= 1024 )
	{
		return false;
	}
	FDX12GraphicsPipelineBinding Pipeline; // PSOとスロット宣言を取得する。
	if ( !GraphicsPipelines->AcquirePipeline( Draw.Pipeline, Pipeline ) || !Pipeline.Pipeline || !Pipeline.Root || Pipeline.NumRenderTargets != ColorViewCount || ( Pipeline.DepthFormat == EGraphicsDepthFormat::D32Float && ( !Depth || !DsvHeap ) ) )
	{
		return false;
	}
	for ( uint32 i = 0; i < ColorViewCount; ++i )
	{
		if ( Pipeline.RenderTargetFormats[i] != ColorFormats[i] )
		{
			return false;
		}
	}
	const bool bIndexed = Draw.IndexBuffer.IsValid(); // index付き描画の有無。
	const uint32 Group = Pipeline.Topology == EGeometryTopology::Points ? 1u : Pipeline.Topology == EGeometryTopology::Lines ? 2u
																															  : 3u;
	if ( Draw.VertexCount == 0 || Draw.VertexCount > 65536 || ( Draw.VertexOffset % 4 ) != 0 || Draw.IndexFormat > EGeometryIndexFormat::UInt32 || ( bIndexed ? Draw.IndexCount == 0 || Draw.IndexCount > 196608 || ( Draw.IndexCount % Group ) != 0 : ( Draw.IndexCount != 0 || Draw.IndexOffset != 0 || ( Draw.VertexCount % Group ) != 0 ) ) )
	{
		return false;
	}
	if ( Draw.Buffers.Num() != Pipeline.BufferSlots.Num() || ( Draw.Buffers.Num() > 0 && !Draw.Buffers.GetData() ) || Draw.Textures.Num() != Pipeline.TextureSlots.Num() || ( Draw.Textures.Num() > 0 && !Draw.Textures.GetData() ) )
	{
		return false;
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
	TArray<FDX12BufferBinding> SlotBuffers; // Root CBV／Root SRVとして結合するバッファ。
	SlotBuffers.SetNum( Pipeline.BufferSlots.Num() );
	for ( int32 i = 0; i < Pipeline.BufferSlots.Num(); ++i )
	{
		const FGraphicsBufferSlot &Slot = Pipeline.BufferSlots[i];
		if ( !Buffers->AcquireComputeBuffer( Draw.Buffers[i], ERenderBufferShaderAccess::Read, SlotBuffers[i] ) )
		{
			return false;
		}
		const uint64 Size = SlotBuffers[i].Size;
		if ( Slot.Layout == EGraphicsBufferLayout::Constant )
		{
			if ( Size == 0 || ( Size % 256 ) != 0 || Size > 65536 || Size < Pipeline.MinimumBytes[i] )
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
	if ( !Pipeline.TextureSlots.IsEmpty() )
	{
		if ( !TextureStore || !SrvHeap )
		{
			return false;
		}
		SlotTextures.SetNum( Pipeline.TextureSlots.Num() );
		for ( int32 i = 0; i < Pipeline.TextureSlots.Num(); ++i )
		{
			const FTextureResourceHandle TexHandle = Draw.Textures[i];
			const FGraphicsTextureSlot &Slot = Pipeline.TextureSlots[i];
			if ( !TextureStore->AcquireGraphicsTexture( TexHandle, Slot.Format, SlotTextures[i] ) )
			{
				if ( TextureStore->IsActiveTarget( TexHandle ) )
				{
					bHazardRejected = true;
				}
				return false;
			}
			if ( !IsValidSrvHandle( SlotTextures[i].SrvGpuDescriptor ) )
			{
				return false;
			}
			if ( IsForbiddenTextureBinding( SlotTextures[i].SrvGpuDescriptor ) )
			{
				bHazardRejected = true;
				return false;
			}
		}
	}
	D3D12_VIEWPORT Viewport = {};
	D3D12_RECT Scissor = {};
	if ( !ResolveViewportAndScissor( Viewport, Scissor ) )
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
	if ( !CanCommitBufferPins( TArrayView<const FDX12BufferBinding>( BufferCandidates.GetData(), BufferCandidates.Num() ), NewBufferPinCount, AddedBufferBytes ) || !CommitGraphicsAssetPins( TArrayView<const FDX12TextureBinding>( SlotTextures.GetData(), SlotTextures.Num() ), Pipeline ) )
	{
		return false;
	}
	CommitBufferPins( TArrayView<const FDX12BufferBinding>( BufferCandidates.GetData(), BufferCandidates.Num() ) );
	++GraphicsDrawCount;

	TArray<D3D12_RESOURCE_BARRIER> Barriers; // 同一バッファが多用途で重複しても1回の遷移へまとめる。
	AccumulateBufferReadState( Barriers, Vertices.Resource.Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER );
	if ( bIndexed )
	{
		AccumulateBufferReadState( Barriers, Indices.Resource.Get(), D3D12_RESOURCE_STATE_INDEX_BUFFER );
	}
	for ( int32 i = 0; i < SlotBuffers.Num(); ++i )
	{
		const D3D12_RESOURCE_STATES RoleState = Pipeline.BufferSlots[i].Layout == EGraphicsBufferLayout::Constant ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER : ( D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		AccumulateBufferReadState( Barriers, SlotBuffers[i].Resource.Get(), RoleState );
	}
	Commands->ResourceBarrier( static_cast<UINT>( Barriers.Num() ), Barriers.GetData() );

	if ( SrvHeap )
	{
		ID3D12DescriptorHeap *Heaps[] = { SrvHeap };
		Commands->SetDescriptorHeaps( 1, Heaps );
	}
	Commands->SetGraphicsRootSignature( Pipeline.Root.Get() );
	Commands->SetPipelineState( Pipeline.Pipeline.Get() );
	for ( int32 i = 0; i < SlotBuffers.Num(); ++i )
	{
		const UINT RootIndex = static_cast<UINT>( i );
		const D3D12_GPU_VIRTUAL_ADDRESS GpuAddress = SlotBuffers[i].Resource->GetGPUVirtualAddress();
		if ( Pipeline.BufferSlots[i].Layout == EGraphicsBufferLayout::Constant )
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
		const UINT RootIndex = static_cast<UINT>( SlotBuffers.Num() + i );
		Commands->SetGraphicsRootDescriptorTable( RootIndex, D3D12_GPU_DESCRIPTOR_HANDLE{ SlotTextures[i].SrvGpuDescriptor } );
	}
	const D3D12_CPU_DESCRIPTOR_HANDLE Dsv = DsvHeap ? DsvHeap->GetCPUDescriptorHandleForHeapStart() : D3D12_CPU_DESCRIPTOR_HANDLE{};
	Commands->OMSetRenderTargets( ColorViewCount, Rtvs, FALSE, Pipeline.DepthFormat == EGraphicsDepthFormat::D32Float ? &Dsv : nullptr );
	Commands->RSSetViewports( 1, &Viewport );
	Commands->RSSetScissorRects( 1, &Scissor );
	Commands->IASetPrimitiveTopology( ToD3DTopology( Pipeline.Topology ) );
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
	Commands->OMSetRenderTargets( ColorViewCount, Rtvs, FALSE, nullptr );
	for ( D3D12_RESOURCE_BARRIER &Barrier : Barriers )
	{
		Barrier.Transition.StateBefore = Barrier.Transition.StateAfter;
		Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
	}
	Commands->ResourceBarrier( static_cast<UINT>( Barriers.Num() ), Barriers.GetData() );
	return true;
}
