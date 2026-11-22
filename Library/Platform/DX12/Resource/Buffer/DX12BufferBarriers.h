// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Platform/System/WindowsInclude.h"
#include <d3d12.h>

// GPU バッファの、提出の間の待機状態（COMMON）から、読み取りの用途への遷移を組み立てる。
namespace NDX12BufferBarriers
{
	// バリアの配列へ、同じ GPU バッファの必要な読み取り状態を、1 つの遷移にまとめて足す。
	void AccumulateReadState( TArray<D3D12_RESOURCE_BARRIER> &Barriers, ID3D12Resource *Resource, D3D12_RESOURCE_STATES ReadState );
}
