// SPDX-License-Identifier: MIT
#include "RenderThread.h"

#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Thread/Thread.h"
#include "Library/Core/Log/Log.h"

DEFINE_LOG_CATEGORY_STATIC( LogRenderThread, Log )

namespace
{
	// 識別と誤呼出し数だけを保持する、共有の記録値。
	struct FRenderThreadState
	{
		// 登録された描画スレッド。0は未登録で、終了時に必ず戻す。
		TAtomic<uint64> Id{ 0 };
		// 専用描画スレッド以外からの誤った呼出し数。
		TAtomic<int32> WrongThreadCalls{ 0 };
	};
	// 起動順序に依存せず、最初の識別要求で共有状態を用意する。
	FRenderThreadState &State()
	{
		static FRenderThreadState Instance;
		return Instance;
	}
} // namespace

bool NRenderThread::Bind()
{
	// CASで登録を1本に限定し、同時起動した別の所有者には譲らない。
	uint64 Expected = 0;
	const uint64 Current = NThread::GetCurrentId();
	return State().Id.CompareExchangeStrong( Expected, Current, EMemoryOrder::AcquireRelease, EMemoryOrder::Acquire ) || Expected == Current;
}

bool NRenderThread::Unbind()
{
	uint64 Expected = NThread::GetCurrentId();
	return State().Id.CompareExchangeStrong( Expected, 0, EMemoryOrder::AcquireRelease, EMemoryOrder::Acquire );
}

bool NRenderThread::IsBound() { return State().Id.Load( EMemoryOrder::Acquire ) != 0; }
bool NRenderThread::IsCurrent() { return State().Id.Load( EMemoryOrder::Acquire ) == NThread::GetCurrentId(); }

void NRenderThread::ReportWrongThread( const char *Function )
{
	State().WrongThreadCalls.FetchAdd( 1, EMemoryOrder::Relaxed );
	LOG( LogRenderThread, Error, "%s must be called on the render thread.", Function ? Function : "?" );
}

int32 NRenderThread::GetNumWrongThreadCalls() { return State().WrongThreadCalls.Load( EMemoryOrder::Relaxed ); }
