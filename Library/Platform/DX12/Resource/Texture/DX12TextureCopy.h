// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>

// 描画先の色の一部を、画像へ写す命令を記録する。
namespace NDX12TextureCopy
{
	// 色の資源 Source（SourceState の状態）の矩形 [X0, X1) x [Y0, Y1) を、同じ形式・同じ大きさの画像 Dest（定常のSRV状態）の左上へ写す命令を、Commands へ記録する。
	// 写したあとは、両方を元の状態へ戻す。矩形が資源の外・Dest の大きさが矩形と違う・形式が違う・同じ資源のときは、何も記録せず false。
	bool RecordRegionCopy( ID3D12GraphicsCommandList* Commands, ID3D12Resource* Source, D3D12_RESOURCE_STATES SourceState, ID3D12Resource* Dest, int32 X0, int32 Y0, int32 X1, int32 Y1 );
}
