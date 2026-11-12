// SPDX-License-Identifier: MIT
#include "DX12RenderTargetPass.h"
#include "Library/Platform/DX12/Pipeline/Graphics/DX12GraphicsPipelineStore.h"
#include "Library/Platform/DX12/Resource/Buffer/DX12BufferStore.h"
#include "Library/Platform/DX12/Resource/Timestamp/DX12TimestampStore.h"
#include "Library/Platform/DX12/Resource/Upload/DX12UploadArena.h"
#include "Library/Core/Types/Numeric.h"
#include <d3dcompiler.h>

CDX12RenderTargetPass::~CDX12RenderTargetPass()
{
	while ( !TryShutdown( 1000 ) )
	{
		Sleep( 1 );
	}
}

bool CDX12RenderTargetPass::Initialize( ID3D12Device *InDevice, ID3D12CommandQueue *InQueue, CDX12TextureStore &InTextures, IRenderTextRasterizer &InText, ID3D12DescriptorHeap *InImageHeap, CDX12BufferStore *InBuffers, CDX12GraphicsPipelineStore *InGraphicsPipelines, CDX12TimestampStore *InTimestamps, CDX12UploadArena *InUploadArena )
{
	if ( Device || bClosing || !InDevice || !InQueue || InQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT || !InTextures.UsesDeviceAndQueue( InDevice, InQueue ) || ( InBuffers && !InBuffers->UsesDeviceAndQueue( InDevice, InQueue ) ) || ( InGraphicsPipelines && !InGraphicsPipelines->UsesDevice( InDevice ) ) || ( InTimestamps && !InTimestamps->UsesDeviceAndQueue( InDevice, InQueue ) ) || ( InUploadArena && !InUploadArena->UsesDeviceAndQueue( InDevice, InQueue ) ) )
	{
		return false;
	}
	Microsoft::WRL::ComPtr<ID3D12Device> QueueDevice, HeapDevice; // 異なるGPUのキューやヒープを混在させない。
	if ( FAILED( InQueue->GetDevice( IID_PPV_ARGS( &QueueDevice ) ) ) || QueueDevice.Get() != InDevice || FAILED( InDevice->GetDeviceRemovedReason() ) )
	{
		return false;
	}
	if ( InImageHeap && ( FAILED( InImageHeap->GetDevice( IID_PPV_ARGS( &HeapDevice ) ) ) || HeapDevice.Get() != InDevice || InImageHeap->GetDesc().Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || !( InImageHeap->GetDesc().Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE ) ) )
	{
		return false;
	}
	if ( !Completion.Initialize( InDevice, InQueue ) || FAILED( InDevice->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &Fence ) ) ) )
	{
		Completion.ResetAfterGPUIdle();
		Fence.Reset();
		return false;
	}
	Device = InDevice;
	Queue = InQueue;
	Textures = &InTextures;
	Text = &InText;
	ImageHeap = InImageHeap;
	Buffers = InBuffers;
	GraphicsPipelines = InGraphicsPipelines;
	Timestamps = InTimestamps;
	UploadArena = InUploadArena;
	bHealthy = true;
	return true;
}

bool CDX12RenderTargetPass::CollectCompleted()
{
	if ( !bHealthy || !Device || !Fence )
	{
		return false;
	}
	const uint64 Completed = Fence->GetCompletedValue(); // 最大値はデバイス喪失を表す。
	if ( Completed == TNumericLimits<uint64>::Max() || FAILED( Device->GetDeviceRemovedReason() ) )
	{
		bHealthy = false;
		return false;
	}
	Pending.RemoveAll( [Completed]( const FSubmissionRecord& Record ) { return Record.Value <= Completed; } );
	return true;
}

void CDX12RenderTargetPass::CollectAfterGPUIdle()
{
	Pending.Reset();
	bHealthy = Device && SUCCEEDED( Device->GetDeviceRemovedReason() );
}

