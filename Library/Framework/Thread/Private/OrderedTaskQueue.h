// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Queue.h"
#include "Library/Core/Thread/TaskFence.h"
#include "Library/Core/Thread/TaskFenceState.h"
#include "Library/Core/Types/Callable.h"

// 直列ジョブと専用スレッドで共用する内部の受付・実行・完了管理。一度に1つの実行側だけが取り出す。
class COrderedTaskQueue
{
  public:
	// 受付を開始する。保持できる未実行の仕事は4096個まで。
	COrderedTaskQueue();
	// 値を所有する仕事を受付順に積む。空、終了後、満杯、番号枯渇なら無効票。新たな実行側が必要ならOutStartをtrueにする。
	FTaskFence Enqueue( TCallable<void()> Task, const char *Name, bool &OutStart );
	// 現在までの受付を区切る。bCloseなら以後の受付も終了する。
	FTaskFence GetFence( bool bClose );
	// 先頭を実行する。bWaitなら次の受付か終了まで眠る。空または全件終了ならfalse。
	bool ExecuteOne( bool bWait );
	// 起動ジョブが拒否された場合だけ、未実行値を手放し、未完了票の待機を失敗で解く。
	void Abort();

  private:
	// 受付時点で値を所有する仕事と、その完了番号。
	struct FEntry
	{
		// 実行後、完了通知より前に手放す仕事本体。
		TCallable<void()> Task;
		// 文字列リテラルで渡す計測名。
		const char *Name = "OrderedTask";
		// この仕事の受付番号。
		uint64 Sequence = 0;
	};
	// 実行側の寿命を越えて票へ残す同期状態。Mutexは下の全フィールドも保護する。
	TSharedPtr<FTaskFenceState> State;
	// まだ取り出していない仕事。
	TQueue<FEntry> Pending;
	// 最後に受け付けた番号。
	uint64 Issued = 0;
	// 新しい受付を許すか。
	bool bAccepting = true;
	// 取り出す仕事があるか、実行側を起動済みか。
	bool bRunning = false;
};
