// SPDX-License-Identifier: MIT
#include "DX12GeometryRenderer.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"

bool FDX12GeometryRenderer::Initialize( ID3D12Device* InDevice, int32 InWidth, int32 InHeight )
{
	Device = InDevice;
	Width = InWidth;
	Height = InHeight;
	const char Source[] = "struct V { float4 p:POSITION; float4 c:COLOR; float2 uv:TEXCOORD; }; struct P { float4 p:SV_POSITION; float4 c:COLOR; float2 uv:TEXCOORD; }; P VS(V v) { P p; p.p=v.p; p.c=v.c; p.uv=v.uv; return p; } float4 PS(P p):SV_TARGET { return p.c; } Texture2D image:register(t0); SamplerState sampling:register(s0); float4 TexturePS(P p):SV_TARGET { float4 color=image.Sample(sampling,p.uv)*p.c; if(color.a<=0) discard; return color; }"; // 透視補間はGPUが行う。完全な透明画素は色・深度のどちらも書かない。
	if ( !Device || Width <= 0 || Height <= 0
		|| FAILED( D3DCompile( Source, sizeof( Source ) - 1, "Geometry", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &VertexShader, nullptr ) )
		|| FAILED( D3DCompile( Source, sizeof( Source ) - 1, "Geometry", nullptr, nullptr, "PS", "ps_5_0", 0, 0, &PixelShader, nullptr ) )
		|| FAILED( D3DCompile( Source, sizeof( Source ) - 1, "Geometry", nullptr, nullptr, "TexturePS", "ps_5_0", 0, 0, &TexturedPixelShader, nullptr ) ) )
	{
		return false;
	}
	D3D12_DESCRIPTOR_RANGE Ranges[2] = {}; // 画像と補間を独立したheapから参照する。
	Ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	Ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
	D3D12_ROOT_PARAMETER Parameters[2] = {}; // t0とs0をピクセルシェーダーへ渡す。
	for ( int32 Index = 0; Index < 2; ++Index )
	{
		Ranges[Index].NumDescriptors = 1;
		Parameters[Index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		Parameters[Index].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		Parameters[Index].DescriptorTable.NumDescriptorRanges = 1;
		Parameters[Index].DescriptorTable.pDescriptorRanges = &Ranges[Index];
	}
	D3D12_ROOT_SIGNATURE_DESC RootDesc = {}; // 画像なしのPSではdescriptorを参照しない。
	RootDesc.NumParameters = 2;
	RootDesc.pParameters = Parameters;
	RootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	Microsoft::WRL::ComPtr<ID3DBlob> Serialized; // GPUへ渡す設定。
	if ( FAILED( D3D12SerializeRootSignature( &RootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &Serialized, nullptr ) )
		|| FAILED( Device->CreateRootSignature( 0, Serialized->GetBufferPointer(), Serialized->GetBufferSize(), IID_PPV_ARGS( &RootSignature ) ) ) )
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
	D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {}; // 深度用は1枚。
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
	if ( FAILED( Device->CreateDescriptorHeap( &HeapDesc, IID_PPV_ARGS( &DsvHeap ) ) )
		|| FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &Clear, IID_PPV_ARGS( &Depth ) ) ) )
	{
		return false;
	}
	Device->CreateDepthStencilView( Depth.Get(), nullptr, DsvHeap->GetCPUDescriptorHandleForHeapStart() );
	return true;
}

void FDX12GeometryRenderer::BeginFrame( ID3D12GraphicsCommandList* InCommands, D3D12_CPU_DESCRIPTOR_HANDLE InRtv, ID3D12DescriptorHeap* InSrvHeap )
{
	Uploads.Reset();
	UploadBytes = 0;
	Commands = InCommands;
	Rtv = InRtv;
	SrvHeap = InSrvHeap;
	Commands->ClearDepthStencilView( DsvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr );
}

void FDX12GeometryRenderer::EndFrame()
{
	Commands = nullptr;
	SrvHeap = nullptr;
}

