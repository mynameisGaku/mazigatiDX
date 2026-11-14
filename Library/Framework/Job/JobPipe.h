// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Thread/TaskFence.h"
#include "Library/Core/Types/Callable.h"

class IJobSystem;
class COrderedTaskQueue;

// 同じ資源を扱う仕事を既存ジョブプール上で受付順に直列実行する。実行するスレッド自体は固定しない。
class CJobPipe
{
  public:
	// Jobsはこの実行列の受付と全仕事の完了まで生存させる。
	explicit CJobPipe( IJobSystem &InJobs );
	// 受付を終了する。実行中の自分からの破棄も可能だが、取り込んだ外部資源の寿命は完了票で守る。
	~CJobPipe();
	// 複数の要求元から例外を投げない仕事を積む。Nameは文字列リテラル。拒否なら無効票。ワーカー0では要求元で直列実行する。
	FTaskFence Enqueue( TCallable<void()> Task, const char *Name = "JobPipe" );
	// 取得した時点までの仕事の完了票を返す。
	FTaskFence GetFence() const;
	// 新しい受付を止め、その時点までの完了票を返す。既に受け付けた仕事は実行する。
	FTaskFence Close();
	CJobPipe( const CJobPipe & ) = delete;
	CJobPipe &operator=( const CJobPipe & ) = delete;

  private:
	// 共有ワーカーを占有し続けないよう、短いまとまりで取り出す実行ジョブを登録する。
	static bool ScheduleDrain( IJobSystem &Jobs, TSharedPtr<COrderedTaskQueue> Pending );
	// ワーカーを追加せず共有する既存スケジューラー。
	IJobSystem &Jobs;
	// 実行ジョブへも渡し、公開オブジェクトより長く保持できる実行列。
	TSharedPtr<COrderedTaskQueue> Queue;
};
