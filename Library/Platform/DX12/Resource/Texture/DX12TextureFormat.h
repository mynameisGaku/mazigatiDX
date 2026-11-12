// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/ComputeTextureCapabilities.h"
#include "Library/Core/Render/RenderTargetCapabilities.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>

// 形式に関する値の照会だけを行う。資源とサービスの所有者は保持しない。
namespace NDX12TextureFormat
{
	// 実デバイスのRGBA8とR32_FLOATに必要な読み取り・typed UAVの能力を調べる。照会失敗・喪失ではOutを保つ。
	bool GetComputeCapabilities( ID3D12Device *Device, FComputeTextureCapabilities &Out );
	// 実デバイスのRGBA8描画先と読み取れるD32深度の能力を調べる。照会失敗・喪失ではOutを保つ。
	bool GetRenderTargetCapabilities( ID3D12Device *Device, FRenderTargetCapabilities &Out );
} // namespace NDX12TextureFormat
