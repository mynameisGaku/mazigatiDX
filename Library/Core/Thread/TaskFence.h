// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Types/TypeDefinition.h"

struct FTaskFenceState;
class COrderedTaskQueue;

// ある実行列の指定番号までのCPU処理が終わったかを共有する値。GPU完了は表さない。
class FTaskFence
{
  public:
	// 拒否された仕事と同じ、無効な票を作る。
	FTaskFence() = default;
	// 受け付けられた仕事か、実行列の区切りを表しているか返す。
	bool IsValid() const;
	// 指定番号までの本体と取り込んだ値の破棄が完了したか返す。無効な票はfalse。
	bool IsComplete() const;
	// 0〜60000ミリ秒の期限で眠って待つ。無効、期限切れ、実行不能、自分の実行列待ちはfalse。
	bool Wait( uint32 TimeoutMilliseconds = 60000 ) const;
	// 同じ実行列の受付順を表す番号を返す。異なる実行列同士では比較しない。
	uint64 GetSequence() const;

  private:
	friend class COrderedTaskQueue;
	// 実行列が発行する、共有状態と受付番号から票を作る。
	FTaskFence( TSharedPtr<FTaskFenceState> InState, uint64 InSequence );
	// 実行側が終了しても完了結果を保持する共有状態。
	TSharedPtr<FTaskFenceState> State;
	// この票が待つ受付番号。
	uint64 Sequence = 0;
};
