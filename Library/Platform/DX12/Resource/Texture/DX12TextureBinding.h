// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>
#include <wrl/client.h>

// compute提出が画像の更新・登録解除後も保持する実体。描画用SRVの借用とは分ける。
struct FDX12TextureBinding
{
	// GPU処理の完了まで保持する画像。
	Microsoft::WRL::ComPtr<ID3D12Resource> Resource;
	// 検査済みのRGBA8画像の寸法。
	int32 Width = 0, Height = 0;
};