void CDX12RenderTargetPass::ResetAfterGPUIdle()
{
	if ( bActive && Textures )
	{
		Textures->EndRenderTargetPass();
	}
	if ( Timestamps && ActiveTimestampBatchId != 0 )
	{
		Timestamps->AbortBatch( ActiveTimestampBatchId );
		ActiveTimestampBatchId = 0;
	}
	delete Renderer;
	Renderer = nullptr;
	Pending.Reset();
	CurrentCommands.Reset();
	CurrentAllocator.Reset();
	ActiveColors.Reset();
	ActiveDepth = FDX12TextureBinding();
	FallbackDepth.Reset();
	FallbackDepthHeap.Reset();
	DepthClearPipeline.Reset();
	DepthClearRoot.Reset();
	ColorClearPipeline.Reset();
	ColorClearRoot.Reset();
	Fence.Reset();
	Completion.ResetAfterGPUIdle();
	Queue.Reset();
	Device.Reset();
	Textures = nullptr;
	Text = nullptr;
	ImageHeap = nullptr;
	Buffers = nullptr;
	GraphicsPipelines = nullptr;
	Timestamps = nullptr;
	UploadArena = nullptr;
	ActiveTimestampBatchId = 0;
	NextFenceValue = 0;
	FallbackClearDepth = 1.0f;
	FallbackWidth = FallbackHeight = 0;
	bFallbackInitialized = false;
	bUsingExternalDepth = false;
	bRecordClearFailed = false;
	bActive = false;
	bHealthy = false;
	bClosing = false;
}

bool CDX12RenderTargetPass::TryShutdown( uint32 TimeoutMs )
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
	ResetAfterGPUIdle();
	return true;
}

bool CDX12RenderTargetPass::PrepareFallbackDepth( int32 Width, int32 Height, float32 ClearDepth )
{
	if ( FallbackDepth && FallbackDepthHeap && FallbackWidth == Width && FallbackHeight == Height )
	{
		return true;
	}
	D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {}; // 内部深度用のCPU DSVヒープ。
	HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	HeapDesc.NumDescriptors = 1;
	D3D12_HEAP_PROPERTIES Heap = {}; // GPU専用の深度領域。
	Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC Desc = {}; // D32_FLOATの内部深度。
	Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	Desc.Width = Width;
	Desc.Height = Height;
	Desc.DepthOrArraySize = Desc.MipLevels = Desc.SampleDesc.Count = 1;
	Desc.Format = DXGI_FORMAT_D32_FLOAT;
	Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	D3D12_CLEAR_VALUE ClearValue = {}; // 要求された初期深度と一致させる。
	ClearValue.Format = Desc.Format;
	ClearValue.DepthStencil.Depth = ClearDepth;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> NewHeap; // 失敗時に既存の深度を壊さない。
	Microsoft::WRL::ComPtr<ID3D12Resource> NewDepth; // 新しい内部深度。
	if ( FAILED( Device->CreateDescriptorHeap( &HeapDesc, IID_PPV_ARGS( &NewHeap ) ) ) || FAILED( Device->CreateCommittedResource( &Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &ClearValue, IID_PPV_ARGS( &NewDepth ) ) ) )
	{
		return false;
	}
	Device->CreateDepthStencilView( NewDepth.Get(), nullptr, NewHeap->GetCPUDescriptorHandleForHeapStart() );
	FallbackDepth = MoveTemp( NewDepth );
	FallbackDepthHeap = MoveTemp( NewHeap );
	FallbackWidth = Width;
	FallbackHeight = Height;
	FallbackClearDepth = ClearDepth;
	bFallbackInitialized = false;
	return true;
}

