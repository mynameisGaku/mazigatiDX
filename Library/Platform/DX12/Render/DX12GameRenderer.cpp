// SPDX-License-Identifier: MIT
#include "DX12GameRenderer.h"
#include "Library/Core/Types/Numeric.h"

FDX12GameRenderer::~FDX12GameRenderer() { EndFrame(); }

bool FDX12GameRenderer::Initialize( ID3D12Device *Device, int32 Width, int32 Height, IRenderTextureResources &InTextures, IRenderTextRasterizer &InText, ID3D12Resource *Depth, ID3D12DescriptorHeap *DepthHeap )
{
	if ( Textures || !Geometry.Initialize( Device, Width, Height, Depth, DepthHeap ) )
	{
		return false;
	}
	Textures = &InTextures;
	Text = &InText;
	return true;
}

bool FDX12GameRenderer::BeginFrame( ID3D12GraphicsCommandList *Commands, D3D12_CPU_DESCRIPTOR_HANDLE ColorView, ID3D12DescriptorHeap *Images, float32 Scale )
{
	if ( bActive || !Textures || !Text || !NMath::IsFinite( Scale ) || Scale <= 0 || Scale > 16384 || !Geometry.BeginFrame( Commands, ColorView, Images, &Clip ) )
	{
		return false;
	}
	if ( !Canvas.BeginFrame( Geometry, *Textures, *Text, Clip, Scale ) )
	{
		Geometry.EndFrame();
		return false;
	}
	bDrawSucceeded = true;
	bActive = true;
	return true;
}

bool FDX12GameRenderer::EndFrame()
{
	const bool bResult = !bActive || ( bDrawSucceeded && Canvas.DidFrameSucceed() ); // 失敗した文字を黙って省略した画像を成功扱いしない。
	Canvas.EndFrame();
	Geometry.EndFrame();
	bActive = false;
	return bResult;
}

IRenderCanvas2D *FDX12GameRenderer::GetCanvas2D() { return bActive ? &Canvas : nullptr; }
IRenderGeometry3D *FDX12GameRenderer::GetGeometry3D() { return bActive ? &Geometry : nullptr; }
void FDX12GameRenderer::RecordClear( FColor32 Color )
{
	if ( bActive )
	{
		bDrawSucceeded = Geometry.ClearColor( Color ) && bDrawSucceeded;
	}
}
