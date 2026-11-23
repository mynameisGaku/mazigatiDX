// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Time/PlatformTime.h"
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Core/Types/TypeDefinition.h"

#if defined( _MSC_VER ) && ( defined( _M_X64 ) || defined( _M_IX86 ) )
#include <intrin.h>
#endif

//=============================================================================
// NSpinWait
//   眠る前に、ごく短い間だけ、眠らずに待つ。
//   スレッドを眠らせて起こすと、1 回の往復に数 µs〜十数 µs かかる。相手の仕事がそれより短いときは、眠らずに待つほうが早く、受け取れる。
//   長引いたら、呼び出し側が、そのまま眠って待つ（このヘルパーは、待つ時間に上限があり、足りなければ false を返すだけ）。
//
//   if ( !NSpinWait::Until( [this] { return bReady.Load( EMemoryOrder::Acquire ); } ) )
//   {
//       // 眠って待つ
//   }
//=============================================================================
namespace NSpinWait
{
	// 眠らずに待つ、既定の上限（マイクロ秒）。
	constexpr int64 DefaultMicroseconds = 50;

	// CPU へ「待っている」と知らせる。同じコアの、もう 1 つのハードウェアスレッドへ実行資源を譲る。
	FORCEINLINE void Pause()
	{
#if defined( _MSC_VER ) && ( defined( _M_X64 ) || defined( _M_IX86 ) )
		_mm_pause();
#endif
	}

	// Predicate が真になるか、MaxMicroseconds が過ぎるまで、眠らずに待つ。真になったら true、過ぎたら false。
	template<typename PredicateType>
	bool Until( PredicateType Predicate, int64 MaxMicroseconds = DefaultMicroseconds )
	{
		if ( Predicate() )
		{
			return true;
		}
		const int64 EndTicks = NPlatformTime::GetTicks() + NPlatformTime::GetTicksPerSecond() * MaxMicroseconds / 1000000;
		for ( int32 Count = 1;; ++Count )
		{
			Pause();
			if ( Predicate() )
			{
				return true;
			}
			if ( ( Count & 15 ) == 0 && NPlatformTime::GetTicks() >= EndTicks ) // 時計を読むのは、16 回に 1 回。
			{
				return false;
			}
		}
	}
}
