// SPDX-License-Identifier: MIT
#pragma once

#include <thread>   // 中身は std::thread。標準ライブラリに触れるのは、Core/Thread の三つのヘッダーだけに閉じ込める
#include <utility>

#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// NThread
//   スレッドを調べる・譲る関数。std::this_thread / std::thread の静的な部分の代わり。
//=============================================================================
namespace NThread
{
	// 今動いているスレッドの識別子。生きている間は、スレッドごとに違う値で、同じスレッドなら変わらない。
	// 終わったスレッドの値は、あとから別のスレッドに再利用されうる。
	FORCEINLINE uint64 GetCurrentId()
	{
		// スレッドごとに別の場所にある変数のアドレスを、識別子にする
		static thread_local char Marker = 0;
		return static_cast<uint64>( reinterpret_cast<uintptr_t>( &Marker ) );
	}

	// 同時に動かせるスレッドの数の目安（論理コア数）。わからないときは 0。
	FORCEINLINE int32 GetHardwareConcurrency() { return static_cast<int32>( std::thread::hardware_concurrency() ); }

	// 実行を、待っているほかのスレッドへ譲る。（名前の Yield は、windows.h が空のマクロとして定義しているため使えない）
	FORCEINLINE void YieldToOthers() { std::this_thread::yield(); }
}

//=============================================================================
// FThread
//   別のスレッドで関数を動かす。std::thread の代わり。コピーはできず、移動はできる。
//   作ると同時に動き出す。壊す前に Join すること（Join し忘れると終了する）。
//
//   FThread Worker( [this] { RunWorker(); } );
//   Worker.Join();
//=============================================================================
class FThread
{
public:
	// 何も動かさない状態。
	FThread() noexcept = default;
	// Function を新しいスレッドで動かす。Function は呼び出せるもの（引数なし）。
	template<typename FunctionType>
	explicit FThread( FunctionType&& Function ) : Native( std::forward<FunctionType>( Function ) ) {}

	FThread( const FThread& ) = delete;
	FThread& operator=( const FThread& ) = delete;
	FThread( FThread&& ) noexcept = default;
	FThread& operator=( FThread&& ) noexcept = default;

	// スレッドが動いていて、まだ Join していないか。
	FORCEINLINE bool IsJoinable() const noexcept { return Native.joinable(); }
	// スレッドが終わるまで待つ。IsJoinable のときだけ呼べる。
	FORCEINLINE void Join() { Native.join(); }

private:
	// 中身。
	std::thread Native;
};
