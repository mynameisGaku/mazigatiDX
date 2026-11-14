// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// 実行先がGPUタイムスタンプ計測に対応するかを表す能力値。
struct FGpuTimestampCapabilities
{
	// 同一DIRECT / COMPUTEキュー上でのタイムスタンプ計測に対応しているか。
	bool bSupported = false;
	// 1秒あたりのGPUタイムスタンプティック周波数（Hz）。未対応時は0。
	uint64 FrequencyHz = 0;
	// 同時に作成・保持できる未解放タイムスタンプ計測の上限数。未対応時は0。
	uint32 MaxActiveQueries = 0;
};
