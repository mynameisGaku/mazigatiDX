// SPDX-License-Identifier: MIT
#include "DX12CaptureTarget.h"
#include <d3dcompiler.h>

FDX12CaptureTarget::~FDX12CaptureTarget()
{
	while ( !TryShutdown( 1000 ) )
	{
		Sleep( 1 );
	}
}

bool FDX12CaptureTarget::Initialize( ID3D12Device* InDevice, ID3D12CommandQueue* InQueue )
{
	if ( Device || bClosing || !Completion.Initialize( InDevice, InQueue ) )
	{
		return false;
	}
	Device = InDevice;
	Queue = InQueue;
	D3D12_DESCRIPTOR_HEAP_DESC Heap = {}; // 色と深度の記述子を一枚ずつ持つ。
	Heap.NumDescriptors = 1;
	Heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	if ( FAILED( Device->CreateDescriptorHeap( &Heap, IID_PPV_ARGS( &ColorHeap ) ) ) )
	{
		return false;
	}
	Heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	return SUCCEEDED( Device->CreateDescriptorHeap( &Heap, IID_PPV_ARGS( &DepthHeap ) ) ) && SUCCEEDED( Device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &Allocator ) ) ) && SUCCEEDED( Device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.Get(), nullptr, IID_PPV_ARGS( &Commands ) ) ) && SUCCEEDED( Commands->Close() );
}

bool FDX12CaptureTarget::PrepareImages( int32 InWidth, int32 InHeight, FColor32 Clear )
{
	if ( ColorTexture && Width == InWidth && Height == InHeight && InitialClear.r == Clear.r && InitialClear.g == Clear.g && InitialClear.b == Clear.b && InitialClear.a == Clear.a )
	{
		return true;
	}
	D3D12_HEAP_PROPERTIES Heap = {}; // GPUが直接描き込む領域。
	Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC Desc = {}; // 一枚の色画像。
	Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	Desc.Width = InWidth;
	Desc.Height = InHeight;
	Desc.DepthOrArraySize = 1;
	Desc.MipLevels = 1;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	D3D12_CLEAR_VALUE ClearValue = {}; // 実際の初期消去色と一致させる。
	ClearValue.Format = Desc.Format;
	ClearValue.Color[0] = Clear.r / 255.0f; ClearValue.Color[1] = Clear.g / 255.0f; ClearValue.Color[2] = Clear.b / 255.0f; ClearValue.Color[3] = Clear.a / 255.0f;
	Microsoft::WRL::ComPtr<ID3D12Resource> NewColor, NewDepth; // 完成するまで現在の描画先を保つ。
	if ( FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &ClearValue, IID_PPV_ARGS( &NewColor ) ) ) )
	{
		return false;
	}
	Desc.Format = DXGI_FORMAT_D32_FLOAT;
	Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	ClearValue = {};
	ClearValue.Format = Desc.Format;
	ClearValue.DepthStencil.Depth = 1;
	if ( FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &ClearValue, IID_PPV_ARGS( &NewDepth ) ) ) || !Readback.InitializeAfterGPUIdle( Device.Get(), NewColor.Get() ) )
	{
		return false;
	}
	ColorTexture = MoveTemp( NewColor );
	DepthTexture = MoveTemp( NewDepth );
	InitialClear = Clear;
	Width = InWidth;
	Height = InHeight;
	Device->CreateRenderTargetView( ColorTexture.Get(), nullptr, ColorHeap->GetCPUDescriptorHandleForHeapStart() );
	Device->CreateDepthStencilView( DepthTexture.Get(), nullptr, DepthHeap->GetCPUDescriptorHandleForHeapStart() );
	return true;
}

bool FDX12CaptureTarget::Begin( int32 InWidth, int32 InHeight, FColor32 Clear )
{
	if ( bRecording || bClosing || !bHealthy || !Device || !Commands || InWidth <= 0 || InHeight <= 0 || InWidth > 16384 || InHeight > 16384 || static_cast<uint64>( InWidth ) * InHeight * 4 > 256ull * 1024 * 1024 || !Completion.Wait() || !PrepareImages( InWidth, InHeight, Clear ) || FAILED( Allocator->Reset() ) || FAILED( Commands->Reset( Allocator.Get(), nullptr ) ) )
	{
		return false;
	}
	bRecording = true;
	const auto View = GetColorView(); // 色の描画先だけを有効にする。
	Commands->OMSetRenderTargets( 1, &View, FALSE, nullptr );
	Commands->ClearDepthStencilView( DepthHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr );
	return ClearColor( Clear );
}