bool CDX12RenderTargetPass::PrepareDepthClearPipeline()
{
	if ( DepthClearPipeline )
	{
		return true;
	}
	const char Shader[] = "cbuffer C:register(b0){float depthValue;} float4 VS(uint id:SV_VertexID):SV_POSITION{ return float4(id==2?3:-1,id==1?3:-1,depthValue,1); }"; // 色描画先を変更せず深度だけを塗る。
	Microsoft::WRL::ComPtr<ID3DBlob> VS, Root; // 深度消去専用の小さなプログラム。
	D3D12_ROOT_PARAMETER Parameter = {}; // 1個の32bit浮動小数点深度。
	Parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	Parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	Parameter.Constants.Num32BitValues = 1;
	D3D12_ROOT_SIGNATURE_DESC Signature = {}; // 記述子ヒープを使わない最小構成。
	Signature.NumParameters = 1;
	Signature.pParameters = &Parameter;
	if ( FAILED( D3DCompile( Shader, sizeof( Shader ) - 1, "DepthClear", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &VS, nullptr ) ) || FAILED( D3D12SerializeRootSignature( &Signature, D3D_ROOT_SIGNATURE_VERSION_1, &Root, nullptr ) ) || FAILED( Device->CreateRootSignature( 0, Root->GetBufferPointer(), Root->GetBufferSize(), IID_PPV_ARGS( &DepthClearRoot ) ) ) )
	{
		return false;
	}
	D3D12_GRAPHICS_PIPELINE_STATE_DESC State = {}; // 深度だけを上書きするPSO。
	State.pRootSignature = DepthClearRoot.Get();
	State.VS = { VS->GetBufferPointer(), VS->GetBufferSize() };
	State.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	State.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	State.RasterizerState.DepthClipEnable = TRUE;
	State.DepthStencilState.DepthEnable = TRUE;
	State.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	State.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	State.SampleMask = UINT_MAX;
	State.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	State.NumRenderTargets = 0;
	State.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	State.SampleDesc.Count = 1;
	return SUCCEEDED( Device->CreateGraphicsPipelineState( &State, IID_PPV_ARGS( &DepthClearPipeline ) ) );
}

bool CDX12RenderTargetPass::ClearDepthWithShader( int32 Width, int32 Height, D3D12_CPU_DESCRIPTOR_HANDLE Dsv, float32 ClearDepth )
{
	if ( !CurrentCommands || !PrepareDepthClearPipeline() )
	{
		return false;
	}
	const D3D12_VIEWPORT Viewport = { 0, 0, static_cast<float>( Width ), static_cast<float>( Height ), 0, 1 }; // 深度全体を覆う。
	const D3D12_RECT Scissor = { 0, 0, Width, Height }; // クリップに依存しない全領域。
	CurrentCommands->OMSetRenderTargets( 0, nullptr, FALSE, &Dsv );
	CurrentCommands->RSSetViewports( 1, &Viewport );
	CurrentCommands->RSSetScissorRects( 1, &Scissor );
	CurrentCommands->SetGraphicsRootSignature( DepthClearRoot.Get() );
	CurrentCommands->SetGraphicsRoot32BitConstants( 0, 1, &ClearDepth, 0 );
	CurrentCommands->SetPipelineState( DepthClearPipeline.Get() );
	CurrentCommands->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	CurrentCommands->DrawInstanced( 3, 1, 0, 0 );
	return true;
}