int32 FDX12GeometryRenderer::GetWidth() const
{
	return Width;
}

int32 FDX12GeometryRenderer::GetHeight() const
{
	return Height;
}

ID3D12PipelineState* FDX12GeometryRenderer::GetPipeline( const FGeometryDrawState& State, bool bTextured )
{
	const uint64 Key = ( static_cast<uint64>( static_cast<uint32>( State.DepthBias ) ) << 32 ) | static_cast<uint64>( State.Topology )
		| ( static_cast<uint64>( State.Compare ) << 2 ) | ( static_cast<uint64>( State.Culling ) << 5 )
		| ( static_cast<uint64>( State.bTestDepth ) << 7 ) | ( static_cast<uint64>( State.bWriteDepth ) << 8 ) | ( static_cast<uint64>( State.bAlphaBlend ) << 9 ) | ( static_cast<uint64>( bTextured ) << 10 ); // PSOを決める全要素を含める。補間はsamplerで指定する。
	if ( const auto* Found = Pipelines.Find( Key ) )
	{
		return Found->Resource.Get();
	}
	if ( Pipelines.Num() >= 128 )
	{
		return nullptr;
	}
	const D3D12_INPUT_ELEMENT_DESC Elements[] = { { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 } }; // 共通頂点の配置。
	D3D12_GRAPHICS_PIPELINE_STATE_DESC Desc = {}; // 状態を呼び出し単位で固定する。
	Desc.pRootSignature = RootSignature.Get();
	Desc.VS = { VertexShader->GetBufferPointer(), VertexShader->GetBufferSize() };
	ID3DBlob* Pixel = bTextured ? TexturedPixelShader.Get() : PixelShader.Get(); // 画像の有無でPSを固定する。
	Desc.PS = { Pixel->GetBufferPointer(), Pixel->GetBufferSize() };
	Desc.InputLayout = { Elements, 3 };
	Desc.SampleMask = UINT_MAX;
	Desc.PrimitiveTopologyType = State.Topology == EGeometryTopology::Points ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT
		: State.Topology == EGeometryTopology::Lines ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	Desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	Desc.RasterizerState.CullMode = State.Culling == EGeometryCulling::BackFaces ? D3D12_CULL_MODE_BACK
		: State.Culling == EGeometryCulling::FrontFaces ? D3D12_CULL_MODE_FRONT : D3D12_CULL_MODE_NONE;
	Desc.RasterizerState.DepthClipEnable = TRUE;
	Desc.RasterizerState.DepthBias = State.DepthBias;
	Desc.DepthStencilState.DepthEnable = State.bTestDepth || State.bWriteDepth;
	Desc.DepthStencilState.DepthWriteMask = State.bWriteDepth ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	Desc.DepthStencilState.DepthFunc = State.bTestDepth ? static_cast<D3D12_COMPARISON_FUNC>( static_cast<int32>( State.Compare ) + 1 ) : D3D12_COMPARISON_FUNC_ALWAYS;
	Desc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
	Desc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
	Desc.DepthStencilState.FrontFace = { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
	Desc.DepthStencilState.BackFace = Desc.DepthStencilState.FrontFace;
	auto& Blend = Desc.BlendState.RenderTarget[0]; // 通常のアルファ合成。
	Blend.BlendEnable = State.bAlphaBlend;
	Blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	Blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	Blend.BlendOp = D3D12_BLEND_OP_ADD;
	Blend.SrcBlendAlpha = D3D12_BLEND_ONE;
	Blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	Blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	Blend.LogicOp = D3D12_LOGIC_OP_NOOP;
	Blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	Desc.NumRenderTargets = 1;
	Desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	Desc.SampleDesc.Count = 1;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> Pipeline; // 失敗時はキャッシュを変更しない。
	if ( FAILED( Device->CreateGraphicsPipelineState( &Desc, IID_PPV_ARGS( &Pipeline ) ) ) )
	{
		return nullptr;
	}
	ID3D12PipelineState* Result = Pipeline.Get(); // キャッシュへ所有権を渡す前の借用値。
	Pipelines.Add( Key, FPipelineRecord { MoveTemp( Pipeline ) } );
	return Result;
}

bool FDX12GeometryRenderer::DrawGeometry( const FGeometryVertex* Vertices, int32 NumVertices, const FGeometryDrawState& State )
{
	return DrawInternal( Vertices, NumVertices, State, nullptr );
}

bool FDX12GeometryRenderer::DrawTexturedGeometry( const FGeometryVertex* Vertices, int32 NumVertices, const FGeometryDrawState& State, const FTextureDrawView& Texture )
{
	return DrawInternal( Vertices, NumVertices, State, &Texture );
}

bool FDX12GeometryRenderer::DrawInternal( const FGeometryVertex* Vertices, int32 NumVertices, const FGeometryDrawState& State, const FTextureDrawView* Texture )
{
	const int32 Group = State.Topology == EGeometryTopology::Points ? 1 : State.Topology == EGeometryTopology::Lines ? 2 : 3; // リストの最小単位。
	if ( !Commands || !Vertices || NumVertices <= 0 || NumVertices > 65536 || NumVertices % Group != 0
		|| State.Topology > EGeometryTopology::Triangles || State.Compare > EGeometryCompare::Always || State.Culling > EGeometryCulling::FrontFaces || State.AddressU > EGeometryTextureAddress::Border || State.AddressV > EGeometryTextureAddress::Border
		|| ( Texture && ( !SrvHeap || Texture->Binding == 0 || Texture->Width <= 0 || Texture->Height <= 0 ) ) )
	{
		return false;
	}
	if ( Texture )
	{
		const UINT64 Base = SrvHeap->GetGPUDescriptorHandleForHeapStart().ptr; // この実装が所有するheapの先頭。
		const UINT Stride = Device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ); // 画像記述子の幅。
		if ( Texture->Binding < Base || ( Texture->Binding - Base ) % Stride != 0 || ( Texture->Binding - Base ) / Stride >= SrvHeap->GetDesc().NumDescriptors )
		{
			return false;
		}
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
	ID3D12PipelineState* Pipeline = GetPipeline( State, Texture != nullptr ); // 今回だけのGPU状態。
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
	void* Mapped = nullptr; // CPUが書く先。
	const D3D12_RANGE NoRead = { 0, 0 }; // CPUは読み戻さない。
	if ( FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &Upload ) ) )
		|| FAILED( Upload->Map( 0, &NoRead, &Mapped ) ) )
	{
		return false;
	}
	FMemory::Memcpy( Mapped, Vertices, static_cast<usize>( Desc.Width ) );
	Upload->Unmap( 0, nullptr );
	FPendingDraw Pending; // コールバックへ値で複写する。
	Pending.Owner = this;
	Pending.TextureBinding = Texture ? Texture->Binding : 0;
	Pending.SamplerIndex = static_cast<UINT>( State.AddressU ) | ( static_cast<UINT>( State.AddressV ) << 2 ) | ( State.bLinearFilter ? 16 : 0 );
	Pending.Pipeline = Pipeline;
	Pending.Vertices = { Upload->GetGPUVirtualAddress(), static_cast<UINT>( Desc.Width ), sizeof( FGeometryVertex ) };
	Pending.NumVertices = static_cast<UINT>( NumVertices );
	Pending.Topology = State.Topology == EGeometryTopology::Points ? D3D_PRIMITIVE_TOPOLOGY_POINTLIST
		: State.Topology == EGeometryTopology::Lines ? D3D_PRIMITIVE_TOPOLOGY_LINELIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	Uploads.Add( FUploadRecord { MoveTemp( Upload ) } );
	UploadBytes += Desc.Width;
	ImDrawList* List = ImGui::GetBackgroundDrawList( ImGui::GetMainViewport() ); // 2Dと3Dの記録順を保つ。
	List->AddCallback( DrawCallback, &Pending, sizeof( Pending ) );
	List->AddCallback( ImDrawCallback_ResetRenderState, nullptr );
	return true;
}

