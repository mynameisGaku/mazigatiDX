// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

// GPU完了後に読み出された1組のGPUタイムスタンプ計測結果。
struct FGpuTimestampResult
{
	// 計測開始点でGPUが記録した生のタイムスタンプ値（ティック）。
	uint64 BeginTicks = 0;
	// 計測終了点でGPUが記録した生のタイムスタンプ値（ティック）。
	uint64 EndTicks = 0;
	// 経過時間の算出に用いたキューのタイムスタンプ周波数（Hz）。
	uint64 FrequencyHz = 0;
	// BeginからEndまでのGPU経過時間（ミリ秒）。
	float64 ElapsedMs = 0.0;
};
