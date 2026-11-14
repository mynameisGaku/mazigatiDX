// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/GpuTimestampCapabilities.h"
#include "Library/Core/Render/GpuTimestampHandle.h"
#include "Library/Core/Render/GpuTimestampResult.h"

// GPUタイムスタンプ計測の能力照会・生成・解放と、Fence完了後の非同期解決読み取りを行う窓口。
// 記録コマンドの挿入はIRenderSurfaceまたはIComputeQueueで行い、所有権を分離する。
class IGpuTimestampResources
{
public:
	// 窓口経由の破棄を許可する。
	virtual ~IGpuTimestampResources() = default;
	// GPUタイムスタンプ計測への対応可否・周波数・上限数を返す。失敗や未初期化時はOutを保つ。
	virtual bool GetGpuTimestampCapabilities( FGpuTimestampCapabilities& Out ) const = 0;
	// タイムスタンプ計測ペアを作成する。未対応・同時上限超過・GPU異常時は無効値。
	virtual FGpuTimestampHandle CreateGpuTimestampQuery() = 0;
	// 番号を直ちに無効化し、未完了提出や記録中スロットのGPU完了後に物理資源を回収する。
	virtual void ReleaseGpuTimestampQuery( FGpuTimestampHandle Query ) = 0;
	// 有効な計測のGPU解決が完了したか待たずに照会する。不正番号・GPU失敗時はOutReadyを保ってfalse。
	virtual bool IsGpuTimestampReady( FGpuTimestampHandle Query, bool& OutReady ) = 0;
	// 提出済みのGPUタイムスタンプが解決するまで最大TimeoutMs待つ（上限60000ms、0は待たない）。
	virtual bool WaitGpuTimestamp( FGpuTimestampHandle Query, uint32 TimeoutMs = 10000 ) = 0;
	// GPU完了済みの計測結果を返す。未提出・記録中・未完了・不正番号・GPU失敗時はOutを保つ。
	virtual bool GetGpuTimestampResult( FGpuTimestampHandle Query, FGpuTimestampResult& Out ) = 0;
};
