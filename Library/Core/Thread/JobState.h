// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Thread/Mutex.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Core/Types/TypeDefinition.h"

class IJobSystem;

// スケジューラーが実行待ち、依存関係、完了状態を共有するジョブの状態。
struct FJobState
{
	// 依存を登録できる発行元。設定後は変えず、handleから呼び出さない。
	const IJobSystem* Owner = nullptr;
	// 実行時に呼び出すジョブ本体。
	TCallable<void()> Function;
	// プロファイラーに表示するジョブ名。
	const char* Name = "Job";
	// 実行待ちと未完了の依存先を合わせた数。
	TAtomic<int32> NumPending{ 0 };
	// ジョブ本体と依存先がすべて完了したか。
	TAtomic<bool> bDone{ false };
	// 完了状態と依存先一覧を保護するmutex。
	FMutex Mutex;
	// このジョブの完了後に実行可能になるジョブ。
	TArray<TSharedPtr<FJobState>> Dependents;
};
