// SPDX-License-Identifier: MIT
#include "RenderResourceSerial.h"
#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Types/Numeric.h"

uint64 NRenderResourceSerial::Allocate()
{
	static TAtomic<uint64> Next( 1 ); // この実行中は所有先が変わっても再利用しない番号。
	uint64 Candidate = Next.Load( EMemoryOrder::Relaxed ); // 競合時に再取得する候補。
	while ( Candidate != TNumericLimits<uint64>::Max() )
	{
		if ( Next.CompareExchangeWeak( Candidate, Candidate + 1, EMemoryOrder::Relaxed, EMemoryOrder::Relaxed ) )
		{
			return Candidate;
		}
	}
	return 0;
}