bool CDX12RenderTargetPass::PrepareColorClearPipeline()
{
	if ( ColorClearPipeline )
	{
		return true;
	}
	const char Shader[] = "cbuffer C:register(b0){float4 clearColor;} float4 VS(uint id:SV_VertexID):SV_POSITION{return float4(id==2?3:-1,id==1?3:-1,0,1);} float4 PS():SV_TARGET{return clearColor;}"; // 最適化消去色と異なっても警告なしで全画面を塗る。
	Microsoft::WRL::ComPtr<ID3DBlob> VS, PS, Root; // 任意色クリア専用の小さなプログラム。
	D3D12_ROOT_PARAMETER Parameter = {}; // RGBAの4つの32bit定数。
	Parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	Parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	Parameter.Constants.Num32BitValues = 4;
	D3D12_ROOT_SIGNATURE_DESC Signature = {}; // 記述子ヒープを使わない最小構成。
	Signature.NumParameters = 1;
	Signature.pParameters = &Parameter;
	if ( FAILED( D3DCompile( Shader, sizeof( Shader ) - 1, "ColorClear", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &VS, nullptr ) ) || FAILED( D3DCompile( Shader, sizeof( Shader ) - 1, "ColorClear", nullptr, nullptr, "PS", "ps_5_0", 0, 0, &PS, nullptr ) ) || FAILED( D3D12SerializeRootSignature( &Signature, D3D_ROOT_SIGNATURE_VERSION_1, &Root, nullptr ) ) || FAILED( Device->CreateRootSignature( 0, Root->GetBufferPointer(), Root->GetBufferSize(), IID_PPV_ARGS( &ColorClearRoot ) ) ) )
	{
		return false;
	}
	D3D12_GRAPHICS_PIPELINE_STATE_DESC State = {}; // 深度へ触れず1枚のRGBA8だけを上書きするPSO。
	State.pRootSignature = ColorClearRoot.Get();
	State.VS = { VS->GetBufferPointer(), VS->GetBufferSize() };
	State.PS = { PS->GetBufferPointer(), PS->GetBufferSize() };
	State.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	State.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	State.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	State.RasterizerState.DepthClipEnable = TRUE;
	State.DepthStencilState.DepthEnable = FALSE;
	State.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	State.SampleMask = UINT_MAX;
	State.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	State.NumRenderTargets = 1;
	State.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	State.DSVFormat = DXGI_FORMAT_UNKNOWN;
	State.SampleDesc.Count = 1;
	return SUCCEEDED( Device->CreateGraphicsPipelineState( &State, IID_PPV_ARGS( &ColorClearPipeline ) ) );
}

bool CDX12RenderTargetPass::ClearColorWithShader( int32 Width, int32 Height, D3D12_CPU_DESCRIPTOR_HANDLE Rtv, FColor32 Color )
{
	if ( !CurrentCommands || !PrepareColorClearPipeline() )
	{
		return false;
	}
	const float Values[4] = { Color.r / 255.0f, Color.g / 255.0f, Color.b / 255.0f, Color.a / 255.0f };
	const D3D12_VIEWPORT Viewport = { 0, 0, static_cast<float>( Width ), static_cast<float>( Height ), 0, 1 };
	const D3D12_RECT Scissor = { 0, 0, Width, Height };
	CurrentCommands->OMSetRenderTargets( 1, &Rtv, FALSE, nullptr );
	CurrentCommands->RSSetViewports( 1, &Viewport );
	CurrentCommands->RSSetScissorRects( 1, &Scissor );
	CurrentCommands->SetGraphicsRootSignature( ColorClearRoot.Get() );
	CurrentCommands->SetGraphicsRoot32BitConstants( 0, 4, Values, 0 );
	CurrentCommands->SetPipelineState( ColorClearPipeline.Get() );
	CurrentCommands->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	CurrentCommands->DrawInstanced( 3, 1, 0, 0 );
	return true;
}

bool CDX12RenderTargetPass::ClearColorAttachment( const FDX12TextureBinding &Target, FColor32 Color )
{
	if ( !CurrentCommands || !Target.TargetHeap )
	{
		return false;
	}
	const D3D12_CPU_DESCRIPTOR_HANDLE Rtv = Target.TargetHeap->GetCPUDescriptorHandleForHeapStart();
	if ( Color.r == Target.InitialClearColor.r && Color.g == Target.InitialClearColor.g && Color.b == Target.InitialClearColor.b && Color.a == Target.InitialClearColor.a )
	{
		const float Values[4] = { Color.r / 255.0f, Color.g / 255.0f, Color.b / 255.0f, Color.a / 255.0f };
		CurrentCommands->ClearRenderTargetView( Rtv, Values, 0, nullptr );
		return true;
	}
	return ClearColorWithShader( Target.Width, Target.Height, Rtv, Color );
}

