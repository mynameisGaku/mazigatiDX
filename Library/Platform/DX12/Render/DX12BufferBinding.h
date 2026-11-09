// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>
#include <wrl/client.h>

// 記録中の番号検証後に取得する実体の参照。提出先が完了まで保持し、API共通層へは渡さない。
struct FDX12BufferBinding
{
	Microsoft::WRL::ComPtr<ID3D12Resource> Resource; // 登録解除後も提出済み処理が保持する参照。
	uint64 Size = 0; // SRV/UAVに設定する範囲のバイト数。
};
