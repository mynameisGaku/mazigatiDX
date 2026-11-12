// SPDX-License-Identifier: MIT
#include "DX12GeometryRenderer.h"
#include "Library/Platform/DX12/Resource/Buffer/DX12BufferStore.h"
#include <cstddef>
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"

bool CDX12GeometryRenderer::Initialize( ID3D12Device *InDevice, int32 InWidth, int32 InHeight, ID3D12Resource *InDepth, ID3D12DescriptorHeap *InDepthHeap, CDX12BufferStore *InBuffers, ID3D12CommandQueue *InQueue )
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
	if ( ( InBuffers == nullptr ) != ( InQueue == nullptr ) || ( InBuffers && !InBuffers->UsesDeviceAndQueue( InDevice, InQueue ) ) )
	{
		return false;
	}
	Buffers = InBuffers;
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
		return true;
	}
	D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {}; // 通常描画では専用の深度を作る。
	HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	HeapDesc.NumDescriptors = 1;
	D3D12_HEAP_PROPERTIES Heap = {}; // GPUが読み書きする領域。
	Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC Desc = {}; // 深度画像。
	Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	Desc.Width = Width;
	Desc.Height = Height;
	Desc.DepthOrArraySize = 1;
	Desc.MipLevels = 1;
	Desc.Format = DXGI_FORMAT_D32_FLOAT;
	Desc.SampleDesc.Count = 1;
	Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	D3D12_CLEAR_VALUE Clear = {}; // 未描画部分の深度。
	Clear.Format = Desc.Format;
	Clear.DepthStencil.Depth = 1;
	if ( FAILED( Device->CreateDescriptorHeap( &HeapDesc, IID_PPV_ARGS( &DsvHeap ) ) ) || FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &Clear, IID_PPV_ARGS( &Depth ) ) ) )
	{
		return false;
	}
	Device->CreateDepthStencilView( Depth.Get(), nullptr, DsvHeap->GetCPUDescriptorHandleForHeapStart() );
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
	return true;
}