bool CDX12RenderTargetPass::Begin( const FRenderTargetPassDesc &Desc )
{
	if ( bActive || bClosing || !bHealthy || !Device || !Textures || !Text || !CollectCompleted() || Pending.Num() >= MaxPending || NextFenceValue >= TNumericLimits<uint64>::Max() - 1 || !NMath::IsFinite( Desc.Scale ) || Desc.Scale <= 0.0f || Desc.Scale > 16384.0f || ( Desc.bClearDepth && ( !NMath::IsFinite( Desc.ClearDepth ) || Desc.ClearDepth < 0.0f || Desc.ClearDepth > 1.0f ) ) )
	{
		return false;
	}
	TArray<FRenderTargetColorAttachment> RequestedColors; // 単一指定またはMRT指定の描画先群。
	if ( !Desc.ColorTargets.IsEmpty() )
	{
		if ( Desc.ColorTarget.IsValid() || !Desc.ColorTargets.GetData() || Desc.ColorTargets.Num() <= 0 || Desc.ColorTargets.Num() > static_cast<int32>( FRenderTargetPassDesc::MaxColorTargets ) )
		{
			return false;
		}
		for ( const FRenderTargetColorAttachment &Item : Desc.ColorTargets )
		{
			RequestedColors.Add( Item );
		}
	}
	else
	{
		if ( !Desc.ColorTarget.IsValid() )
		{
			return false;
		}
		RequestedColors.Add( FRenderTargetColorAttachment{ Desc.ColorTarget, Desc.ClearColor, Desc.bClearColor } );
	}
	TArray<FTextureResourceHandle> Handles; // ストア検証へ渡すハンドル配列。
	Handles.SetNum( RequestedColors.Num() );
	for ( int32 i = 0; i < RequestedColors.Num(); ++i )
	{
		Handles[i] = RequestedColors[i].Target;
	}
	TArray<FDX12TextureBinding> ColorBindings; // 検証済みの色描画先群。
	FDX12TextureBinding DepthBinding; // 検証済みの任意深度。
	if ( !Textures->BeginRenderTargetPass( TArrayView<const FTextureResourceHandle>( Handles.GetData(), Handles.Num() ), Desc.DepthTarget, ColorBindings, DepthBinding ) )
	{
		return false;
	}
	const int32 PassWidth = ColorBindings[0].Width;
	const int32 PassHeight = ColorBindings[0].Height;
	const bool bExternalDepth = Desc.DepthTarget.IsValid(); // 呼び出し側の読み取れる深度を使うか。
	if ( !bExternalDepth )
	{
		const float32 DesiredDepth = Desc.bClearDepth ? Desc.ClearDepth : 1.0f; // 内部深度の初期値。
		if ( !PrepareFallbackDepth( PassWidth, PassHeight, DesiredDepth ) )
		{
			Textures->EndRenderTargetPass();
			return false;
		}
		DepthBinding.Resource = FallbackDepth;
		DepthBinding.TargetHeap = FallbackDepthHeap;
		DepthBinding.Width = PassWidth;
		DepthBinding.Height = PassHeight;
		DepthBinding.InitialClearDepth = FallbackClearDepth;
	}
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> Allocator; // このパス専用の記録領域。
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> Commands; // このパス専用の命令列。
	if ( FAILED( Device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &Allocator ) ) ) || FAILED( Device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.Get(), nullptr, IID_PPV_ARGS( &Commands ) ) ) )
	{
		Textures->EndRenderTargetPass();
		return false;
	}
	if ( !Renderer )
	{
		Renderer = new CDX12GameRenderer();
		if ( !Renderer->Initialize( Device.Get(), PassWidth, PassHeight, *Textures, *Text, DepthBinding.Resource.Get(), DepthBinding.TargetHeap.Get(), Buffers, Queue.Get(), Textures, GraphicsPipelines, UploadArena ) )
		{
			delete Renderer;
			Renderer = nullptr;
			Textures->EndRenderTargetPass();
			bHealthy = false;
			return false;
		}
	}
	else if ( !Renderer->BindTarget( PassWidth, PassHeight, DepthBinding.Resource.Get(), DepthBinding.TargetHeap.Get() ) )
	{
		Textures->EndRenderTargetPass();
		bHealthy = false;
		return false;
	}
	TArray<D3D12_RESOURCE_BARRIER> Barriers; // 定常のSRV状態から描画書き込み状態へ遷移する。
	Barriers.SetNum( ColorBindings.Num() + ( bExternalDepth ? 1 : 0 ) );
	for ( int32 i = 0; i < ColorBindings.Num(); ++i )
	{
		Barriers[i] = {};
		Barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		Barriers[i].Transition.pResource = ColorBindings[i].Resource.Get();
		Barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		Barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		Barriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	}
	if ( bExternalDepth )
	{
		D3D12_RESOURCE_BARRIER &DepthBarrier = Barriers[ColorBindings.Num()];
		DepthBarrier = {};
		DepthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		DepthBarrier.Transition.pResource = DepthBinding.Resource.Get();
		DepthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		DepthBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		DepthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	}
	Commands->ResourceBarrier( static_cast<UINT>( Barriers.Num() ), Barriers.GetData() );
	CurrentAllocator = MoveTemp( Allocator );
	CurrentCommands = MoveTemp( Commands );
	TArray<D3D12_CPU_DESCRIPTOR_HANDLE> ColorRtvs;
	TArray<uint64> ForbiddenBindings;
	ColorRtvs.SetNum( ColorBindings.Num() );
	for ( int32 i = 0; i < ColorBindings.Num(); ++i )
	{
		ColorRtvs[i] = ColorBindings[i].TargetHeap->GetCPUDescriptorHandleForHeapStart();
		if ( ColorBindings[i].SrvGpuDescriptor != 0 )
		{
			ForbiddenBindings.Add( ColorBindings[i].SrvGpuDescriptor );
		}
	}
	if ( bExternalDepth && DepthBinding.SrvGpuDescriptor != 0 )
	{
		ForbiddenBindings.Add( DepthBinding.SrvGpuDescriptor );
	}
	const D3D12_CPU_DESCRIPTOR_HANDLE DepthDsv = DepthBinding.TargetHeap->GetCPUDescriptorHandleForHeapStart(); // 深度描画先のDSV。
	const bool bClearDepth = Desc.bClearDepth || ( !bExternalDepth && !bFallbackInitialized ); // 新規の内部深度はLoad要求でも一度初期化する。
	const float32 ClearDepthValue = Desc.bClearDepth ? Desc.ClearDepth : 1.0f; // 未提出で破棄した消去値に初回Loadを依存させない。
	const bool bFastDepthClear = bClearDepth && ClearDepthValue == DepthBinding.InitialClearDepth; // 作成時の最適化深度と一致する場合。
	if ( bClearDepth && !bFastDepthClear && !ClearDepthWithShader( PassWidth, PassHeight, DepthDsv, ClearDepthValue ) )
	{
		CurrentCommands.Reset();
		CurrentAllocator.Reset();
		Textures->EndRenderTargetPass();
		bHealthy = false;
		return false;
	}
	if ( !Renderer->BeginFrame( CurrentCommands.Get(), TArrayView<const D3D12_CPU_DESCRIPTOR_HANDLE>( ColorRtvs.GetData(), ColorRtvs.Num() ), ImageHeap, Desc.Scale, bFastDepthClear, ClearDepthValue, TArrayView<const uint64>( ForbiddenBindings.GetData(), ForbiddenBindings.Num() ) ) )
	{
		CurrentCommands.Reset();
		CurrentAllocator.Reset();
		Textures->EndRenderTargetPass();
		bHealthy = false;
		return false;
	}
	for ( int32 i = 0; i < ColorBindings.Num(); ++i )
	{
		if ( RequestedColors[i].bClear && !ClearColorAttachment( ColorBindings[i], RequestedColors[i].ClearColor ) )
		{
			Renderer->EndFrame();
			Renderer->AbortUploadTicket();
			CurrentCommands.Reset();
			CurrentAllocator.Reset();
			Textures->EndRenderTargetPass();
			bHealthy = false;
			return false;
		}
	}
	CurrentCommands->OMSetRenderTargets( static_cast<UINT>( ColorRtvs.Num() ), ColorRtvs.GetData(), FALSE, nullptr );
	ActiveColors = MoveTemp( ColorBindings );
	ActiveDepth = MoveTemp( DepthBinding );
	bUsingExternalDepth = bExternalDepth;
	bRecordClearFailed = false;
	ActiveTimestampBatchId = Timestamps ? Timestamps->BeginBatch() : 0;
	if ( Timestamps && ActiveTimestampBatchId == 0 )
	{
		Renderer->EndFrame();
		Renderer->AbortUploadTicket();
		CurrentCommands.Reset();
		CurrentAllocator.Reset();
		ActiveColors.Reset();
		ActiveDepth = FDX12TextureBinding();
		bUsingExternalDepth = false;
		Textures->EndRenderTargetPass();
		bHealthy = false;
		return false;
	}
	bActive = true;
	return true;
}

