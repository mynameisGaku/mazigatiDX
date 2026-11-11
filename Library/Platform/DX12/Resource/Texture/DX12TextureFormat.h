// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/ComputeTextureCapabilities.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>

// 形式に関する値の照会だけを行う。資源とサービスの所有者は保持しない。
namespace NDX12TextureFormat
{
	// 実デバイスのRGBA8に必要な読み取りとtyped UAVの能力を調べる。照会失敗・喪失ではOutを保つ。
	bool GetComputeCapabilities( ID3D12Device *Device, FComputeTextureCapabilities &Out );
} // namespace NDX12TextureFormat
