// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>   // 中身は std::atomic。標準ライブラリに触れるのは、Core/Thread の三つのヘッダーだけに閉じ込める

#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// EMemoryOrder
//   アトミック操作が、前後のメモリ操作の並びをどこまで保証するか。std::memory_order の代わり。
//   迷ったら SequentiallyConsistent（既定）。値だけを数える場合は Relaxed、
//   「中身を書いてから公開する」場合は、書く側 Release・読む側 Acquire を使う。
//=============================================================================
enum class EMemoryOrder : uint8
{
	Relaxed,                  // 並びの保証なし（他の変数との順序に依存しない数え上げ・フラグ）
	Acquire,                  // 読み込み。これ以降の操作が、これより前へ移らない
	Release,                  // 書き込み。これより前の操作が、これより後へ移らない
	AcquireRelease,           // 読み書き。Acquire と Release の両方
	SequentiallyConsistent    // すべてのスレッドで同じ順序に見える（既定）
};

namespace NThreadPrivate
{
	// EMemoryOrder を std::memory_order に直す。
	constexpr std::memory_order ToStdOrder( EMemoryOrder Order )
	{
		switch ( Order )
		{
		case EMemoryOrder::Relaxed:        return std::memory_order_relaxed;
		case EMemoryOrder::Acquire:        return std::memory_order_acquire;
		case EMemoryOrder::Release:        return std::memory_order_release;
		case EMemoryOrder::AcquireRelease: return std::memory_order_acq_rel;
		default:                           return std::memory_order_seq_cst;
		}
	}
}

//=============================================================================
// TAtomic<T>
//   複数のスレッドから同時に読み書きしても壊れない値。std::atomic<T> の代わり。
//   T は整数・bool・列挙型・ポインター。コピーはできない。
//
//   TAtomic<int32> Count { 0 };
//   Count.FetchAdd( 1, EMemoryOrder::Relaxed );
//   const int32 Now = Count.Load( EMemoryOrder::Acquire );
//=============================================================================
template<typename T>
class TAtomic
{
public:
	// 0（ポインターは nullptr）で始める。
	constexpr TAtomic() noexcept : Value( T() ) {}
	// 与えた値で始める。
	constexpr TAtomic( T Initial ) noexcept : Value( Initial ) {}

	TAtomic( const TAtomic& ) = delete;
	TAtomic& operator=( const TAtomic& ) = delete;

	// 今の値を読む。
	FORCEINLINE T Load( EMemoryOrder Order = EMemoryOrder::SequentiallyConsistent ) const noexcept { return Value.load( NThreadPrivate::ToStdOrder( Order ) ); }
	// 値を書く。
	FORCEINLINE void Store( T Desired, EMemoryOrder Order = EMemoryOrder::SequentiallyConsistent ) noexcept { Value.store( Desired, NThreadPrivate::ToStdOrder( Order ) ); }
	// 値を書いて、書く前の値を返す。
	FORCEINLINE T Exchange( T Desired, EMemoryOrder Order = EMemoryOrder::SequentiallyConsistent ) noexcept { return Value.exchange( Desired, NThreadPrivate::ToStdOrder( Order ) ); }
	// 足して、足す前の値を返す（整数とポインター）。
	template<typename U>
	FORCEINLINE T FetchAdd( U Amount, EMemoryOrder Order = EMemoryOrder::SequentiallyConsistent ) noexcept { return Value.fetch_add( Amount, NThreadPrivate::ToStdOrder( Order ) ); }
	// 引いて、引く前の値を返す（整数とポインター）。
	template<typename U>
	FORCEINLINE T FetchSub( U Amount, EMemoryOrder Order = EMemoryOrder::SequentiallyConsistent ) noexcept { return Value.fetch_sub( Amount, NThreadPrivate::ToStdOrder( Order ) ); }
	// 今の値が Expected と等しければ Desired を書いて true。違えば Expected へ今の値を入れて false。
	// 偽の失敗が起きうるので、ループの中で使う。Success は成功時、Failure は失敗時の並びの保証。
	FORCEINLINE bool CompareExchangeWeak( T& Expected, T Desired, EMemoryOrder Success, EMemoryOrder Failure ) noexcept
	{
		return Value.compare_exchange_weak( Expected, Desired, NThreadPrivate::ToStdOrder( Success ), NThreadPrivate::ToStdOrder( Failure ) );
	}
	// CompareExchangeWeak と同じだが、偽の失敗は起きない。
	FORCEINLINE bool CompareExchangeStrong( T& Expected, T Desired, EMemoryOrder Success, EMemoryOrder Failure ) noexcept
	{
		return Value.compare_exchange_strong( Expected, Desired, NThreadPrivate::ToStdOrder( Success ), NThreadPrivate::ToStdOrder( Failure ) );
	}

	// 値として読む（SequentiallyConsistent）。
	FORCEINLINE operator T() const noexcept { return Load(); }
	// 値を書く（SequentiallyConsistent）。
	FORCEINLINE T operator=( T Desired ) noexcept { Store( Desired ); return Desired; }

private:
	// 中身。
	std::atomic<T> Value;
};
