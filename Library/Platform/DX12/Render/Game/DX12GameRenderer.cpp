// SPDX-License-Identifier: MIT
#include "DX12GameRenderer.h"
#include "Library/Core/Types/Numeric.h"

CDX12GameRenderer::~CDX12GameRenderer() { EndFrame(); }

bool CDX12GameRenderer::Initialize( ID3D12Device *Device, int32 Width, int32 Height, IRenderTextureResources &InTextures, IRenderTextRasterizer &InText, ID3D12Resource *Depth, ID3D12DescriptorHeap *DepthHeap, CDX12BufferStore *Buffers, ID3D12CommandQueue *Queue, CDX12TextureStore *TextureStore, CDX12GraphicsPipelineStore *GraphicsPipelines )
{
	if ( Textures || !Geometry.Initialize( Device, Width, Height, Depth, DepthHeap, Buffers, Queue, TextureStore, GraphicsPipelines ) )
	{
		return false;
	}
	Textures = &InTextures;
	Text = &InText;
	return true;
}

bool CDX12GameRenderer::BindTarget( int32 Width, int32 Height, ID3D12Resource *Depth, ID3D12DescriptorHeap *DepthHeap )
{
	return !bActive && Textures && Geometry.BindTarget( Width, Height, Depth, DepthHeap );
}

bool CDX12GameRenderer::BeginFrame( ID3D12GraphicsCommandList *Commands, D3D12_CPU_DESCRIPTOR_HANDLE ColorView, ID3D12DescriptorHeap *Images, float32 Scale, bool bClearDepth, float32 ClearDepth, uint64 ForbiddenTextureBinding )
{
	const D3D12_CPU_DESCRIPTOR_HANDLE Views[1] = { ColorView };
	const uint64 Forbidden[1] = { ForbiddenTextureBinding };
	return BeginFrame( Commands, TArrayView<const D3D12_CPU_DESCRIPTOR_HANDLE>( Views, 1 ), Images, Scale, bClearDepth, ClearDepth, ForbiddenTextureBinding != 0 ? TArrayView<const uint64>( Forbidden, 1 ) : TArrayView<const uint64>() );
}

bool CDX12GameRenderer::BeginFrame( ID3D12GraphicsCommandList *Commands, TArrayView<const D3D12_CPU_DESCRIPTOR_HANDLE> ColorViews, ID3D12DescriptorHeap *Images, float32 Scale, bool bClearDepth, float32 ClearDepth, TArrayView<const uint64> ForbiddenTextureBindings )
{
	if ( bActive || !Textures || !Text || !NMath::IsFinite( Scale ) || Scale <= 0 || Scale > 16384 || ColorViews.Num() <= 0 || !Geometry.BeginFrame( Commands, ColorViews, Images, &Clip, bClearDepth, ClearDepth, ForbiddenTextureBindings ) )
	{
		return false;
	}
	Clip = { 0, 0, Geometry.GetWidth(), Geometry.GetHeight() }; // MRTでCanvasが無効なパスでも描画先全体の切り取りを初期化する。
	bAllowCanvas = ColorViews.Num() == 1;
	if ( bAllowCanvas && !Canvas.BeginFrame( Geometry, *Textures, *Text, Clip, Scale ) )
	{
		Geometry.EndFrame();
		bAllowCanvas = false;
		return false;
	}
	bDrawSucceeded = true;
	bActive = true;
	return true;
}

bool CDX12GameRenderer::EndFrame()
{
	const bool bResult = !bActive || ( bDrawSucceeded && ( !bAllowCanvas || Canvas.DidFrameSucceed() ) && Geometry.DidFrameSucceed() ); // 失敗した文字や自己参照競合を成功扱いしない。
	if ( bAllowCanvas )
	{
		Canvas.EndFrame();
	}
	Geometry.EndFrame();
	bAllowCanvas = false;
	bActive = false;
	return bResult;
}

void CDX12GameRenderer::MoveFrameResources( TArray<FDX12BufferBinding> &OutBuffers, TArray<Microsoft::WRL::ComPtr<ID3D12Resource>> &OutUploads )
{
	if ( !bActive )
	{
		Geometry.MoveFrameResources( OutBuffers, OutUploads );
	}
}

void CDX12GameRenderer::MoveFrameResources( TArray<FDX12BufferBinding> &OutBuffers, TArray<Microsoft::WRL::ComPtr<ID3D12Resource>> &OutUploads, TArray<FDX12TextureBinding> &OutTextures, TArray<FDX12GraphicsPipelineBinding> &OutPipelines )
{
	if ( !bActive )
	{
		Geometry.MoveFrameResources( OutBuffers, OutUploads, OutTextures, OutPipelines );
	}
}

IRenderCanvas2D *CDX12GameRenderer::GetCanvas2D() { return bActive && bAllowCanvas ? &Canvas : nullptr; }
IRenderGeometry3D *CDX12GameRenderer::GetGeometry3D() { return bActive ? &Geometry : nullptr; }
void CDX12GameRenderer::RecordClear( FColor32 Color )
{
	if ( bActive )
	{
		bDrawSucceeded = Geometry.ClearColor( Color ) && bDrawSucceeded;
	}
}