bool CDX12GeometryRenderer::BeginFrame( ID3D12GraphicsCommandList *InCommands, D3D12_CPU_DESCRIPTOR_HANDLE InRtv, ID3D12DescriptorHeap *InSrvHeap, const FRenderClipRect *InClip, bool bInClearDepth, float32 InClearDepth, uint64 InForbiddenTextureBinding )
{
	if ( !Device || !Depth || !DsvHeap || Commands || !InCommands || InCommands->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT || !InRtv.ptr || ( bInClearDepth && ( !NMath::IsFinite( InClearDepth ) || InClearDepth < 0.0f || InClearDepth > 1.0f ) ) )
	{
		return false;
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
	BufferPins.Reset();
	PinnedBytes = 0;
	BufferDrawCount = 0;
	Uploads.Reset();
	UploadBytes = 0;
	Commands = InCommands;
	Rtv = InRtv;
	SrvHeap = InSrvHeap;
	Clip = InClip;
	ForbiddenTextureBinding = InForbiddenTextureBinding;
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
	SrvHeap = nullptr;
	Clip = nullptr;
	ForbiddenTextureBinding = 0;
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

bool CDX12GeometryRenderer::ValidateDrawState( const FGeometryDrawState &State, const FTextureDrawView *Texture ) const
{
	if ( !Commands || State.Topology > EGeometryTopology::Triangles || State.Compare > EGeometryCompare::Always || State.Culling > EGeometryCulling::FrontFaces || State.AddressU > EGeometryTextureAddress::Border || State.AddressV > EGeometryTextureAddress::Border || ( Texture && ( !SrvHeap || Texture->Binding == 0 || Texture->Width <= 0 || Texture->Height <= 0 ) ) )
	{
		return false;
	}
	if ( Texture )
	{
		if ( ForbiddenTextureBinding != 0 && Texture->Binding == ForbiddenTextureBinding )
		{
			bHazardRejected = true;
			return false;
		}
		const UINT64 Base = SrvHeap->GetGPUDescriptorHandleForHeapStart().ptr; // 同じ画像heapの範囲だけを使う。
		const UINT Stride = Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ); // 画像記述子の幅。
		if ( Texture->Binding < Base || ( Texture->Binding - Base ) % Stride != 0 || ( Texture->Binding - Base ) / Stride >= SrvHeap->GetDesc().NumDescriptors )
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
	if ( UploadBytes + static_cast<uint64>( NumVertices ) * sizeof( FGeometryVertex ) > 64 * 1024 * 1024 )
	{
		return false;
	}
	ID3D12PipelineState *Pipeline = Pipelines.GetOrCreatePipeline( State, Texture != nullptr ); // 今回だけのGPU状態。
	if ( !Pipeline )
	{
		return false;
	}
	D3D12_HEAP_PROPERTIES Heap = {}; // CPUから一度書き、GPUが読む領域。
	Heap.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC Desc = {}; // 頂点を所有コピーするバッファ。
	Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	Desc.Width = static_cast<UINT64>( NumVertices ) * sizeof( FGeometryVertex );
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
	FMemory::Memcpy( Mapped, Vertices, static_cast<usize>( Desc.Width ) );
	Upload->Unmap( 0, nullptr );
	FPendingDraw Pending; // 検査済みのGPU参照を、この位置で命令列へ渡す。
	Pending.TextureBinding = Texture ? Texture->Binding : 0;
	Pending.SamplerIndex = static_cast<UINT>( State.AddressU ) | ( static_cast<UINT>( State.AddressV ) << 2 ) | ( State.bLinearFilter ? 16 : 0 );
	Pending.Pipeline = Pipeline;
	Pending.Vertices = { Upload->GetGPUVirtualAddress(), static_cast<UINT>( Desc.Width ), sizeof( FGeometryVertex ) };
	Pending.NumVertices = static_cast<UINT>( NumVertices );
	Pending.Topology = State.Topology == EGeometryTopology::Points	? D3D_PRIMITIVE_TOPOLOGY_POINTLIST
					   : State.Topology == EGeometryTopology::Lines ? D3D_PRIMITIVE_TOPOLOGY_LINELIST
																	: D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Uploads.Add( FUploadRecord{ MoveTemp( Upload ) } );
	UploadBytes += Desc.Width;
	DrawPrepared( Pending );
	return true;
}

void CDX12GeometryRenderer::DrawPrepared( const FPendingDraw &Pending )
{
	const FRenderClipRect Area = Clip ? *Clip : FRenderClipRect{ 0, 0, Width, Height }; // UI座標ではなく描画先の画素を使う。
	const D3D12_RECT Scissor = { NMath::Clamp( Area.Left, 0, Width ), NMath::Clamp( Area.Top, 0, Height ), NMath::Clamp( Area.Right, 0, Width ), NMath::Clamp( Area.Bottom, 0, Height ) }; // 現在の切り取り範囲。
	if ( Scissor.left >= Scissor.right || Scissor.top >= Scissor.bottom )
	{
		return;
	}
	const D3D12_VIEWPORT Viewport = { 0, 0, static_cast<float>( Width ), static_cast<float>( Height ), 0, 1 }; // 論理解像度。
	const auto Dsv = DsvHeap->GetCPUDescriptorHandleForHeapStart(); // 所有する深度。
	Commands->OMSetRenderTargets( 1, &Rtv, FALSE, &Dsv );
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
	Commands->OMSetRenderTargets( 1, &Rtv, FALSE, nullptr );
}

void CDX12GeometryRenderer::ClearDepth()
{
	ClearDepthValue( 1.0f );
}

bool CDX12GeometryRenderer::ClearDepthValue( float32 DepthValue )
{
	if ( !Commands || !NMath::IsFinite( DepthValue ) || DepthValue < 0.0f || DepthValue > 1.0f )
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
	const auto HasPin = [&]( ID3D12Resource *Resource ) // 重複した資源は参照数と保持量を増やさない。
	{
		for ( const FDX12BufferBinding &Pin : BufferPins )
		{
			if ( Pin.Resource.Get() == Resource )
			{
				return true;
			}
		}
		return false;
	};
	const bool bPinVertices = !HasPin( Vertices.Resource.Get() ); // この描画で初めて参照する頂点実体。
	const bool bPinIndices = bIndexed && Indices.Resource.Get() != Vertices.Resource.Get() && !HasPin( Indices.Resource.Get() ); // 同じ実体のVBとIBは一度だけ保持する。
	const uint64 AddedBytes = ( bPinVertices ? Vertices.Size : 0 ) + ( bPinIndices ? Indices.Size : 0 ); // 部分viewでも、保持する実体全体の容量を数える。
	if ( BufferPins.Num() + static_cast<int32>( bPinVertices ) + static_cast<int32>( bPinIndices ) > 256 || AddedBytes > 256ull * 1024 * 1024 - PinnedBytes )
	{
		return false;
	}
	ID3D12PipelineState *Pipeline = Pipelines.GetOrCreatePipeline( State, Texture != nullptr ); // すべての検査後に描画状態を用意する。
	if ( !Pipeline )
	{
		return false;
	}
	FPendingDraw Pending; // GPUアドレスと状態は呼出中に固定する。バッファ内容はGPUが実行時に読む。
	Pending.Pipeline = Pipeline;
	Pending.Vertices = { Vertices.Resource->GetGPUVirtualAddress() + Draw.VertexOffset, static_cast<UINT>( VertexBytes ), sizeof( FGeometryVertex ) };
	Pending.NumVertices = static_cast<UINT>( Draw.VertexCount );
	Pending.TextureBinding = Texture ? Texture->Binding : 0;
	Pending.SamplerIndex = static_cast<UINT>( State.AddressU ) | ( static_cast<UINT>( State.AddressV ) << 2 ) | ( State.bLinearFilter ? 16 : 0 );
	Pending.Topology = State.Topology == EGeometryTopology::Points ? D3D_PRIMITIVE_TOPOLOGY_POINTLIST : State.Topology == EGeometryTopology::Lines ? D3D_PRIMITIVE_TOPOLOGY_LINELIST
																																				   : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	if ( bIndexed )
	{
		Pending.Indices = { Indices.Resource->GetGPUVirtualAddress() + Draw.IndexOffset, static_cast<UINT>( IndexBytes ), Draw.IndexFormat == EGeometryIndexFormat::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT };
		Pending.NumIndices = static_cast<UINT>( Draw.IndexCount );
	}
	if ( bPinVertices )
	{
		BufferPins.Add( Vertices );
	}
	if ( bPinIndices )
	{
		BufferPins.Add( Indices );
	}
	PinnedBytes += AddedBytes;
	++BufferDrawCount;
	D3D12_RESOURCE_BARRIER Barriers[2] = {}; // 読み取り用途をまとめ、同じ実体へ二重の遷移を記録しない。
	Barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Barriers[0].Transition.pResource = Vertices.Resource.Get();
	Barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	Barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	Barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
	const bool bShared = bIndexed && Vertices.Resource.Get() == Indices.Resource.Get(); // 同一バッファに頂点とindexを格納した場合。
	const UINT Count = bIndexed && !bShared ? 2 : 1; // 実際に状態を変える資源の数。
	if ( bShared )
	{
		Barriers[0].Transition.StateAfter |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
	}
	if ( Count == 2 )
	{
		Barriers[1] = Barriers[0];
		Barriers[1].Transition.pResource = Indices.Resource.Get();
		Barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
	}
	Commands->ResourceBarrier( Count, Barriers );
	DrawPrepared( Pending );
	for ( UINT Index = 0; Index < Count; ++Index )
	{
		Barriers[Index].Transition.StateBefore = Barriers[Index].Transition.StateAfter;
		Barriers[Index].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
	}
	Commands->ResourceBarrier( Count, Barriers );
	return true;
}
