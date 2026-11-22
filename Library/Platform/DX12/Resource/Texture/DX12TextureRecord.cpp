// SPDX-License-Identifier: MIT
#include "DX12TextureRecord.h"

bool NDX12TextureKind::IsRGBA8( EDX12TextureKind Kind )
{
	return Kind == EDX12TextureKind::StandardRGBA8 || Kind == EDX12TextureKind::ComputeRGBA8 || Kind == EDX12TextureKind::RenderTargetRGBA8;
}

bool NDX12TextureKind::IsRGBA16F( EDX12TextureKind Kind )
{
	return Kind == EDX12TextureKind::ComputeRGBA16F || Kind == EDX12TextureKind::RenderTargetRGBA16F;
}

bool NDX12TextureKind::IsR32Float( EDX12TextureKind Kind )
{
	return Kind == EDX12TextureKind::DepthD32 || Kind == EDX12TextureKind::RenderTargetR32Float;
}

FDX12TextureBinding FDX12TextureRecord::MakeBinding( EComputeTextureFormat Format ) const
{
	FDX12TextureBinding Binding;
	Binding.Resource = Resource;
	Binding.TargetHeap = TargetHeap;
	Binding.SrvGpuDescriptor = Descriptor.Gpu.ptr;
	Binding.Width = Width;
	Binding.Height = Height;
	Binding.InitialClearColor = InitialClearColor;
	Binding.InitialClearColorF = InitialClearColorF;
	Binding.InitialClearDepth = InitialClearDepth;
	Binding.Format = Format;
	return Binding;
}