bool FDX12CaptureTarget::PrepareClearPipeline()
{
	if ( ClearPipeline )
	{
		return true;
	}
	const char Shader[] = "cbuffer C:register(b0){float4 color;} float4 VS(uint id:SV_VertexID):SV_POSITION{ return float4(id==2?3:-1,id==1?3:-1,0,1); } float4 PS():SV_TARGET{return color;}"; // 全画面を一枚の三角形で塗る。
	Microsoft::WRL::ComPtr<ID3DBlob> VS, PS, Root; // 消去だけに使う小さなGPUプログラム。
	D3D12_ROOT_PARAMETER Parameter = {}; // 四成分の消去色。
	Parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	Parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	Parameter.Constants.Num32BitValues = 4;
	D3D12_ROOT_SIGNATURE_DESC Signature = {}; // 画像やUIの記述子を使わない。
	Signature.NumParameters = 1;
	Signature.pParameters = &Parameter;
	if ( FAILED( D3DCompile( Shader, sizeof( Shader ) - 1, "CaptureClear", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &VS, nullptr ) ) || FAILED( D3DCompile( Shader, sizeof( Shader ) - 1, "CaptureClear", nullptr, nullptr, "PS", "ps_5_0", 0, 0, &PS, nullptr ) ) || FAILED( D3D12SerializeRootSignature( &Signature, D3D_ROOT_SIGNATURE_VERSION_1, &Root, nullptr ) ) || FAILED( Device->CreateRootSignature( 0, Root->GetBufferPointer(), Root->GetBufferSize(), IID_PPV_ARGS( &ClearRoot ) ) ) )
	{
		return false;
	}
	D3D12_GRAPHICS_PIPELINE_STATE_DESC State = {}; // 合成せず、RGBAをそのまま書く。
	State.pRootSignature = ClearRoot.Get();
	State.VS = { VS->GetBufferPointer(), VS->GetBufferSize() };
	State.PS = { PS->GetBufferPointer(), PS->GetBufferSize() };
	State.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	State.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	State.RasterizerState.DepthClipEnable = TRUE;
	State.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	State.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	State.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
	State.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	State.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	State.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	State.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	State.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
	State.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	State.SampleMask = UINT_MAX;
	State.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	State.NumRenderTargets = 1;
	State.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	State.SampleDesc.Count = 1;
	return SUCCEEDED( Device->CreateGraphicsPipelineState( &State, IID_PPV_ARGS( &ClearPipeline ) ) );
}

bool FDX12CaptureTarget::ClearColor( FColor32 Color )
{
	if ( !bRecording || !bHealthy )
	{
		return false;
	}
	const float Values[4] = { Color.r / 255.0f, Color.g / 255.0f, Color.b / 255.0f, Color.a / 255.0f }; // GPUへ渡すRGBA。
	if ( InitialClear.r == Color.r && InitialClear.g == Color.g && InitialClear.b == Color.b && InitialClear.a == Color.a )
	{
		Commands->ClearRenderTargetView( GetColorView(), Values, 0, nullptr );
		return true;
	}
	if ( !PrepareClearPipeline() )
	{
		bHealthy = false;
		return false;
	}
	const D3D12_VIEWPORT Viewport = { 0, 0, static_cast<float>( Width ), static_cast<float>( Height ), 0, 1 }; // 色全体を塗る。
	const D3D12_RECT Clip = { 0, 0, Width, Height }; // 既存のクリップへ影響されない消去範囲。
	const auto View = GetColorView(); // 深度は変更しない。
	Commands->OMSetRenderTargets( 1, &View, FALSE, nullptr );
	Commands->RSSetViewports( 1, &Viewport );
	Commands->RSSetScissorRects( 1, &Clip );
	Commands->SetGraphicsRootSignature( ClearRoot.Get() );
	Commands->SetGraphicsRoot32BitConstants( 0, 4, Values, 0 );
	Commands->SetPipelineState( ClearPipeline.Get() );
	Commands->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	Commands->DrawInstanced( 3, 1, 0, 0 );
	return true;
}

bool FDX12CaptureTarget::SubmitAndRead( TArray<FColor32>& Out, uint32 TimeoutMs )
{
	if ( !bRecording || bClosing || !bHealthy || TimeoutMs > 60000 )
	{
		return false;
	}
	bRecording = false;
	if ( !Readback.RecordCopy( Commands.Get(), ColorTexture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET ) || FAILED( Commands->Close() ) )
	{
		bHealthy = false;
		return false;
	}
	ID3D12CommandList* Lists[] = { Commands.Get() }; // 通常描画と同じキューへ順番に提出する。
	Queue->ExecuteCommandLists( 1, Lists );
	if ( !Completion.Wait( TimeoutMs ) )
	{
		bHealthy = false;
		return false;
	}
	return Readback.ReadAfterGPUIdle( Out, false );
}

bool FDX12CaptureTarget::TryShutdown( uint32 TimeoutMs )
{
	if ( TimeoutMs > 60000 )
	{
		return false;
	}
	bClosing = true;
	if ( Device && Completion.IsInitialized() && !Completion.Wait( TimeoutMs ) && SUCCEEDED( Device->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	Commands.Reset(); Allocator.Reset(); ColorTexture.Reset(); DepthTexture.Reset(); ColorHeap.Reset(); DepthHeap.Reset(); ClearPipeline.Reset(); ClearRoot.Reset();
	Readback.ResetAfterGPUIdle();
	Completion.ResetAfterGPUIdle();
	Queue.Reset(); Device.Reset();
	bRecording = false;
	return true;
}

bool FDX12CaptureTarget::IsRecording() const { return bRecording && !bClosing && bHealthy; }
ID3D12GraphicsCommandList* FDX12CaptureTarget::GetCommands() const { return IsRecording() ? Commands.Get() : nullptr; }
ID3D12Device* FDX12CaptureTarget::GetDevice() const { return Device.Get(); }
D3D12_CPU_DESCRIPTOR_HANDLE FDX12CaptureTarget::GetColorView() const { return ColorHeap ? ColorHeap->GetCPUDescriptorHandleForHeapStart() : D3D12_CPU_DESCRIPTOR_HANDLE(); }
ID3D12Resource* FDX12CaptureTarget::GetDepthTexture() const { return DepthTexture.Get(); }
ID3D12DescriptorHeap* FDX12CaptureTarget::GetDepthHeap() const { return DepthHeap.Get(); }
int32 FDX12CaptureTarget::GetWidth() const { return Width; }
int32 FDX12CaptureTarget::GetHeight() const { return Height; }
