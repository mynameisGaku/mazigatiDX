// SPDX-License-Identifier: MIT
#include "BufferResourceHandle.h"
#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Types/Numeric.h"

FBufferResourceHandle FBufferResourceHandle::Allocate()
{
	static TAtomic<uint64> Next( 1 ); // 所有先の作り直し後も再利用しないプロセス内の番号。
	uint64 Candidate = Next.Load( EMemoryOrder::Relaxed ); // 他の所有先との競合時には再取得する。
	while ( Candidate != TNumericLimits<uint64>::Max() )
	{
		if ( Next.CompareExchangeWeak( Candidate, Candidate + 1, EMemoryOrder::Relaxed, EMemoryOrder::Relaxed ) )
		{
			return FBufferResourceHandle( Candidate );
		}
	}
	return FBufferResourceHandle();
}