void FDX12GeometryRenderer::DrawCallback( const ImDrawList*, const ImDrawCmd* Command )
{
	const auto& Pending = *static_cast<const FPendingDraw*>( Command->UserCallbackData ); // 所有済みの描画。
	auto& Owner = *Pending.Owner; // フレーム中のGPU記録先。
	D3D12_RECT Clip = { static_cast<LONG>( NMath::Clamp( Command->ClipRect.x, 0.0f, static_cast<float32>( Owner.Width ) ) ),
		static_cast<LONG>( NMath::Clamp( Command->ClipRect.y, 0.0f, static_cast<float32>( Owner.Height ) ) ),
		static_cast<LONG>( NMath::Clamp( Command->ClipRect.z, 0.0f, static_cast<float32>( Owner.Width ) ) ),
		static_cast<LONG>( NMath::Clamp( Command->ClipRect.w, 0.0f, static_cast<float32>( Owner.Height ) ) ) }; // 現在のクリップを3Dにも適用する。
	if ( Clip.left >= Clip.right || Clip.top >= Clip.bottom )
	{
		return;
	}
	const D3D12_VIEWPORT Viewport = { 0, 0, static_cast<float>( Owner.Width ), static_cast<float>( Owner.Height ), 0, 1 }; // 論理解像度。
	const auto Dsv = Owner.DsvHeap->GetCPUDescriptorHandleForHeapStart(); // 所有する深度。
	Owner.Commands->OMSetRenderTargets( 1, &Owner.Rtv, FALSE, &Dsv );
	Owner.Commands->RSSetViewports( 1, &Viewport );
	Owner.Commands->RSSetScissorRects( 1, &Clip );
	Owner.Commands->SetGraphicsRootSignature( Owner.RootSignature.Get() );
	if ( Pending.TextureBinding != 0 )
	{
		ID3D12DescriptorHeap* Heaps[] = { Owner.SrvHeap, Owner.SamplerHeap.Get() }; // 画像は所有側のheap、補間は実行側のheap。
		Owner.Commands->SetDescriptorHeaps( 2, Heaps );
		Owner.Commands->SetGraphicsRootDescriptorTable( 0, D3D12_GPU_DESCRIPTOR_HANDLE { Pending.TextureBinding } );
		auto Sampler = Owner.SamplerHeap->GetGPUDescriptorHandleForHeapStart(); // この命令に固定した補間。
		Sampler.ptr += Pending.SamplerIndex * Owner.SamplerStride;
		Owner.Commands->SetGraphicsRootDescriptorTable( 1, Sampler );
	}
	Owner.Commands->SetPipelineState( Pending.Pipeline );
	Owner.Commands->IASetPrimitiveTopology( Pending.Topology );
	Owner.Commands->IASetVertexBuffers( 0, 1, &Pending.Vertices );
	Owner.Commands->DrawInstanced( Pending.NumVertices, 1, 0, 0 );
	Owner.Commands->OMSetRenderTargets( 1, &Owner.Rtv, FALSE, nullptr );
	if ( Pending.TextureBinding != 0 )
	{
		Owner.Commands->SetDescriptorHeaps( 1, &Owner.SrvHeap );
	}
}

void FDX12GeometryRenderer::ClearDepth()
{
	if ( Commands )
	{
		ImGui::GetBackgroundDrawList( ImGui::GetMainViewport() )->AddCallback( ClearDepthCallback, this );
	}
}

void FDX12GeometryRenderer::ClearDepthCallback( const ImDrawList*, const ImDrawCmd* Command )
{
	auto* Owner = static_cast<FDX12GeometryRenderer*>( Command->UserCallbackData ); // フレームを所有する実行先。
	Owner->Commands->ClearDepthStencilView( Owner->DsvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr );
}