bool CDX12RenderTargetPass::End()
{
	if ( !bActive || bClosing || !bHealthy || !Renderer || !Textures || !CurrentCommands )
	{
		return false;
	}
	const bool bHasUnclosedTimestamps = Timestamps && ActiveTimestampBatchId != 0 && Timestamps->HasActiveRecording( ActiveTimestampBatchId );
	const bool bRendered = Renderer->EndFrame() && !bRecordClearFailed && !bHasUnclosedTimestamps; // 文字失敗・任意クリア失敗・自己参照競合・未閉鎖タイムスタンプがあれば提出しない。
	const uint64 UploadTicket = Renderer->TakeUploadTicket(); // 提出成否に応じて完了または破棄するアップロード予約。
	Textures->EndRenderTargetPass();
	bActive = false;
	bRecordClearFailed = false;
	FSubmissionRecord Created; // 完了まで保持する提出レコード。
	Renderer->MoveFrameResources( Created.Buffers, Created.Uploads, Created.Textures, Created.Pipelines );
	if ( !bRendered )
	{
		if ( UploadArena && UploadTicket != 0 )
		{
			UploadArena->AbortTicket( UploadTicket );
		}
		if ( Timestamps && ActiveTimestampBatchId != 0 )
		{
			Timestamps->AbortBatch( ActiveTimestampBatchId );
			ActiveTimestampBatchId = 0;
		}
		CurrentCommands->Close();
		CurrentCommands.Reset();
		CurrentAllocator.Reset();
		ActiveColors.Reset();
		ActiveDepth = FDX12TextureBinding();
		bUsingExternalDepth = false;
		return false;
	}
	TArray<D3D12_RESOURCE_BARRIER> Barriers; // パス終了時に後続の描画・compute読み取り用のSRV状態へ戻す。
	Barriers.SetNum( ActiveColors.Num() + ( bUsingExternalDepth ? 1 : 0 ) );
	for ( int32 i = 0; i < ActiveColors.Num(); ++i )
	{
		Barriers[i] = {};
		Barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		Barriers[i].Transition.pResource = ActiveColors[i].Resource.Get();
		Barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		Barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		Barriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	}
	if ( bUsingExternalDepth )
	{
		D3D12_RESOURCE_BARRIER &DepthBarrier = Barriers[ActiveColors.Num()];
		DepthBarrier = {};
		DepthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		DepthBarrier.Transition.pResource = ActiveDepth.Resource.Get();
		DepthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		DepthBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		DepthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	}
	CurrentCommands->ResourceBarrier( static_cast<UINT>( Barriers.Num() ), Barriers.GetData() );
	if ( FAILED( CurrentCommands->Close() ) )
	{
		if ( UploadArena && UploadTicket != 0 )
		{
			UploadArena->AbortTicket( UploadTicket );
		}
		if ( Timestamps && ActiveTimestampBatchId != 0 )
		{
			Timestamps->AbortBatch( ActiveTimestampBatchId );
			ActiveTimestampBatchId = 0;
		}
		CurrentCommands.Reset();
		CurrentAllocator.Reset();
		ActiveColors.Reset();
		ActiveDepth = FDX12TextureBinding();
		bUsingExternalDepth = false;
		bHealthy = false;
		return false;
	}
	Created.Allocator = MoveTemp( CurrentAllocator );
	Created.Commands = MoveTemp( CurrentCommands );
	Created.Colors = MoveTemp( ActiveColors );
	Created.Depth = MoveTemp( ActiveDepth );
	Created.Value = ++NextFenceValue;
	bUsingExternalDepth = false;
	Pending.Add( MoveTemp( Created ) );
	FSubmissionRecord &Submitted = Pending.Last(); // Signal失敗時もGPU完了まで保持する。
	ID3D12CommandList *Lists[] = { Submitted.Commands.Get() }; // 後続のSubmitComputeより先に同じDIRECTキューへ提出する。
	Queue->ExecuteCommandLists( 1, Lists );
	if ( Submitted.Depth.Resource.Get() == FallbackDepth.Get() )
	{
		// 記録失敗で破棄したパスは初期化済みにせず、実際の提出位置で更新する。
		bFallbackInitialized = true;
	}
	const uint64 BatchToCommit = ActiveTimestampBatchId;
	ActiveTimestampBatchId = 0;
	const bool bUploadCommitted = !UploadArena || UploadTicket == 0 || UploadArena->CommitTicket( UploadTicket );
	if ( !bUploadCommitted || ( BatchToCommit != 0 && !Timestamps->CommitBatch( BatchToCommit ) ) || FAILED( Queue->Signal( Fence.Get(), Submitted.Value ) ) )
	{
		bHealthy = false;
		return false;
	}
	return true;
}

