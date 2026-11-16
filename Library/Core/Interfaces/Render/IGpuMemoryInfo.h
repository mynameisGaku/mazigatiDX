// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// IGpuMemoryInfo
//   GPU のメモリ（ビデオメモリ）の、使用量と、このプロセスが使える量を、読むだけの窓口。
//   実装: CDX12PlatformSubsystem（DX12）。ゲームは GetAppSubsystem<IGpuMemoryInfo>() で取る。メインスレッドから使う。
//=============================================================================
class IGpuMemoryInfo
{
public:
	virtual ~IGpuMemoryInfo() = default;

	// このプロセスの、GPU 専用メモリの使用量と、使える量の目安（OS が割り当てる予算。バイト）。調べられなければ false で、Out は変えない
	virtual bool QueryGpuMemory( int64& OutUsedBytes, int64& OutBudgetBytes ) const = 0;
};
