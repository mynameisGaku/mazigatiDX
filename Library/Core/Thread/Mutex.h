// SPDX-License-Identifier: NOASSERTION
#pragma once

#include <condition_variable>   // 中身は std の mutex / condition_variable。標準ライブラリに触れるのは、Core/Thread の三つのヘッダーだけに閉じ込める
#include <mutex>

#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"

class FUniqueLock;
class FConditionVariable;

//=============================================================================
// FMutex
//   同時に一つのスレッドだけが入れる区間を作る排他ロック。std::mutex の代わり。再帰（同じスレッドで二重に取ること）はできない。
//   取りっぱなしにならないよう、直接 Lock / Unlock せず、FScopedLock を使う。
//=============================================================================
class FMutex
{
public:
	FMutex() = default;
	FMutex( const FMutex& ) = delete;
	FMutex& operator=( const FMutex& ) = delete;

	// ロックを取る。ほかのスレッドが持っている間は待つ。
	FORCEINLINE void Lock() { Native.lock(); }
	// ロックを手放す。取っているスレッドだけが呼べる。
	FORCEINLINE void Unlock() { Native.unlock(); }
	// 待たずにロックを取る。取れたら true。
	FORCEINLINE bool TryLock() { return Native.try_lock(); }

private:
	friend class FUniqueLock;
	// 中身。
	std::mutex Native;
};

//=============================================================================
// FScopedLock
//   作った場所から、そのブロックを抜けるまでロックを持つ。std::lock_guard<std::mutex> の代わり。
//
//   FScopedLock Lock( Mutex );   // 以降、ブロックの終わりまで排他
//=============================================================================
class FScopedLock
{
public:
	// ロックを取る。Mutex は、このオブジェクトより長く生きていること。
	FORCEINLINE explicit FScopedLock( FMutex& InMutex ) : Mutex( InMutex ) { Mutex.Lock(); }
	FORCEINLINE ~FScopedLock() { Mutex.Unlock(); }

	FScopedLock( const FScopedLock& ) = delete;
	FScopedLock& operator=( const FScopedLock& ) = delete;

private:
	// 持っているロック。
	FMutex& Mutex;
};

//=============================================================================
// FUniqueLock
//   途中で手放し、また取り直せるロック。FConditionVariable で待つときに使う。std::unique_lock<std::mutex> の代わり。
//   作った時点でロックを取る。
//=============================================================================
class FUniqueLock
{
public:
	// ロックを取る。
	FORCEINLINE explicit FUniqueLock( FMutex& InMutex ) : NativeLock( InMutex.Native ) {}

	FUniqueLock( const FUniqueLock& ) = delete;
	FUniqueLock& operator=( const FUniqueLock& ) = delete;

	// ロックを取り直す。持っているときは呼べない。
	FORCEINLINE void Lock() { NativeLock.lock(); }
	// ロックを手放す。持っているときだけ呼べる。
	FORCEINLINE void Unlock() { NativeLock.unlock(); }
	// ロックを持っているか。
	FORCEINLINE bool OwnsLock() const { return NativeLock.owns_lock(); }

private:
	friend class FConditionVariable;
	// 中身。
	std::unique_lock<std::mutex> NativeLock;
};

//=============================================================================
// FConditionVariable
//   「条件が整うまで眠って待つ」仕組み。std::condition_variable の代わり。
//   待つ側は FUniqueLock を持った状態で Wait を呼ぶ（待っている間はロックを手放し、起きるときに取り直す）。
//
//   Condition.Wait( Lock, [this] { return bStopping || HasWork(); } );   // 条件が真になるまで待つ
//   Condition.NotifyAll();                                               // 待っている全員を起こす
//=============================================================================
class FConditionVariable
{
public:
	FConditionVariable() = default;
	FConditionVariable( const FConditionVariable& ) = delete;
	FConditionVariable& operator=( const FConditionVariable& ) = delete;

	// 待っているスレッドを一つ起こす。
	FORCEINLINE void NotifyOne() { Native.notify_one(); }
	// 待っているスレッドをすべて起こす。
	FORCEINLINE void NotifyAll() { Native.notify_all(); }
	// 起こされるまで待つ。偽の起床があるので、条件つきの Wait を使うほうが安全。Lock は取っていること。
	FORCEINLINE void Wait( FUniqueLock& Lock ) { Native.wait( Lock.NativeLock ); }
	// Predicate（引数なしで bool を返す）が true になるまで待つ。Lock は取っていること。
	template<typename PredicateType>
	FORCEINLINE void Wait( FUniqueLock& Lock, PredicateType Predicate ) { Native.wait( Lock.NativeLock, Predicate ); }

private:
	// 中身。
	std::condition_variable Native;
};