bool CDX12RenderTargetPass::IsActive() const
{
	return bActive;
}

IRenderCanvas2D *CDX12RenderTargetPass::GetCanvas2D()
{
	return bActive && Renderer ? Renderer->GetCanvas2D() : nullptr;
}

IRenderGeometry3D *CDX12RenderTargetPass::GetGeometry3D()
{
	return bActive && Renderer ? Renderer->GetGeometry3D() : nullptr;
}

void CDX12RenderTargetPass::RecordClear( FColor32 Color )
{
	if ( !bActive || !Renderer || ActiveColors.IsEmpty() )
	{
		return;
	}
	TArray<D3D12_CPU_DESCRIPTOR_HANDLE> ColorRtvs;
	ColorRtvs.SetNum( ActiveColors.Num() );
	for ( int32 i = 0; i < ActiveColors.Num(); ++i )
	{
		ColorRtvs[i] = ActiveColors[i].TargetHeap->GetCPUDescriptorHandleForHeapStart();
		if ( !ClearColorAttachment( ActiveColors[i], Color ) )
		{
			bRecordClearFailed = true;
		}
	}
	if ( CurrentCommands )
	{
		CurrentCommands->OMSetRenderTargets( static_cast<UINT>( ColorRtvs.Num() ), ColorRtvs.GetData(), FALSE, nullptr );
	}
}

bool CDX12RenderTargetPass::WaitForGPU()
{
	if ( !bHealthy || !Device || !Completion.IsInitialized() || !Completion.Wait() )
	{
		return false;
	}
	CollectAfterGPUIdle();
	return true;
}

bool CDX12RenderTargetPass::BeginRenderTargetPass( const FRenderTargetPassDesc &Desc )
{
	return Begin( Desc );
}

bool CDX12RenderTargetPass::EndRenderTargetPass()
{
	return End();
}

bool CDX12RenderTargetPass::BeginGpuTimestamp( FGpuTimestampHandle Query )
{
	return bActive && bHealthy && CurrentCommands && Timestamps && ActiveTimestampBatchId != 0 && Timestamps->BeginRecording( CurrentCommands.Get(), ActiveTimestampBatchId, Query );
}

bool CDX12RenderTargetPass::EndGpuTimestamp( FGpuTimestampHandle Query )
{
	return bActive && bHealthy && CurrentCommands && Timestamps && ActiveTimestampBatchId != 0 && Timestamps->EndRecording( CurrentCommands.Get(), ActiveTimestampBatchId, Query );
}
