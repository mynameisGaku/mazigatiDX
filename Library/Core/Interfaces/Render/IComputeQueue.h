// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Render/ComputeDispatch.h"
#include "Library/Core/Render/ComputeSubmission.h"
#include "Library/Core/Render/GpuTimestampHandle.h"

// ゲームスレッドからcompute命令をまとめて提出し、完了を確認する。資源の登録やシェーダー生成とは別の責務。
class IComputeQueue
{
public:
	// 所有者はGPUの完了またはデバイス喪失を保証してから破棄する。
	virtual ~IComputeQueue() = default;
	// 全命令を検査してから順に提出し、完了を待たずに戻る。不正・容量超過・GPU失敗は無効値。
	// 不正な命令列は一部だけを実行しない。提出後の番号解除や呼出側の配列変更から必要な寿命を独立させる。
	// GPUへ提出した後の失敗では、すでに行った書き込みを巻き戻すことはできない。
	virtual FComputeSubmission SubmitCompute( TArrayView<const FComputeDispatch> Dispatches ) = 0;
	// 全compute命令の前後をGPUタイムスタンプで計測しながら同じキューへ提出する。未対応・不正番号・計測中は無効値。
	virtual FComputeSubmission SubmitComputeWithTimestamp( TArrayView<const FComputeDispatch> Dispatches, FGpuTimestampHandle Timestamp ) { (void)Dispatches; (void)Timestamp; return FComputeSubmission(); }
	// 有効な提出の完了状態を待たずに返す。不正な値・GPU失敗ではOutCompletedを保ってfalse。
	virtual bool GetComputeCompletion( FComputeSubmission Submission, bool& OutCompleted ) = 0;
	// 有効な提出を指定したミリ秒まで待つ。初期経路の上限は60000ms。0は待たない。
	// タイムアウト・不正値・GPU失敗でfalse。タイムアウトだけでは資源を回収せず、再照会や待機を許す。
	virtual bool WaitCompute( FComputeSubmission Submission, uint32 TimeoutMs ) = 0;
};
