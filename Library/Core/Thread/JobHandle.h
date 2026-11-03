// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Thread/JobState.h"
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/Utility.h"

// Scheduleで投げた単発ジョブを参照し、完了確認や依存登録に使うhandle。
class FJobHandle
{
public:
	// 空のhandleを作る。空の場合、待機対象はない。
	FJobHandle() = default;
	// 共有ジョブ状態からhandleを作る。
	explicit FJobHandle( TSharedPtr<FJobState> InState ) : State( MoveTemp( InState ) ) {}

	// ジョブが完了済みか返す。空のhandleも完了済みとして扱う。
	FORCEINLINE bool IsDone() const { return State == nullptr || State->bDone.Load( EMemoryOrder::Acquire ); }
	// 有効な共有ジョブ状態を持つか返す。
	FORCEINLINE bool IsValid() const { return State != nullptr; }

	// スケジューラー間で共有するジョブ状態。
	TSharedPtr<FJobState> State;
};
