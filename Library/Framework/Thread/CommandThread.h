// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Thread/TaskFence.h"
#include "Library/Core/Thread/Thread.h"
#include "Library/Core/Types/Callable.h"

class COrderedTaskQueue;

// 同じ専用スレッドで仕事を受付順に実行する。描画APIなどスレッドの固定が必要な所有者が保持する。
class CCommandThread
{
  public:
	// 文字列リテラルの計測名を渡して専用スレッドを開始する。OSの生成失敗はFThreadと同じ例外になる。
	explicit CCommandThread( const char *Name );
	// 受付済みの仕事を全件実行してから結合する。作成スレッドだけで破棄し、他の公開呼出しと寿命を競合させない。
	~CCommandThread();
	// 任意のスレッドから例外を投げない仕事を積む。Nameは文字列リテラル。空、満杯、受付終了後なら無効票。
	FTaskFence Enqueue( TCallable<void()> Task, const char *Name = "CommandThread" );
	// 取得した時点までのCPU処理の完了票を返す。
	FTaskFence GetFence() const;
	// 任意のスレッドから受付を閉じる。受け付けた仕事は全件実行する。
	FTaskFence RequestStop();
	// 作成スレッドで受付終了と結合を行う。他のスレッドではfalse。繰り返してもよい。
	bool StopAndJoin();
	CCommandThread( const CCommandThread & ) = delete;
	CCommandThread &operator=( const CCommandThread & ) = delete;

  private:
	// 完了票と共有する、専用スレッドの実行列。
	TSharedPtr<COrderedTaskQueue> Queue;
	// 結合と破棄を担当する作成スレッドの識別子。
	uint64 ControlThread = 0;
	// 起動した1本のスレッド。先行するフィールドの構築後に開始する。
	FThread Worker;
};
